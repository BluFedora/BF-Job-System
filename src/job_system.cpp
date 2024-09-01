/******************************************************************************/
/*!
 * @file   job_system.cpp
 * @author Shareef Rahem (https://blufedora.github.io/)
 * @date   2020-09-03
 * @brief
 *    API for a multi-threading job system.
 *
 *    References:
 *      [https://blog.molecular-matters.com/2015/08/24/job-system-2-0-lock-free-work-stealing-part-1-basics/]
 *      [https://manu343726.github.io/2017-03-13-lock-free-job-stealing-task-system-with-modern-c/]
 *      [https://github.com/cdwfs/cds_job/blob/master/cds_job.h]
 *      [https://github.com/cyshi/logbook/blob/master/src/common/work_stealing_queue.h]
 *      [https://fabiensanglard.net/doom3_bfg/threading.php]
 *      [https://gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine]
 *
 * @copyright Copyright (c) 2020-2024 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#include "concurrent/job_api.hpp"

#include "concurrent/job_assert.hpp"  //
#include "concurrent/job_queue.hpp"

#include "pcg_basic.h" /* pcg_state_setseq_64, pcg32_srandom_r, pcg32_boundedrand_r */

#include <algorithm> /* partition, for_each, distance                                                   */
#include <cstdio>    /* fprintf, stderr                                                                 */
#include <cstdlib>   /* abort                                                                           */
#include <limits>    /* numeric_limits                                                                  */
#include <new>       /* hardware_constructive_interference_size, hardware_destructive_interference_size */
#include <thread>    /* thread                                                                          */

#if _WIN32
#define IS_WINDOWS         1
#define IS_POSIX           0
#define IS_SINGLE_THREADED 0
#elif __APPLE__
#define IS_WINDOWS         0
#define IS_POSIX           1
#define IS_SINGLE_THREADED 0
#elif (__ANDROID__ || __linux || __unix || __posix)
#define IS_WINDOWS         0
#define IS_POSIX           1
#define IS_SINGLE_THREADED 0
#elif __EMSCRIPTEN__
#define IS_WINDOWS         0
#define IS_POSIX           0
#define IS_SINGLE_THREADED 1
#endif

#if IS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VC_EXTRALEAN
#define WINDOWS_EXTRA_LEAN

#include <Windows.h> /* SYSTEM_INFO, GetSystemInfo */
#elif IS_POSIX
#include <unistd.h>  // also macOS 10.5+
#else                // macOS <= 10.4
// #include <sys/param.h>
// #include <sys/sysctl.h>
#endif

namespace Job
{
  // Constants

#ifdef __cpp_lib_hardware_interference_size
  static constexpr std::size_t k_CachelineSize = std::max(std::hardware_constructive_interference_size, std::hardware_destructive_interference_size);
#else
  static constexpr std::size_t k_CachelineSize = 64u;
#endif

  static constexpr std::size_t k_ExpectedTaskSize = std::max(std::size_t(128u), k_CachelineSize);
  static constexpr QueueType   k_InvalidQueueType = QueueType(int(QueueType::WORKER) + 1);

  // Type Aliases

  using TaskHandle           = std::uint16_t;
  using TaskHandleType       = TaskHandle;
  using AtomicTaskHandleType = std::atomic<TaskHandle>;
  using WorkerIDType         = WorkerID;
  using AtomicInt32          = std::atomic_int32_t;
  using Byte                 = unsigned char;

  static constexpr TaskHandle NullTaskHandle = std::numeric_limits<TaskHandle>::max();

  // Struct Definitions

  struct TaskPtr
  {
    WorkerID   worker_id;
    TaskHandle task_index;

    TaskPtr() noexcept = default;

    TaskPtr(WorkerID worker_id, TaskHandle task_idx) noexcept :
      worker_id{worker_id},
      task_index{task_idx}
    {
    }

    TaskPtr(std::nullptr_t) noexcept :
      worker_id{NullTaskHandle},
      task_index{NullTaskHandle}
    {
    }

    bool isNull() const noexcept { return task_index == NullTaskHandle; }
  };

  using AtomicTaskPtr = std::atomic<TaskPtr>;

  static_assert(sizeof(TaskPtr) == sizeof(std::uint16_t) * 2u, "Expected to be the size of two uint16's.");
  static_assert(sizeof(AtomicTaskPtr) == sizeof(TaskPtr) && AtomicTaskPtr::is_always_lock_free, "Expected to be lock-free so no extra data members should have been added.");

  // NOTE(SR):
  //   So that 32bit and 64bit build have the same `Task` layout.
  //   Allows for the the user data Task::padding to always be 32byte aligned.
  union TaskFnStorage
  {
    TaskFn        fn;
    std::uint64_t pad;

    TaskFnStorage(const TaskFn fn) noexcept :
      fn{fn}
    {
    }
  };
  static_assert(sizeof(TaskFnStorage) == sizeof(std::uint64_t) && alignof(TaskFnStorage) == alignof(std::uint64_t), "Expected to always be 8 bytes.");

  struct alignas(k_CachelineSize) Task
  {
    static constexpr std::size_t k_SizeOfMembers =
     sizeof(TaskFnStorage) +
     sizeof(AtomicInt32) +
     sizeof(AtomicInt32) +
     sizeof(std::uint8_t) +
     sizeof(QueueType) +
     sizeof(TaskPtr) +
     sizeof(AtomicTaskPtr) +
     sizeof(TaskPtr) +
     sizeof(WorkerID);

    static constexpr std::size_t k_TaskPaddingDataSize = k_ExpectedTaskSize - k_SizeOfMembers;

