/******************************************************************************/
/*!
 * @file   job_api.cpp
 * @author Shareef Raheem (https://blufedora.github.io/)
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
 * @copyright Copyright (c) 2020-2026 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#include "concurrent/job_api.hpp"

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

namespace job
{
  // Constants

#ifdef __cpp_lib_hardware_interference_size
  static constexpr std::size_t k_CachelineSize = std::max(std::hardware_constructive_interference_size, std::hardware_destructive_interference_size);
#else
  static constexpr std::size_t k_CachelineSize = 64u;
#endif

  static constexpr std::size_t k_ExpectedTaskSize = std::max(std::size_t(128u), k_CachelineSize);

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

    job::TaskPtr() noexcept = default;

    job::TaskPtr(WorkerID worker_id, TaskHandle task_idx) noexcept :
      worker_id{worker_id},
      task_index{task_idx}
    {
    }

    job::TaskPtr(std::nullptr_t) noexcept :
      worker_id{NullTaskHandle},
      task_index{NullTaskHandle}
    {
    }

    bool isNull() const noexcept { return task_index == NullTaskHandle; }
  };

  using AtomicTaskPtr = std::atomic<job::TaskPtr>;

  static_assert(sizeof(job::TaskPtr) == sizeof(std::uint16_t) * 2u, "Expected to be the size of two uint16's.");
  static_assert(sizeof(AtomicTaskPtr) == sizeof(job::TaskPtr) && AtomicTaskPtr::is_always_lock_free, "Expected to be lock-free so no extra data members should have been added.");

  struct alignas(k_CachelineSize) Task
  {
    static constexpr std::size_t k_SizeOfMembers =
     sizeof(const char*) +
     sizeof(internal::JobFn) +
     sizeof(Counter*) +
     sizeof(std::uint8_t) +
     sizeof(std::atomic_bool);

    static constexpr std::size_t k_TaskPaddingDataSize = k_ExpectedTaskSize - k_SizeOfMembers;

    const char*      name;                              //!< Debug name of this task.
    internal::JobFn  job_fn;                            //!< The function that will be run.
    Counter*         counter;                           //!< The counter to be decremented.
    std::uint8_t     userdata_align;                    //!< Alignment offset needed by userdata.
    std::atomic_bool is_ready_for_gc;                   //!< Set to true when the task can be reused.
    Byte             user_data[k_TaskPaddingDataSize];  //!< User data storage.

    Task(const char* const name, const job::internal::JobFn job_fn, job::Counter* const counter) noexcept :
      name{name},
      job_fn{job_fn},
      counter{counter},
      is_ready_for_gc{false},
      userdata_align{0},
      user_data{}
    {
      counter->unfinished_tasks.fetch_add(1u, std::memory_order_release);
    }
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
    SPMCDeque<job::TaskPtr> normal_queue;
    SPMCDeque<job::TaskPtr> worker_queue;
    TaskPool                task_allocator;
    TaskHandle*             allocated_tasks;
    TaskHandleType          num_allocated_tasks;
    ThreadLocalState*       last_stolen_worker;
    pcg_state_setseq_64     rng_state;
    std::thread             thread_id;
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

    job::ThreadLocalState* workers;
    std::uint32_t          num_workers;
    std::atomic_uint32_t   num_user_threads_setup;
    std::uint32_t          num_tasks_per_worker;
    bool                   needs_delete;
    std::atomic_bool       is_running;
    InitializationLock     init_lock;
    const char*            sys_arch_str;
    std::size_t            system_alloc_size;
    std::size_t            system_alloc_alignment;

    // Shared Mutable State

    std::mutex              worker_sleep_mutex;
    std::condition_variable worker_sleep_cv;
    std::atomic_size_t      num_available_jobs;
  };
}  // namespace Job

// System Globals

static job::JobSystemContext*              g_JobSystem     = nullptr;
static thread_local job::ThreadLocalState* g_CurrentWorker = nullptr;

// Internal API

void job::internal::PrivateCtx::ReleaseTaskToPool() const
{
  is_ready_for_gc->store(true, std::memory_order_release);
}

#if JOB_SYS_ASSERTIONS

void job::internal::AssertHandler(const bool condition, const char* const filename, const int line_number, const char* const msg)
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
      job::JobSystemContext* const job_system = g_JobSystem;

      if (job_system->is_running.load(std::memory_order_relaxed))
      {
        job::PauseProcessor();

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

    static job::ThreadLocalState* GetWorker(const job::WorkerID worker_id) noexcept
    {
      JobAssert(worker_id < job::NumWorkers(), "This thread was not created by the job system.");
      return g_JobSystem->workers + worker_id;
    }

  }  // namespace system

  namespace task_pool
  {
    static void Initialize(job::TaskPool* const pool, job::TaskMemoryBlock* const memory, const job::TaskHandleType capacity) noexcept
    {
      const job::TaskHandleType capacity_minus_one = capacity - 1;

      for (std::size_t i = 0u; i < capacity_minus_one; ++i)
      {
        memory[i].next = &memory[i + 1u];
      }
      memory[capacity_minus_one].next = nullptr;

      pool->memory   = memory;
      pool->freelist = &memory[0];
    }

    static job::TaskHandle TaskToIndex(const job::TaskPool& pool, const job::Task* const task) noexcept
    {
      const job::TaskMemoryBlock* const block = reinterpret_cast<const job::TaskMemoryBlock*>(task);

      return job::TaskHandle(block - pool.memory);
    }

    static job::Task* TaskFromIndex(const job::TaskPool& pool, const std::size_t idx) noexcept
    {
      return reinterpret_cast<job::Task*>(&pool.memory[idx]);
    }

    static job::Task* AllocateTask(job::TaskPool* const pool, const char* const name, const job::internal::JobFn job_fn, job::Counter* const counter) noexcept
    {
      job::TaskMemoryBlock* const result = std::exchange(pool->freelist, pool->freelist->next);

      JobAssert(result != nullptr, "Allocation failure.");

      return new (result) job::Task(name, job_fn, counter);
    }

    static void DeallocateTask(job::TaskPool* const pool, job::Task* const task) noexcept
    {
      task->~Task();

      job::TaskMemoryBlock* const block = new (task) job::TaskMemoryBlock();

      block->next = std::exchange(pool->freelist, block);
    }
  }  // namespace task_pool

  namespace task
  {
    static job::Task* TaskPtrToPointer(const job::TaskPtr ptr) noexcept
    {
      if (!ptr.isNull())
      {
        job::ThreadLocalState* const worker = system::GetWorker(ptr.worker_id);
        job::Task* const             result = task_pool::TaskFromIndex(worker->task_allocator, ptr.task_index);

        return result;
      }
      else
      {
        return nullptr;
      }
    }

    static void RunTaskFunction(job::Task* const self, const job::WorkerID worker_id) noexcept
    {
      const job::internal::PrivateCtx ctx{self->counter, worker_id, self->name, 0, self->user_data + self->userdata_align, &self->is_ready_for_gc};

      (self->job_fn)(ctx);

      if (ctx.task_counter != nullptr)
      {
        ctx.task_counter->unfinished_tasks.fetch_sub(1, std::memory_order_release);
      }
    }
  }  // namespace task

  namespace worker
  {
    static job::WorkerID GetCurrentID() noexcept
    {
      JobAssert(g_CurrentWorker != nullptr, "This thread was not created by the job system.");
      return job::WorkerID(g_CurrentWorker - g_JobSystem->workers);
    }

    static void GarbageCollectAllocatedTasks(job::ThreadLocalState* const worker) noexcept
    {
      job::TaskHandle* const allocated_tasks = worker->allocated_tasks;
      job::TaskPool&         task_pool       = worker->task_allocator;

#if 0
      const job::TaskHandleType num_tasks = worker->num_allocated_tasks;
      job::TaskHandleType       read_idx  = 0u;
      job::TaskHandleType       write_idx = 0u;

      while (read_idx != num_tasks)
      {
        const job::TaskHandle task_handle      = allocated_tasks[read_idx++];
        job::Task* const      task_ptr         = task_pool::TaskFromIndex(task_pool, task_handle);
        const bool            task_is_finished = task_ptr->is_ready_for_gc.load(std::memory_order_acquire);

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
#else
      constexpr job::TaskHandleType max_tasks_to_gc = 512u;

      job::TaskHandleType num_tasks = worker->num_allocated_tasks;
      job::TaskHandleType read_idx  = 0u;
      job::TaskHandleType num_gc    = 0u;

      while (read_idx < num_tasks && num_gc < max_tasks_to_gc)
      {
        const job::TaskHandle task_handle      = allocated_tasks[read_idx];
        job::Task* const      task_ptr         = task_pool::TaskFromIndex(task_pool, task_handle);
        const bool            task_is_finished = task_ptr->is_ready_for_gc.load(std::memory_order_acquire);

        if (task_is_finished)
        {
          allocated_tasks[read_idx] = allocated_tasks[--num_tasks];

          task_pool::DeallocateTask(&task_pool, task_ptr);
          ++num_gc;
        }
        else
        {
          ++read_idx;
        }
      }

      worker->num_allocated_tasks = num_tasks;
#endif
    }

    static job::ThreadLocalState* RandomWorker(job::ThreadLocalState* const worker) noexcept
    {
      const std::uint32_t num_workers     = g_JobSystem->num_workers;
      const std::uint32_t other_worker_id = pcg32_boundedrand_r(&worker->rng_state, num_workers);

      return system::GetWorker(job::WorkerID(other_worker_id));
    }

    static bool IsMainThread(const job::ThreadLocalState* const worker) noexcept
    {
      return worker == g_JobSystem->workers;
    }

    static bool TryRunTask(job::ThreadLocalState* const worker) noexcept
    {
      const bool is_main_thread = IsMainThread(worker);

      job::TaskPtr task_ptr = nullptr;
      worker->normal_queue.Pop(&task_ptr);

      if (task_ptr.isNull() && !is_main_thread)
      {
        worker->worker_queue.Pop(&task_ptr);
      }

      const auto TrySteal = [is_main_thread, worker](job::ThreadLocalState* const other_worker) -> job::TaskPtr {
        job::TaskPtr result = nullptr;

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
        job::ThreadLocalState* const random_worker = RandomWorker(worker);

        task_ptr = TrySteal(random_worker);

        if (task_ptr.isNull())
        {
          return false;
        }

        worker->last_stolen_worker = random_worker;
      }

      g_JobSystem->num_available_jobs.fetch_sub(1, std::memory_order_relaxed);

      job::Task* const task = task::TaskPtrToPointer(task_ptr);
      task::RunTaskFunction(task, worker::GetCurrentID());

      return true;
    }

    static void WaitForAllThreadsReady(job::JobSystemContext* const job_system) noexcept
    {
      job::InitializationLock* const init_lock = &job_system->init_lock;

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

    static job::JobSystemContext* WorkerThreadSetup(job::ThreadLocalState* const worker)
    {
      std::atomic_thread_fence(std::memory_order_acquire);

      job::JobSystemContext* const job_system = g_JobSystem;

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

      const int c_size = std::snprintf(thread_name, sizeof(thread_name), "job::Worker%u", thread_index);

      std::mbstowcs(thread_name_w, thread_name, c_size);

      const HRESULT hr = SetThreadDescription(handle, thread_name_w);
      JobAssert(SUCCEEDED(hr), "Failed to set thread name.");
      (void)hr;
#endif

      g_CurrentWorker = worker;

      WaitForAllThreadsReady(job_system);

      return job_system;
    }

    static void InitializeThread(job::ThreadLocalState* const worker) noexcept
    {
      worker->thread_id = std::thread([worker]() {
        job::JobSystemContext* const job_system = WorkerThreadSetup(worker);

        while (job_system->is_running.load(std::memory_order_relaxed))
        {
          if (!worker::TryRunTask(worker))
          {
            system::Sleep();
          }
        }
      });
    }

    static void ShutdownThread(job::ThreadLocalState* const worker) noexcept
    {
      // Join throws an exception if the thread is not joinable. this should always be true.
      worker->thread_id.join();
    }
  }  // namespace worker

  namespace task
  {
    static void SubmitQPushHelper(const job::TaskPtr task_ptr, job::ThreadLocalState* const worker, job::SPMCDeque<job::TaskPtr>* queue) noexcept
    {
      if (queue->Push(task_ptr) != job::SPMCDequeStatus::SUCCESS)
      {
        // Loop until we have successfully pushed to the queue.
        system::WakeUpAllWorkers();
        while (queue->Push(task_ptr) != job::SPMCDequeStatus::SUCCESS)
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

    span->ptr          += num_elements;
    span->num_elements -= num_elements;

    return result;
  }

  static std::size_t AlignedSizeUp(const std::size_t size, const std::size_t alignment) noexcept
  {
    const std::size_t remainder = size % alignment;

    return remainder != 0 ? size + (alignment - remainder) : size;
  }

  template<typename T>
  static void MemoryRequirementsPush(job::JobSystemMemoryRequirements* in_out_reqs, const std::size_t num_elements) noexcept
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
    static job::WorkerID WorkerCount(const job::JobSystemCreateOptions& options) noexcept
    {
      return (options.num_threads ? options.num_threads : job::WorkerID(job::NumSystemThreads()));
    }

    static std::uint16_t NumTasksPerWorker(const job::JobSystemCreateOptions& options) noexcept
    {
      const std::size_t num_tasks_per_worker = std::size_t(options.normal_queue_size) + std::size_t(options.worker_queue_size);

      JobAssert(num_tasks_per_worker <= std::uint16_t(-1), "Too many task items per worker.");

      return std::uint16_t(num_tasks_per_worker);
    }

    static std::uint32_t TotalNumTasks(const job::WorkerID num_threads, const std::uint16_t num_tasks_per_worker) noexcept
    {
      return num_tasks_per_worker * num_threads;
    }
  }  // namespace config

}  // namespace

// Public API

job::JobSystemMemoryRequirements::JobSystemMemoryRequirements(const JobSystemCreateOptions& options) noexcept :
  options{options},
  byte_size{0},
  alignment{0}
{
  JobAssert(IsPowerOf2(options.normal_queue_size), "Normal queue size must be a power of two.");
  JobAssert(IsPowerOf2(options.worker_queue_size), "Worker queue size must be a power of two.");

  const WorkerID      num_threads          = config::WorkerCount(options);
  const std::uint16_t num_tasks_per_worker = config::NumTasksPerWorker(options);
  const std::uint32_t total_num_tasks      = config::TotalNumTasks(num_threads, num_tasks_per_worker);

  MemoryRequirementsPush<JobSystemContext>(this, 1u);
  MemoryRequirementsPush<job::ThreadLocalState>(this, num_threads);
  MemoryRequirementsPush<TaskMemoryBlock>(this, total_num_tasks);
  MemoryRequirementsPush<AtomicTaskPtr>(this, total_num_tasks);
  MemoryRequirementsPush<TaskHandle>(this, total_num_tasks);
}

void job::Initialize(const job::JobSystemMemoryRequirements& memory_requirements, void* memory) noexcept
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
  const std::uint16_t           num_tasks_per_worker = config::NumTasksPerWorker(options);
  const std::uint32_t           total_num_tasks      = config::TotalNumTasks(num_threads, num_tasks_per_worker);

  void*                       alloc_ptr        = memory;
  JobSystemContext*           job_system       = LinearAlloc<JobSystemContext>(alloc_ptr, 1u).ptr;
  Span<job::ThreadLocalState> all_workers      = LinearAlloc<job::ThreadLocalState>(alloc_ptr, num_threads);
  Span<TaskMemoryBlock>       all_tasks        = LinearAlloc<TaskMemoryBlock>(alloc_ptr, total_num_tasks);
  Span<AtomicTaskPtr>         worker_task_ptrs = LinearAlloc<AtomicTaskPtr>(alloc_ptr, total_num_tasks);
  Span<TaskHandle>            all_task_handles = LinearAlloc<TaskHandle>(alloc_ptr, total_num_tasks);

  job_system->workers     = all_workers.ptr;
  job_system->num_workers = num_threads;
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

  job::ThreadLocalState* const main_thread_worker = job_system->workers;

  for (std::uint64_t worker_index = 0; worker_index < num_threads; ++worker_index)
  {
    job::ThreadLocalState* const worker = SpanAlloc(&all_workers, 1u);

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
  for (std::uint64_t worker_index = 1; worker_index < num_threads; ++worker_index)
  {
    worker::InitializeThread(job_system->workers + worker_index);
  }

  JobAssert(all_workers.num_elements == 0u, "All elements expected to be allocated out.");
  JobAssert(all_tasks.num_elements == 0u, "All elements expected to be allocated out.");
  JobAssert(worker_task_ptrs.num_elements == 0u, "All elements expected to be allocated out.");
  JobAssert(all_task_handles.num_elements == 0u, "All elements expected to be allocated out.");
}

std::size_t job::NumSystemThreads() noexcept
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

std::uint16_t job::NumWorkers() noexcept
{
  return std::uint16_t(g_JobSystem->num_workers);
}

const char* job::ProcessorArchitectureName() noexcept
{
  return g_JobSystem->sys_arch_str;
}

job::WorkerID job::CurrentWorker() noexcept
{
  JobAssert(g_CurrentWorker != nullptr, "This thread was not created by the job system.");
  return job::WorkerID(g_CurrentWorker - g_JobSystem->workers);
}

bool job::IsMainThread() noexcept
{
  return worker::IsMainThread(g_CurrentWorker);
}

void job::Shutdown() noexcept
{
  JobAssert(g_JobSystem != nullptr, "Cannot shutdown when never initialized.");

  static_assert(std::is_trivially_destructible_v<TaskMemoryBlock>, "TaskMemoryBlock's destructor not called.");
  static_assert(std::is_trivially_destructible_v<job::TaskPtr>, "job::TaskPtr's destructor not called.");
  static_assert(std::is_trivially_destructible_v<AtomicTaskPtr>, "AtomicTaskPtr's destructor not called.");
  static_assert(std::is_trivially_destructible_v<TaskHandle>, "TaskHandle's destructor not called.");

  JobSystemContext* const job_system  = g_JobSystem;
  const std::uint32_t     num_workers = job_system->num_workers;

  // Incase all threads are not initialized by the time shutdown is called.
  while (job_system->is_running.load(std::memory_order_relaxed) != true) {}

  {
    std::unique_lock<std::mutex> lock(job_system->worker_sleep_mutex);
    job_system->is_running.store(false, std::memory_order_relaxed);
  }

  // Allow one last update loop to allow them to end.
  system::WakeUpAllWorkers();

  for (std::uint32_t i = 0; i < num_workers; ++i)
  {
    job::ThreadLocalState* const worker = job_system->workers + i;

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

void job::WaitOn(const Counter& counter) noexcept
{
  const WorkerID worker_id = CurrentWorker();

  system::WakeUpAllWorkers();

  job::ThreadLocalState* const worker = system::GetWorker(worker_id);

  while (counter.unfinished_tasks.load(std::memory_order_acquire) != 0u)
  {
    worker::TryRunTask(worker);
  }
}

// Member Fn Definitions

void job::internal::DispatchImpl(const char* const name,
                                 Counter* const    counter,
                                 const QueueMode   queue,
                                 const JobFn       func,
                                 const std::size_t user_data_size,
                                 const std::size_t user_data_alignment,
                                 const void* const user_data,
                                 void (*InitUserData)(void* const user_data, const void* const in_user_data)) noexcept
{
  const WorkerID               worker_id            = worker::GetCurrentID();
  job::ThreadLocalState* const worker               = system::GetWorker(worker_id);
  const std::uint32_t          max_tasks_per_worker = g_JobSystem->num_tasks_per_worker;

  // Try to ensure some tasks are free to allocate.
  {
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
  }

  Task* const        task     = task_pool::AllocateTask(&worker->task_allocator, name, func, counter);
  const TaskHandle   task_hdl = task_pool::TaskToIndex(worker->task_allocator, task);
  const job::TaskPtr task_ptr = {worker_id, task_pool::TaskToIndex(worker->task_allocator, task)};

  // Copy user data
  {
    const Byte* const    user_data_end    = task->user_data + sizeof(task->user_data);
    Byte* const          aligned_ptr      = static_cast<Byte*>(AlignPointer(task->user_data, user_data_alignment));
    const Byte* const    aligned_ptr_end  = aligned_ptr + user_data_size;
    const std::ptrdiff_t alignment_offset = aligned_ptr - task->user_data;

    JobAssert(aligned_ptr_end <= user_data_end, "Userdata could not be stored in task.");
    JobAssert(alignment_offset <= std::uint8_t(-1), "Alignment delta too large.");

    InitUserData(aligned_ptr, user_data);
    task->userdata_align = static_cast<std::uint8_t>(alignment_offset);
  }

  worker->allocated_tasks[worker->num_allocated_tasks++] = task_hdl;

  const WorkerID num_workers = NumWorkers();

  // If we only have one thread running using the worker queue is invalid.
  switch ((num_workers == 1u) ? QueueMode::Default : queue)
  {
    case QueueMode::Default:
    {
      task::SubmitQPushHelper(task_ptr, worker, &worker->normal_queue);
      break;
    }
    case QueueMode::WorkerOnly:
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

  const std::size_t num_pending_jobs = g_JobSystem->num_available_jobs.fetch_add(1, std::memory_order_relaxed);

  if (num_pending_jobs >= num_workers)
  {
    system::WakeUpAllWorkers();
  }
  else
  {
    system::WakeUpOneWorker();
  }
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

void job::PauseProcessor() noexcept
{
  NativePause();
}

#undef NativePause

void job::YieldTimeSlice() noexcept
{
  // Windows : SwitchToThread()
  // Linux   : sched_yield()
  std::this_thread::yield();
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

  Copyright (c) 2020-2026 Shareef Abdoul-Raheem

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