    TaskFnStorage fn_storage;                        //!< The function that will be run.
    AtomicInt32   num_unfinished_tasks;              //!< The number of children tasks.
    AtomicInt32   ref_count;                         //!< Keeps the task from being garbage collected.
    TaskPtr       parent;                            //!< The parent task, can be null.
    AtomicTaskPtr first_continuation;                //!< Head of linked list of tasks to be added on completion.
    TaskPtr       next_continuation;                 //!< Next element in the linked list of continuations.
    WorkerID      owning_worker;                     //!< The worker this task has been created on, needed for `Task::toTaskPtr` and various assertions.
    QueueType     q_type;                            //!< The queue type this task has been submitted to, initialized to k_InvalidQueueType.
    std::uint8_t  user_data_start;                   //!< Offset into `padding` that can be used for user data.
    Byte          user_data[k_TaskPaddingDataSize];  //!< User data storage.

    Task(WorkerID worker, TaskFn fn, TaskPtr parent) noexcept;
  };

  static_assert(sizeof(Task) == k_ExpectedTaskSize, "The task struct is expected to be this size.");
  static_assert(std::is_trivially_destructible_v<Task>, "Task must be trivially destructible.");

  union alignas(Task) TaskMemoryBlock
  {
    TaskMemoryBlock* next;
    unsigned char    storage[sizeof(Task)];
  };
  static_assert(sizeof(TaskMemoryBlock) == sizeof(Task) && alignof(TaskMemoryBlock) == alignof(Task), "TaskMemoryBlock should have no overhead.");

  struct TaskPool
  {
    TaskMemoryBlock* memory;
    TaskMemoryBlock* freelist;
  };

  struct ThreadLocalState
  {
    SPMCDeque<TaskPtr>  normal_queue;
    SPMCDeque<TaskPtr>  worker_queue;
    TaskPool            task_allocator;
    TaskHandle*         allocated_tasks;
    TaskHandleType      num_allocated_tasks;
    ThreadLocalState*   last_stolen_worker;
    pcg_state_setseq_64 rng_state;
    std::thread         thread_id;
  };

  struct InitializationLock
  {
    std::mutex              init_mutex        = {};
    std::condition_variable init_cv           = {};
    std::atomic_uint32_t    num_workers_ready = {};
  };

  struct JobSystemContext
  {
    // State that wont be changing during the system's runtime.

    ThreadLocalState*    workers;
    std::uint32_t        num_workers;
    std::uint32_t        num_owned_workers;
    std::atomic_uint32_t num_user_threads_setup;
    std::uint32_t        num_tasks_per_worker;
    InitializationLock   init_lock;
    const char*          sys_arch_str;
    std::size_t          system_alloc_size;
    std::size_t          system_alloc_alignment;
    bool                 needs_delete;
    std::atomic_bool     is_running;

    // Shared Mutable State

    LockedQueue<TaskPtr>    main_queue;
    std::mutex              worker_sleep_mutex;
    std::condition_variable worker_sleep_cv;
    std::atomic_uint32_t    num_available_jobs;
  };
}  // namespace Job

// System Globals

static Job::JobSystemContext*              g_JobSystem     = nullptr;
static thread_local Job::ThreadLocalState* g_CurrentWorker = nullptr;

// Internal API

#if JOB_SYS_ASSERTIONS

void Job::detail::assertHandler(const bool condition, const char* const filename, const int line_number, const char* const msg)
{
  if (!condition)
  {
    std::fprintf(stderr, "JobSystem [%s:%i] Assertion '%s' Failed.\n", filename, line_number, msg);
    std::abort();
  }
}

#endif

namespace
{
  using namespace Job;

  namespace system
  {
    static void WakeUpAllWorkers() noexcept
    {
      g_JobSystem->worker_sleep_cv.notify_all();
    }

    static void WakeUpOneWorker() noexcept
    {
      g_JobSystem->worker_sleep_cv.notify_one();
    }

    static void Sleep() noexcept
    {
      Job::JobSystemContext* const job_system = g_JobSystem;

      if (job_system->is_running.load(std::memory_order_relaxed))
      {
        Job::PauseProcessor();

        if (job_system->num_available_jobs.load(std::memory_order_relaxed) == 0u)
        {
          std::unique_lock<std::mutex> lock(job_system->worker_sleep_mutex);
          job_system->worker_sleep_cv.wait(lock, [job_system]() {
            // NOTE(SR):
            //   Because the stl wants 'false' to mean continue waiting the logic is a bit confusing :/
            //
            //   Returns false if the waiting should be continued, aka num_queued_jobs == 0u (also return true if not running).
            //
            //        Wait If:     running AND num_available_jobs == 0.
            // Do Not Wait If: not running  OR num_available_jobs != 0.
            //
            return !job_system->is_running || job_system->num_available_jobs.load(std::memory_order_relaxed) != 0; });
        }
      }
    }

    static ThreadLocalState* GetWorker(const WorkerID worker_id) noexcept
    {
      JobAssert(worker_id < NumWorkers(), "This thread was not created by the job system.");
      return g_JobSystem->workers + worker_id;
    }

  }  // namespace system

  namespace task_pool
  {
    static void Initialize(Job::TaskPool* const pool, Job::TaskMemoryBlock* const memory, const Job::TaskHandleType capacity) noexcept
    {
      const Job::TaskHandleType capacity_minus_one = capacity - 1;

      for (std::size_t i = 0u; i < capacity_minus_one; ++i)
      {
        memory[i].next = &memory[i + 1u];
      }
      memory[capacity_minus_one].next = nullptr;

      pool->memory   = memory;
      pool->freelist = &memory[0];
    }

    static TaskHandle TaskToIndex(const Job::TaskPool& pool, const Task* const task) noexcept
    {
      const TaskMemoryBlock* const block = reinterpret_cast<const TaskMemoryBlock*>(task);

      return TaskHandle(block - pool.memory);
    }

    static Task* TaskFromIndex(const Job::TaskPool& pool, const std::size_t idx) noexcept
    {
      return reinterpret_cast<Task*>(&pool.memory[idx].storage);
    }

    static Task* AllocateTask(Job::TaskPool* const pool, WorkerID worker, TaskFn fn, TaskPtr parent) noexcept
    {
      TaskMemoryBlock* const result = std::exchange(pool->freelist, pool->freelist->next);

      JobAssert(result != nullptr, "Allocation failure.");

      return new (result) Task(worker, fn, parent);
    }

    static void DeallocateTask(Job::TaskPool* const pool, Task* const task) noexcept
    {
      task->~Task();

      TaskMemoryBlock* const block = new (task) TaskMemoryBlock();

      block->next = std::exchange(pool->freelist, block);
    }
  }  // namespace task_pool

  namespace task
  {
    static Task* TaskPtrToPointer(const TaskPtr ptr) noexcept
    {
      if (!ptr.isNull())
      {
        ThreadLocalState* const worker = system::GetWorker(ptr.worker_id);
        Task* const             result = task_pool::TaskFromIndex(worker->task_allocator, ptr.task_index);

        JobAssert(ptr.worker_id == result->owning_worker, "Corrupted worker ID.");

        return result;
      }

      return nullptr;
    }

    static void TaskOnFinish(Task* const self) noexcept
    {
      const std::int32_t num_jobs_left = self->num_unfinished_tasks.fetch_sub(1, std::memory_order_relaxed) - 1;

      if (num_jobs_left == 0)
      {
        Task* const parent_task = task::TaskPtrToPointer(self->parent);

        if (parent_task)
        {
          TaskOnFinish(parent_task);
        }

        std::atomic_signal_fence(std::memory_order_release);

        self->num_unfinished_tasks.fetch_sub(1, std::memory_order_relaxed);

        TaskPtr continuation_ptr = self->first_continuation.load(std::memory_order_relaxed);

        while (!continuation_ptr.isNull())
        {
          Task* const     continuation = task::TaskPtrToPointer(continuation_ptr);
          const TaskPtr   next_task    = continuation->next_continuation;
          const QueueType q_type       = std::exchange(continuation->q_type, k_InvalidQueueType);

          TaskSubmit(continuation, q_type);

          continuation_ptr = next_task;
        }

        self->ref_count.fetch_sub(1, std::memory_order_relaxed);
      }
    }

    static void RunTaskFunction(Task* const self) noexcept
    {
      self->fn_storage.fn(self);
      TaskOnFinish(self);
    }

    static TaskPtr PointerToTaskPtr(const Task* const self) noexcept
    {
      if (self)
      {
        const ThreadLocalState& worker     = *system::GetWorker(self->owning_worker);
        const TaskHandle        self_index = task_pool::TaskToIndex(worker.task_allocator, self);

        return TaskPtr{self->owning_worker, self_index};
      }

      return TaskPtr(nullptr);
    }

  }  // namespace task

  namespace worker
  {
    static void GarbageCollectAllocatedTasks(Job::ThreadLocalState* const worker) noexcept
    {
      TaskHandle* const    allocated_tasks = worker->allocated_tasks;
      Job::TaskPool&       task_pool       = worker->task_allocator;
      const TaskHandleType num_tasks       = worker->num_allocated_tasks;
      TaskHandleType       read_idx        = 0u;
      TaskHandleType       write_idx       = 0u;

      while (read_idx != num_tasks)
      {
        const TaskHandle task_handle      = allocated_tasks[read_idx++];
        Task* const      task_ptr         = task_pool::TaskFromIndex(task_pool, task_handle);
        const bool       task_is_finished = task_ptr->ref_count.load(std::memory_order_acquire) == 0u;

        if (task_is_finished)
        {
          task_pool::DeallocateTask(&task_pool, task_ptr);
        }
        else
        {
          allocated_tasks[write_idx++] = task_handle;
        }
      }

      worker->num_allocated_tasks = write_idx;
    }

    static Job::ThreadLocalState* RandomWorker(Job::ThreadLocalState* const worker) noexcept
    {
      const std::uint32_t num_workers     = g_JobSystem->num_workers;
      const std::uint32_t other_worker_id = pcg32_boundedrand_r(&worker->rng_state, num_workers);

      return system::GetWorker(WorkerID(other_worker_id));
    }

    static bool IsMainThread(const ThreadLocalState* const worker) noexcept
    {
      return worker == g_JobSystem->workers;
    }

    static bool TryRunTask(ThreadLocalState* const worker) noexcept
    {
      const bool is_main_thread = IsMainThread(worker);

      TaskPtr task_ptr = nullptr;
      worker->normal_queue.Pop(&task_ptr);

      if (task_ptr.isNull() && !is_main_thread)
      {
        worker->worker_queue.Pop(&task_ptr);
      }

      const auto TrySteal = [is_main_thread, worker](ThreadLocalState* const other_worker) -> TaskPtr {
        TaskPtr result = nullptr;

        if (other_worker != worker)
        {
          other_worker->normal_queue.Steal(&result);

          if (result.isNull() && !is_main_thread)
          {
            other_worker->worker_queue.Steal(&result);
          }
        }

        return result;
      };

      if (task_ptr.isNull())
      {
        task_ptr = TrySteal(worker->last_stolen_worker);
      }

      if (task_ptr.isNull())
      {
        Job::ThreadLocalState* const random_worker = RandomWorker(worker);

        task_ptr = TrySteal(random_worker);

        if (task_ptr.isNull())
        {
          return false;
        }

        worker->last_stolen_worker = random_worker;
      }

      g_JobSystem->num_available_jobs.fetch_sub(1, std::memory_order_relaxed);

      Task* const task = task::TaskPtrToPointer(task_ptr);
      task::RunTaskFunction(task);

      return true;
    }

    static void WaitForAllThreadsReady(Job::JobSystemContext* const job_system) noexcept
    {
      Job::InitializationLock* const init_lock = &job_system->init_lock;

      if ((init_lock->num_workers_ready.fetch_add(1u, std::memory_order_relaxed) + 1) == g_JobSystem->num_workers)
      {
        job_system->is_running.store(true, std::memory_order_relaxed);
        init_lock->init_cv.notify_all();
      }
      else
      {
        std::unique_lock<std::mutex> lock(init_lock->init_mutex);
        init_lock->init_cv.wait(lock, [init_lock]() -> bool {
          return init_lock->num_workers_ready.load(std::memory_order_relaxed) == g_JobSystem->num_workers;
        });
      }
    }

    static Job::JobSystemContext* WorkerThreadSetup(Job::ThreadLocalState* const worker)
    {
      std::atomic_thread_fence(std::memory_order_acquire);

      Job::JobSystemContext* const job_system = g_JobSystem;

#if IS_WINDOWS
      const HANDLE handle = GetCurrentThread();

#if 0
          // Put each thread on to dedicated core

          const DWORD_PTR affinity_mask   = 1ull << thread_id;
          const DWORD_PTR affinity_result = SetThreadAffinityMask(handle, affinity_mask);

          if (affinity_result > 0)
          {
            // Increase thread priority

            // const BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_HIGHEST);
            // JobAssert(priority_result != 0, "Failed to set thread priority.");
          }
#endif
      // Name the thread

      const unsigned int thread_index = unsigned int(worker - job_system->workers);

      char    thread_name[32]                    = u8"";
      wchar_t thread_name_w[sizeof(thread_name)] = L"";

      const char* const format = thread_index >= job_system->num_owned_workers ? "Job::User_%u" : "Job::Owned_%u";

      const int c_size = std::snprintf(thread_name, sizeof(thread_name), format, thread_index);

      std::mbstowcs(thread_name_w, thread_name, c_size);

      const HRESULT hr = SetThreadDescription(handle, thread_name_w);
      JobAssert(SUCCEEDED(hr), "Failed to set thread name.");
      (void)hr;
#endif

      g_CurrentWorker = worker;

      WaitForAllThreadsReady(job_system);

      return job_system;
    }

    static void InitializeThread(Job::ThreadLocalState* const worker) noexcept
    {
      worker->thread_id = std::thread([worker]() {
        Job::JobSystemContext* const job_system = WorkerThreadSetup(worker);

        while (job_system->is_running.load(std::memory_order_relaxed))
        {
          if (!worker::TryRunTask(worker))
          {
            system::Sleep();
          }
        }
      });
    }

    static ThreadLocalState* GetCurrent() noexcept
    {
      JobAssert(g_CurrentWorker != nullptr, "This thread was not created by the job system.");
      return g_CurrentWorker;
    }

    static WorkerID GetCurrentID() noexcept
    {
      JobAssert(g_CurrentWorker != nullptr, "This thread was not created by the job system.");
      return WorkerID(g_CurrentWorker - g_JobSystem->workers);
    }

    static void ShutdownThread(Job::ThreadLocalState* const worker) noexcept
    {
      // Join throws an exception if the thread is not joinable. this should always be true.
      worker->thread_id.join();
    }
  }  // namespace worker

  namespace task
  {
    static void SubmitQPushHelper(const TaskPtr task_ptr, ThreadLocalState* const worker, SPMCDeque<TaskPtr>* queue) noexcept
    {
      if (queue->Push(task_ptr) != SPMCDequeStatus::SUCCESS)
      {
        // Loop until we have successfully pushed to the queue.
        system::WakeUpAllWorkers();
        while (queue->Push(task_ptr) != SPMCDequeStatus::SUCCESS)
        {
          // If we could not push to the queues then just do some work.
          worker::TryRunTask(worker);
        }
      }
    }
  }  // namespace task

  static bool IsPointerAligned(const void* const ptr, const std::size_t alignment) noexcept
  {
    return (reinterpret_cast<std::uintptr_t>(ptr) & (alignment - 1u)) == 0u;
  }

  static void* AlignPointer(const void* const ptr, const std::size_t alignment) noexcept
  {
    const std::size_t required_alignment_mask = alignment - 1;

    return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(ptr) + required_alignment_mask & ~required_alignment_mask);
  }

  template<typename T>
  struct Span
  {
    T*          ptr;
    std::size_t num_elements;
  };

  template<typename T>
  static Span<T> LinearAlloc(void*& ptr, const std::size_t num_elements) noexcept
  {
    void* const result = AlignPointer(ptr, alignof(T));

    ptr = static_cast<unsigned char*>(result) + sizeof(T) * num_elements;

    for (std::size_t i = 0; i < num_elements; ++i)
    {
      new (static_cast<T*>(result) + i) T;
    }

    return Span<T>{static_cast<T*>(result), num_elements};
  }

  template<typename T>
  static T* SpanAlloc(Span<T>* const span, const std::size_t num_elements) noexcept
  {
    JobAssert(num_elements <= span->num_elements, "Out of bounds span alloc.");

    T* const result = span->ptr;

    span->ptr += num_elements;
    span->num_elements -= num_elements;

    return result;
  }

  static std::size_t AlignedSizeUp(const std::size_t size, const std::size_t alignment) noexcept
  {
    const std::size_t remainder = size % alignment;

    return remainder != 0 ? size + (alignment - remainder) : size;
  }

  template<typename T>
  static void MemoryRequirementsPush(Job::JobSystemMemoryRequirements* in_out_reqs, const std::size_t num_elements) noexcept
  {
    in_out_reqs->byte_size = AlignedSizeUp(in_out_reqs->byte_size, alignof(T));
    in_out_reqs->alignment = in_out_reqs->alignment < alignof(T) ? alignof(T) : in_out_reqs->alignment;

    in_out_reqs->byte_size += sizeof(T) * num_elements;
  }

  static bool IsPowerOf2(const std::size_t value) noexcept
  {
    return (value & (value - 1)) == 0;
  }

  namespace config
  {
    static Job::WorkerID WorkerCount(const Job::JobSystemCreateOptions& options) noexcept
    {
      return (options.num_threads ? options.num_threads : Job::WorkerID(Job::NumSystemThreads())) + options.num_user_threads;
    }

    static std::uint16_t NumTasksPerWorker(const Job::JobSystemCreateOptions& options) noexcept
    {
      const std::size_t num_tasks_per_worker = std::size_t(options.normal_queue_size) + std::size_t(options.worker_queue_size);

      JobAssert(num_tasks_per_worker <= std::uint16_t(-1), "Too many task items per worker.");

      return std::uint16_t(num_tasks_per_worker);
    }

    static std::uint32_t TotalNumTasks(const Job::WorkerID num_threads, const std::uint16_t num_tasks_per_worker) noexcept
    {
      return num_tasks_per_worker * num_threads;
    }
  }  // namespace config

}  // namespace

// Public API

Job::JobSystemMemoryRequirements::JobSystemMemoryRequirements(const JobSystemCreateOptions& options) noexcept :
  options{options},
  byte_size{0},
  alignment{0}
{
  JobAssert(IsPowerOf2(options.main_queue_size), "Main queue size must be a power of two.");
  JobAssert(IsPowerOf2(options.normal_queue_size), "Normal queue size must be a power of two.");
  JobAssert(IsPowerOf2(options.worker_queue_size), "Worker queue size must be a power of two.");

  const WorkerID      num_threads          = config::WorkerCount(options);
  const std::uint16_t num_tasks_per_worker = config::NumTasksPerWorker(options);
  const std::uint32_t total_num_tasks      = config::TotalNumTasks(num_threads, num_tasks_per_worker);

  MemoryRequirementsPush<JobSystemContext>(this, 1u);
  MemoryRequirementsPush<ThreadLocalState>(this, num_threads);
  MemoryRequirementsPush<TaskMemoryBlock>(this, total_num_tasks);
  MemoryRequirementsPush<TaskPtr>(this, options.main_queue_size);
  MemoryRequirementsPush<AtomicTaskPtr>(this, total_num_tasks);
  MemoryRequirementsPush<TaskHandle>(this, total_num_tasks);
}

Job::InitializationToken Job::Initialize(const Job::JobSystemMemoryRequirements& memory_requirements, void* memory) noexcept
{
  JobAssert(g_JobSystem == nullptr, "Already initialized.");

  const bool needs_delete = memory == nullptr;

  if (!memory)
  {
    memory = ::operator new[](memory_requirements.byte_size, std::align_val_t{memory_requirements.alignment});
  }

  JobAssert(memory != nullptr, "memory must be a valid pointer.");
  JobAssert(IsPointerAligned(memory, memory_requirements.alignment), "memory must be a aligned to `memory_requirements.alignment`.");

  const JobSystemCreateOptions& options              = memory_requirements.options;
  const std::uint64_t           rng_seed             = options.job_steal_rng_seed;
  const WorkerID                num_threads          = config::WorkerCount(options);
  const WorkerID                owned_threads        = num_threads - options.num_user_threads;
  const std::uint16_t           num_tasks_per_worker = config::NumTasksPerWorker(options);
  const std::uint32_t           total_num_tasks      = config::TotalNumTasks(num_threads, num_tasks_per_worker);

  void*                  alloc_ptr        = memory;
  JobSystemContext*      job_system       = LinearAlloc<JobSystemContext>(alloc_ptr, 1u).ptr;
  Span<ThreadLocalState> all_workers      = LinearAlloc<ThreadLocalState>(alloc_ptr, num_threads);
  Span<TaskMemoryBlock>  all_tasks        = LinearAlloc<TaskMemoryBlock>(alloc_ptr, total_num_tasks);
  Span<TaskPtr>          main_tasks_ptrs  = LinearAlloc<TaskPtr>(alloc_ptr, options.main_queue_size);
  Span<AtomicTaskPtr>    worker_task_ptrs = LinearAlloc<AtomicTaskPtr>(alloc_ptr, total_num_tasks);
  Span<TaskHandle>       all_task_handles = LinearAlloc<TaskHandle>(alloc_ptr, total_num_tasks);

  job_system->main_queue.Initialize(SpanAlloc(&main_tasks_ptrs, options.main_queue_size), options.main_queue_size);
  job_system->workers           = all_workers.ptr;
  job_system->num_workers       = num_threads;
  job_system->num_owned_workers = owned_threads;
  job_system->num_user_threads_setup.store(0, std::memory_order_relaxed);
  job_system->num_tasks_per_worker = num_tasks_per_worker;
  job_system->sys_arch_str         = "Unknown Arch";
  job_system->num_available_jobs.store(0, std::memory_order_relaxed);
  job_system->needs_delete           = needs_delete;
  job_system->system_alloc_size      = memory_requirements.byte_size;
  job_system->system_alloc_alignment = memory_requirements.alignment;
  job_system->init_lock.num_workers_ready.store(1u, std::memory_order_relaxed);  // Main thread already initialized.

#if IS_WINDOWS
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);

  switch (sysinfo.wProcessorArchitecture)
  {
    case PROCESSOR_ARCHITECTURE_AMD64:
    {
      job_system->sys_arch_str = "x64 (Intel or AMD)";
      break;
    }
    case PROCESSOR_ARCHITECTURE_ARM:
    {
      job_system->sys_arch_str = "ARM";
      break;
    }
    case PROCESSOR_ARCHITECTURE_ARM64:
    {
      job_system->sys_arch_str = "ARM64";
      break;
    }
    case PROCESSOR_ARCHITECTURE_IA64:
    {
      job_system->sys_arch_str = "Intel Itanium-Based";
      break;
    }
    case PROCESSOR_ARCHITECTURE_INTEL:
    {
      job_system->sys_arch_str = "Intel x86";
      break;
    }
    case PROCESSOR_ARCHITECTURE_UNKNOWN:
    default:
    {
      job_system->sys_arch_str = "Unknown Arch";
      break;
    }
  }
#endif

  ThreadLocalState* const main_thread_worker = job_system->workers;

  for (std::uint64_t worker_index = 0; worker_index < num_threads; ++worker_index)
  {
    ThreadLocalState* const worker = SpanAlloc(&all_workers, 1u);

    worker->normal_queue.Initialize(SpanAlloc(&worker_task_ptrs, options.normal_queue_size), options.normal_queue_size);
    worker->worker_queue.Initialize(SpanAlloc(&worker_task_ptrs, options.worker_queue_size), options.worker_queue_size);
    task_pool::Initialize(&worker->task_allocator, SpanAlloc(&all_tasks, num_tasks_per_worker), num_tasks_per_worker);
    worker->allocated_tasks     = SpanAlloc(&all_task_handles, num_tasks_per_worker);
    worker->num_allocated_tasks = 0u;
    pcg32_srandom_r(&worker->rng_state, worker_index + rng_seed, worker_index * 2u + 1u + rng_seed);
    worker->last_stolen_worker = main_thread_worker;
  }

  g_JobSystem     = job_system;
  g_CurrentWorker = main_thread_worker;

  std::atomic_thread_fence(std::memory_order_release);
  for (std::uint64_t worker_index = 1; worker_index < owned_threads; ++worker_index)
  {
    worker::InitializeThread(job_system->workers + worker_index);
  }

  JobAssert(all_workers.num_elements == 0u, "All elements expected to be allocated out.");
  JobAssert(all_tasks.num_elements == 0u, "All elements expected to be allocated out.");
  JobAssert(main_tasks_ptrs.num_elements == 0u, "All elements expected to be allocated out.");
  JobAssert(worker_task_ptrs.num_elements == 0u, "All elements expected to be allocated out.");
  JobAssert(all_task_handles.num_elements == 0u, "All elements expected to be allocated out.");

  return Job::InitializationToken{owned_threads};
}

void Job::SetupUserThread()
{
  Job::JobSystemContext* const job_system     = g_JobSystem;
  const std::uint32_t          user_thread_id = job_system->num_owned_workers + job_system->num_user_threads_setup.fetch_add(1, std::memory_order_relaxed);

  JobAssert(user_thread_id < job_system->num_workers, "Too many calls to `SetupUserThread`.");

  worker::WorkerThreadSetup(job_system->workers + user_thread_id);
}

std::size_t Job::NumSystemThreads() noexcept
{
#if IS_SINGLE_THREADED
  return 1;
#else
  const auto n = std::thread::hardware_concurrency();
  return n != 0 ? n : 1;
#endif

#if 0

#if IS_WINDOWS
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#elif IS_POSIX
    return sysconf(_SC_NPROCESSORS_ONLN) /* * 2*/;
#elif 0  // FreeBSD, MacOS X, NetBSD, OpenBSD
    nt          mib[4];
    int         numCPU;
    std::size_t len = sizeof(numCPU);

    /* set the mib for hw.ncpu */
    mib[0] = CTL_HW;
    mib[1] = HW_AVAILCPU;  // alternatively, try HW_NCPU;

    /* get the number of CPUs from the system */
    sysctl(mib, 2, &numCPU, &len, NULL, 0);

    if (numCPU < 1)
    {
      mib[1] = HW_NCPU;
      sysctl(mib, 2, &numCPU, &len, NULL, 0);
      if (numCPU < 1)
        numCPU = 1;
    }

    return numCPU;
#elif 0  // HPUX
    return mpctl(MPC_GETNUMSPUS, NULL, NULL);
#elif 0  // IRIX
    return sysconf(_SC_NPROC_ONLN);
#elif 0  // Objective-C (Mac OS X >=10.5 or iOS)
    NSUInteger a = [[NSProcessInfo processInfo] processorCount];
    NSUInteger b = [[NSProcessInfo processInfo] activeProcessorCount];

    return a;
#endif

#endif
}

std::uint16_t Job::NumWorkers() noexcept
{
  return std::uint16_t(g_JobSystem->num_workers);
}

const char* Job::ProcessorArchitectureName() noexcept
{
  return g_JobSystem->sys_arch_str;
}

WorkerID Job::CurrentWorker() noexcept
{
  JobAssert(g_CurrentWorker != nullptr, "This thread was not created by the job system.");
  return WorkerID(g_CurrentWorker - g_JobSystem->workers);
}

bool Job::IsMainThread() noexcept
{
  return worker::IsMainThread(g_CurrentWorker);
}

void Job::Shutdown() noexcept
{
  JobAssert(g_JobSystem != nullptr, "Cannot shutdown when never initialized.");

  static_assert(std::is_trivially_destructible_v<TaskMemoryBlock>, "TaskMemoryBlock's destructor not called.");
  static_assert(std::is_trivially_destructible_v<TaskPtr>, "TaskPtr's destructor not called.");
  static_assert(std::is_trivially_destructible_v<AtomicTaskPtr>, "AtomicTaskPtr's destructor not called.");
  static_assert(std::is_trivially_destructible_v<TaskHandle>, "TaskHandle's destructor not called.");

  JobSystemContext* const job_system  = g_JobSystem;
  const std::uint32_t     num_workers = job_system->num_owned_workers;

  job_system->is_running.store(false, std::memory_order_relaxed);

  // Allow one last update loop to allow them to end.
  system::WakeUpAllWorkers();

  for (std::uint32_t i = 0; i < num_workers; ++i)
  {
    ThreadLocalState* const worker = job_system->workers + i;

    if (i != 0)
    {
      worker::ShutdownThread(worker);
    }

    worker->~ThreadLocalState();
  }

  const bool needs_delete = job_system->needs_delete;

  job_system->~JobSystemContext();
  g_CurrentWorker = nullptr;
  g_JobSystem     = nullptr;

  if (needs_delete)
  {
    ::operator delete[](job_system, job_system->system_alloc_size, std::align_val_t{job_system->system_alloc_alignment});
  }
}

Task* Job::TaskMake(const TaskFn function, Task* const parent) noexcept
{
  const WorkerID          worker_id            = worker::GetCurrentID();
  ThreadLocalState* const worker               = system::GetWorker(worker_id);
  const std::uint32_t     max_tasks_per_worker = g_JobSystem->num_tasks_per_worker;

  if (worker->num_allocated_tasks == max_tasks_per_worker)
  {
    worker::GarbageCollectAllocatedTasks(worker);

    if (worker->num_allocated_tasks == max_tasks_per_worker)
    {
      // While we cannot allocate do some work.
      system::WakeUpAllWorkers();
      while (worker->num_allocated_tasks == max_tasks_per_worker)
      {
        worker::TryRunTask(worker);
        worker::GarbageCollectAllocatedTasks(worker);
      }
    }
  }

  JobAssert(worker->num_allocated_tasks < max_tasks_per_worker, "Too many tasks allocated.");

  Task* const      task     = task_pool::AllocateTask(&worker->task_allocator, worker_id, function, task::PointerToTaskPtr(parent));
  const TaskHandle task_hdl = task_pool::TaskToIndex(worker->task_allocator, task);

  if (parent)
  {
    parent->num_unfinished_tasks.fetch_add(1u, std::memory_order_release);
  }

  worker->allocated_tasks[worker->num_allocated_tasks++] = task_hdl;

  return task;
}

TaskData Job::TaskGetData(Task* const task, const std::size_t alignment) noexcept
{
  Byte* const       user_storage_start = static_cast<Byte*>(AlignPointer(task->user_data + task->user_data_start, alignment));
  const Byte* const user_storage_end   = std::end(task->user_data);

  if (user_storage_start <= user_storage_end)
  {
    const std::size_t user_storage_size = user_storage_end - user_storage_start;

    return {user_storage_start, user_storage_size};
  }

  return {nullptr, 0u};
}

void Job::TaskAddContinuation(Task* const self, Task* const continuation, const QueueType queue) noexcept
{
  JobAssert(self->q_type == k_InvalidQueueType, "The parent task should not have already been submitted to a queue.");
  JobAssert(continuation->q_type == k_InvalidQueueType, "A continuation must not have already been submitted to a queue or already added as a continuation.");
  JobAssert(continuation->next_continuation.isNull(), "A continuation must not have already been added to another task.");

  const TaskPtr new_head          = task::PointerToTaskPtr(continuation);
  continuation->q_type            = queue;
  continuation->next_continuation = self->first_continuation.load(std::memory_order_relaxed);

  while (!std::atomic_compare_exchange_strong(&self->first_continuation, &continuation->next_continuation, new_head))
  {
  }
}

void Job::TaskIncRef(Task* const task) noexcept
{
  const auto old_ref_count = task->ref_count.fetch_add(1, std::memory_order_relaxed);

  JobAssert(old_ref_count >= std::int16_t(1) || task->q_type == k_InvalidQueueType, "First call to taskIncRef should not happen after the task has been submitted.");
  (void)old_ref_count;
}

void Job::TaskDecRef(Task* const task) noexcept
{
  const auto old_ref_count = task->ref_count.fetch_sub(1, std::memory_order_relaxed);

  JobAssert(old_ref_count >= 0, "taskDecRef: Called too many times.");
  (void)old_ref_count;
}

bool Job::TaskIsDone(const Task* const task) noexcept
{
  return task->num_unfinished_tasks.load(std::memory_order_acquire) == -1;
}

void Job::TaskSubmit(Task* const self, QueueType queue) noexcept
{
  JobAssert(self->q_type == k_InvalidQueueType, "A task cannot be submitted to a queue multiple times.");

  const WorkerID num_workers = NumWorkers();

  // If we only have one thread running using the worker queue is invalid.
  if (num_workers == 1u && queue == QueueType::WORKER)
  {
    queue = QueueType::NORMAL;
  }

  ThreadLocalState* const worker   = worker::GetCurrent();
  const TaskPtr           task_ptr = task::PointerToTaskPtr(self);

  self->q_type = queue;

  switch (queue)
  {
    case QueueType::NORMAL:
    {
      task::SubmitQPushHelper(task_ptr, worker, &worker->normal_queue);
      break;
    }
    case QueueType::MAIN:
    {
      LockedQueue<TaskPtr>* const main_queue = &g_JobSystem->main_queue;

      // NOTE(SR):
      //   The only way `main_queue` will be emptied
      //   is by the main thread, so there is a chance
      //   that if it does not get flushed frequently
      //   enough then we have a this thread spinning indefinitely.
      //
      while (!main_queue->Push(task_ptr))
      {
        // If we could not push to the queue then just do some work.
        worker::TryRunTask(worker);
      }
      break;
    }
    case QueueType::WORKER:
    {
      task::SubmitQPushHelper(task_ptr, worker, &worker->worker_queue);
      break;
    }
    default:
#if defined(__GNUC__)  // GCC, Clang, ICC
      __builtin_unreachable();
#elif defined(_MSC_VER)  // MSVC
      __assume(false);
#endif
      break;
  }

  if (queue != QueueType::MAIN)
  {
    const std::int32_t num_pending_jobs = g_JobSystem->num_available_jobs.fetch_add(1, std::memory_order_relaxed);

    if (num_pending_jobs >= num_workers)
    {
      system::WakeUpAllWorkers();
    }
    else
    {
      system::WakeUpOneWorker();
    }
  }
}

void Job::WaitOnTask(const Task* const task) noexcept
{
  const WorkerID worker_id = CurrentWorker();

  JobAssert(task->q_type != k_InvalidQueueType, "The Task must be submitted to a queue before you wait on it.");
  JobAssert(task->owning_worker == worker_id, "You may only call this function with a task created on the current 'Worker'.");

  system::WakeUpAllWorkers();

  ThreadLocalState* const worker = system::GetWorker(worker_id);

  while (!TaskIsDone(task))
  {
    worker::TryRunTask(worker);
  }
}

void Job::TaskSubmitAndWait(Task* const self, const QueueType queue) noexcept
{
  TaskSubmit(self, queue);
  WaitOnTask(self);
}

// Member Fn Definitions

Task::Task(WorkerID worker, TaskFn fn, TaskPtr parent) noexcept :
  fn_storage{fn},
  num_unfinished_tasks{1},
  ref_count{1},
  parent{parent},
  first_continuation{nullptr},
  next_continuation{nullptr},
  owning_worker{worker},
  q_type{k_InvalidQueueType}, /* Set to a valid value in 'Job::submitTask' */
  user_data_start{0},
  user_data{}
{
}

#if defined(_MSC_VER)
#define NativePause YieldProcessor
#elif defined(__clang__) && defined(__SSE__) || defined(__INTEL_COMPILER)  // || defined(__GNUC_PREREQ) && (__GNUC_PREREQ(4, 7) && defined(__SSE__))
#include <xmmintrin.h>
#define NativePause _mm_pause
#elif defined(__arm__)
#ifdef __CC_ARM
#define NativePause __yield
#else
#define NativePause() __asm__ __volatile__("yield")
#endif
#else
#define NativePause std::this_thread::yield
#endif

void Job::PauseProcessor() noexcept
{
  NativePause();
}

#undef NativePause

void Job::YieldTimeSlice() noexcept
{
  // Windows : SwitchToThread()
  // Linux   : sched_yield()
  std::this_thread::yield();
}

// Private Helpers

QueueType Job::detail::taskQType(const Task* const task) noexcept
{
  return task->q_type;
}

void* Job::detail::taskGetPrivateUserData(Task* const task, const std::size_t alignment) noexcept
{
  return AlignPointer(task->user_data, alignment);
}

void* Job::detail::taskReservePrivateUserData(Task* const task, const std::size_t num_bytes, const std::size_t alignment) noexcept
{
  const Byte* const user_storage_end        = std::end(task->user_data);
  Byte* const       requested_storage_start = static_cast<Byte*>(AlignPointer(task->user_data, alignment));
  const Byte* const requested_storage_end   = requested_storage_start + num_bytes;

  JobAssert(requested_storage_end <= user_storage_end, "Cannot store object within the task's user storage. ");

  task->user_data_start = static_cast<std::uint8_t>(requested_storage_end - task->user_data);

  return requested_storage_start;
}

bool Job::detail::mainQueueTryRunTask(void) noexcept
{
  JobAssert(worker::IsMainThread(worker::GetCurrent()), "Must only be called by main thread.");

  TaskPtr task_ptr;
  if (g_JobSystem->main_queue.Pop(&task_ptr))
  {
    Task* const task = task::TaskPtrToPointer(task_ptr);
    task::RunTaskFunction(task);
    return true;
  }

  return false;
}

#undef IS_WINDOWS
#undef IS_POSIX
#undef IS_SINGLE_THREADED

#if defined(_MSC_VER)

#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4146)

#endif

#include "pcg_basic.c"

#if defined(_MSC_VER)

#pragma warning(pop)

#endif

/******************************************************************************/
/*
  MIT License

  Copyright (c) 2020-2024 Shareef Abdoul-Raheem

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/
/******************************************************************************/
