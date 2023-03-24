/******************************************************************************/
/*!
 * @file   bf_job_system.cpp
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
 * @copyright Copyright (c) 2020-2022 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#include "bf/job/bf_job_api.hpp"

#include "bf_job_queue.hpp" /* JobQueueA */

#include "pcg_basic.h" /* pcg_state_setseq_64, pcg32_srandom_r, pcg32_boundedrand_r */

#include <algorithm> /* partition, for_each, distance                                                   */
#include <array>     /* array                                                                           */
#include <cstdio>    /* fprintf, stderr                                                                 */
#include <cstdlib>   /* abort                                                                           */
#include <limits>    /* numeric_limits                                                                  */
#include <new>       /* hardware_constructive_interference_size, hardware_destructive_interference_size */
#include <thread>    /* thread                                                                          */

#if _WIN32
#define IS_WINDOWS 1
#define IS_POSIX 0
#define IS_SINGLE_THREADED 0
#elif __APPLE__
#define IS_WINDOWS 0
#define IS_POSIX 1
#define IS_SINGLE_THREADED 0
#elif (__ANDROID__ || __linux || __unix || __posix)
#define IS_WINDOWS 0
#define IS_POSIX 1
#define IS_SINGLE_THREADED 0
#elif __EMSCRIPTEN__
#define IS_WINDOWS 0
#define IS_POSIX 0
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

#if JOB_SYS_ASSERTIONS
namespace
{
  static inline void jobAssertHandler(const bool condition, const char* const filename, const int line_number, const char* const msg)
  {
    if (!condition)
    {
      std::fprintf(stderr, "JobSystem [%s:%i] Assertion '%s' Failed.\n", filename, line_number, msg);
      std::abort();
    }
  }
}  // namespace

#define JobAssert(expr, msg) ::jobAssertHandler((expr), __FILE__, __LINE__, msg)
#else
#define JobAssert(expr, msg)
#endif

namespace bf
{
  namespace job
  {
    // Constants

#ifdef __cpp_lib_hardware_interference_size
    static constexpr std::size_t k_CachelineSize = std::max(std::hardware_constructive_interference_size, std::hardware_destructive_interference_size);
#else
    static constexpr std::size_t k_CachelineSize = 64u;
#endif

    static constexpr std::size_t k_ExpectedTaskSize  = std::max(std::size_t(128u), k_CachelineSize);
    static constexpr std::size_t k_MaxTasksPerWorker = k_NormalQueueSize + k_BackgroundQueueSize;
    static constexpr WorkerID    k_MainThreadID      = 0;
    static constexpr QueueType   k_InvalidQueueType  = QueueType(int(QueueType::WORKER) + 1);

    // Fwd Declarations

    struct ThreadWorker;

    // Type Aliases

    using TaskHandle           = std::uint16_t;
    using TaskHandleType       = TaskHandle;
    using AtomicTaskHandleType = std::atomic<TaskHandle>;
    using WorkerIDType         = WorkerID;

    static constexpr TaskHandle NullTaskHandle = std::numeric_limits<TaskHandle>::max();

    // Struct Definitions

    struct TaskPtr
    {
      WorkerID   worker_id;
      TaskHandle task_index;

      TaskPtr() = default;

      TaskPtr(WorkerID worker_id, TaskHandle task_idx) :
        worker_id{worker_id},
        task_index{task_idx}
      {
      }

      TaskPtr(std::nullptr_t) :
        worker_id{NullTaskHandle},
        task_index{NullTaskHandle}
      {
      }

      bool isNull() const noexcept { return task_index == NullTaskHandle; }
    };
    using AtomicTaskPtr = std::atomic<TaskPtr>;

    static_assert(sizeof(TaskPtr) == 4u, "Expected to be the size of two uint16's.");
    static_assert(sizeof(AtomicTaskPtr) == sizeof(TaskPtr), "Expected to be lockfree so no extra data members should have been added.");

    /*!
     * @brief
     *   A single 'job' to be run by this system.
     */
    struct Task
    {
      static constexpr std::size_t k_SizeOfMembers =
       sizeof(TaskFn) +
       sizeof(AtomicInt32) +
       sizeof(AtomicInt16) +
       sizeof(std::uint8_t) +
       sizeof(QueueType) +
       sizeof(TaskPtr) +
       sizeof(AtomicTaskPtr) +
       sizeof(TaskPtr) +
       sizeof(WorkerID);

      static constexpr std::size_t k_TaskPaddingDataSize = k_ExpectedTaskSize - k_SizeOfMembers;

      using Padding = std::array<std::uint8_t, k_TaskPaddingDataSize>;

      TaskFn        fn;                    //!< The function that will be run.
      AtomicInt32   num_unfinished_tasks;  //!< The number of children tasks.
      AtomicInt16   ref_count;             //!< Keeps the task from being garbage collected.
      std::uint8_t  user_storage_size;     //!< Number of bytes available to the user in `padding`.
      QueueType     q_type;                //!< The queue type this task has been submitted to, initalized to k_InvalidQueueType.
      TaskPtr       parent;                //!< The parent task, can be null.
      AtomicTaskPtr first_continuation;    //!< Head of linked list of tasks to be added on completion.
      TaskPtr       next_continuation;     //!< Next element in the linked list of continuations.
      WorkerID      owning_worker;         //!< The worker this task has been created on, needed for `Task::toTaskPtr` and various assertions.
      Padding       padding;               //!< User data storage.

      Task(WorkerID worker, TaskFn fn, TaskPtr parent);

      void run()
      {
        fn(this);
        onFinish();
      }

      void    onFinish() noexcept;
      TaskPtr toTaskPtr() const noexcept;
    };

    static_assert(sizeof(Task) == k_ExpectedTaskSize, "The task struct is expected to be this size.");
    static_assert(std::is_trivially_destructible_v<Task>, "Task must be trivially destructible.");

    template<std::size_t k_MaxTask>
    struct TaskPool
    {
      static_assert(k_MaxTask > 0u, "Must store at least one task in this pool.");

      static constexpr std::size_t k_MaxTaskMinusOne = k_MaxTask - 1u;

      union TaskMemoryBlock
      {
        TaskMemoryBlock*                                    next;
        std::aligned_storage_t<sizeof(Task), alignof(Task)> storage;
      };

      TaskMemoryBlock  memory[k_MaxTask];
      TaskMemoryBlock* current_block;

      TaskPool()
      {
        for (std::size_t i = 0u; i < k_MaxTaskMinusOne; ++i)
        {
          memory[i].next = &memory[i + 1u];
        }
        memory[k_MaxTaskMinusOne].next = nullptr;

        current_block = &memory[0];
      }

      Task* allocate(WorkerID worker, TaskFn fn, TaskPtr parent)
      {
        TaskMemoryBlock* const result = std::exchange(current_block, current_block->next);

        JobAssert(result != nullptr, "Allocation failure.");

        return new (result) Task(worker, fn, parent);
      }

      std::size_t indexOf(const Task* const task) const
      {
        const TaskMemoryBlock* const block = reinterpret_cast<const TaskMemoryBlock*>(task);

        JobAssert(block >= memory && block < (memory + k_MaxTask), "Invalid task pointer passed in.");

        return block - memory;
      }

      Task* fromIndex(const std::size_t idx)
      {
        JobAssert(idx < k_MaxTask, "Invalid index.");

        return reinterpret_cast<Task*>(&memory[idx].storage);
      }

      void deallocate(Task* const task)
      {
        task->~Task();

        TaskMemoryBlock* const block = reinterpret_cast<TaskMemoryBlock*>(task);

        block->next = std::exchange(current_block, block);
      }
    };

    struct ThreadWorker
    {
      using SharedQueue     = JobQueueA<k_NormalQueueSize, TaskPtr>;
      using WorkerQueue     = JobQueueA<k_BackgroundQueueSize, TaskPtr>;
      using TaskAllocator   = TaskPool<k_MaxTasksPerWorker>;
      using ThreadStorage   = std::aligned_storage_t<sizeof(std::thread), alignof(std::thread)>;
      using TaskHandleArray = std::array<TaskHandle, k_MaxTasksPerWorker>;

      SharedQueue          hi_queue            = {};
      WorkerQueue          bg_queue            = {};
      TaskAllocator        task_memory         = {};
      ThreadStorage        thread              = {};
      TaskHandleArray      allocated_tasks     = {};
      pcg_state_setseq_64  rng_state           = {};
      AtomicTaskHandleType num_allocated_tasks = 0u;

      ThreadWorker() = default;

      ThreadWorker(const ThreadWorker& rhs)            = delete;
      ThreadWorker(ThreadWorker&& rhs)                 = delete;
      ThreadWorker& operator=(const ThreadWorker& rhs) = delete;
      ThreadWorker& operator=(ThreadWorker&& rhs)      = delete;

      std::thread* worker() { return reinterpret_cast<std::thread*>(&thread); }

      void     start(const WorkerID worker_index, const std::uint64_t rng_seed);
      void     garbageCollectAllocatedTasks();
      bool     run(const WorkerID worker_id);
      void     stop();
      WorkerID randomWorker() noexcept;
    };

    struct JobSystem
    {
      using WorkerThreadStorage = std::array<char, k_MaxThreadsSupported * sizeof(ThreadWorker)>;

      JobQueueM<k_MainQueueSize, TaskPtr> main_queue                           = {};
      std::uint32_t                       num_workers                          = 0u;
      const char*                         sys_arch_str                         = "Unknown Arch";
      std::mutex                          worker_sleep_mutex                   = {};
      std::condition_variable             worker_sleep_cv                      = {};
      WorkerThreadStorage                 worker_storage alignas(ThreadWorker) = {};
      AtomicInt32                         num_available_jobs                   = ATOMIC_VAR_INIT(0u);
      std::atomic_bool                    is_running                           = ATOMIC_VAR_INIT(false);

      ThreadWorker* workers() { return reinterpret_cast<ThreadWorker*>(worker_storage.data()); }
      void          sleep(const ThreadWorker* worker);
      void          wakeUpOneWorker() { worker_sleep_cv.notify_one(); }
      void          wakeUpAllWorkers() { worker_sleep_cv.notify_all(); }
    };

    static_assert(k_MaxTasksPerWorker < NullTaskHandle, "Either request less Tasks or change 'TaskHandle' to a larger type.");

    // System Globals

    namespace
    {
      JobSystem                 s_JobCtx               = {};
      AtomicInt32               s_NextThreadLocalIndex = 0;
      thread_local std::int32_t s_ThreadLocalIndex     = 0x7FFFFFFF;
      std::atomic_bool          s_IsFullyInitialized   = ATOMIC_VAR_INIT(false);  //!< We need to wait for all workers to be initialized before we start doing work.

    }  // namespace

    // Helper Declarations

    static Task* taskPtrToPointer(TaskPtr ptr) noexcept;
    static void  initThreadWorkerID() noexcept;

    void detail::checkTaskDataSize(const Task* task, std::size_t data_size) noexcept
    {
      JobAssert(data_size <= task->user_storage_size, "Attempting to store an object too large to fit within a task's storage buffer.");
      (void)task;
      (void)data_size;
    }

    QueueType detail::taskQType(const Task* const task) noexcept
    {
      return task->q_type;
    }

    void* detail::taskPaddingStart(Task* const task) noexcept
    {
      return task->padding.data();
    }

    void detail::taskUsePadding(Task* task, std::size_t num_bytes) noexcept
    {
      JobAssert(num_bytes <= task->user_storage_size, "Attempting to store an closure too large to fit within a task's storage buffer.");
      task->user_storage_size -= static_cast<std::uint8_t>(num_bytes);
    }

    bool detail::mainQueueRunTask(void) noexcept
    {
      const TaskPtr task_ptr = s_JobCtx.main_queue.pop();

      if (!task_ptr.isNull())
      {
        Task* const task = taskPtrToPointer(task_ptr);
        task->run();

        return true;
      }

      // If no work in the main queue atleast try to do some general work.

      const WorkerID worker_id = currentWorker();

      ThreadWorker& worker = s_JobCtx.workers()[worker_id];
      worker.run(worker_id);

      return false;
    }

    static WorkerID clampThreadCount(WorkerID value, WorkerID min, WorkerID max)
    {
      return std::min(std::max(min, value), max);
    }

    std::size_t numSystemThreads() noexcept
    {
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
#elif IS_SINGLE_THREADED
      return 1;
#else
      const auto n = std::thread::hardware_concurrency();
      return (n) ? n : 8;
#endif
    }

    void initialize(const JobSystemCreateOptions& params) noexcept
    {
      JobAssert(s_NextThreadLocalIndex == 0u, "Job System must be shutdown before it can be initialized again.");

      const WorkerID num_threads = clampThreadCount(WorkerID(params.num_threads ? params.num_threads : numSystemThreads()), WorkerID(1), WorkerID(k_MaxThreadsSupported));

      s_JobCtx.num_workers = num_threads;

#if JOB_SYS_DETERMINISTIC_JOB_STEAL_RNG
      constexpr std::uint64_t random_seed = 0u;
#else
      const std::uint64_t random_seed = static_cast<std::uint64_t>(rand());
#endif

      s_JobCtx.is_running = true;

      for (WorkerID i = 0; i < num_threads; ++i)
      {
        ThreadWorker* const worker = new (s_JobCtx.workers() + i) ThreadWorker();

        worker->start(i, random_seed);
      }

      s_IsFullyInitialized = true;

#if IS_WINDOWS
      SYSTEM_INFO sysinfo;
      GetSystemInfo(&sysinfo);

      switch (sysinfo.wProcessorArchitecture)
      {
        case PROCESSOR_ARCHITECTURE_AMD64:
        {
          s_JobCtx.sys_arch_str = "x64 (Intel or AMD)";
          break;
        }
        case PROCESSOR_ARCHITECTURE_ARM:
        {
          s_JobCtx.sys_arch_str = "ARM";
          break;
        }
        case PROCESSOR_ARCHITECTURE_ARM64:
        {
          s_JobCtx.sys_arch_str = "ARM64";
          break;
        }
        case PROCESSOR_ARCHITECTURE_IA64:
        {
          s_JobCtx.sys_arch_str = "Intel Itanium-Based";
          break;
        }
        case PROCESSOR_ARCHITECTURE_INTEL:
        {
          s_JobCtx.sys_arch_str = "Intel x86";
          break;
        }
        case PROCESSOR_ARCHITECTURE_UNKNOWN:
        default:
        {
          s_JobCtx.sys_arch_str = "Unknown Arch";
          break;
        }
      }
#endif
    }

    std::uint32_t numWorkers() noexcept
    {
      return s_JobCtx.num_workers;
    }

    const char* processorArchitectureName() noexcept
    {
      return s_JobCtx.sys_arch_str;
    }

    WorkerID currentWorker() noexcept
    {
      JobAssert(WorkerID(s_ThreadLocalIndex) < numWorkers(), "This thread was not created by the job system.");
      return WorkerID(s_ThreadLocalIndex);
    }

    void shutdown() noexcept
    {
      JobAssert(s_NextThreadLocalIndex != 0u, "Job System must be initialized before it can be shutdown.");

      s_JobCtx.is_running = false;

      const std::uint32_t num_workers = s_JobCtx.num_workers;

      // Allow one last update loop to allow them to end.
      s_JobCtx.wakeUpAllWorkers();

      for (std::uint32_t i = 0; i < num_workers; ++i)
      {
        ThreadWorker* const worker = s_JobCtx.workers() + i;

        if (i != k_MainThreadID)
        {
          worker->stop();
        }

        worker->~ThreadWorker();
      }

      s_NextThreadLocalIndex = 0u;
      s_IsFullyInitialized   = false;
    }

    Task* taskMake(TaskFn function, Task* const parent) noexcept
    {
      const WorkerID worker_id = currentWorker();

      ThreadWorker* const worker = s_JobCtx.workers() + worker_id;

      if (worker->num_allocated_tasks == k_MaxTasksPerWorker)
      {
        s_JobCtx.wakeUpAllWorkers();

        // While we cannot allocate do some work.
        worker->garbageCollectAllocatedTasks();
        while (worker->num_allocated_tasks == k_MaxTasksPerWorker)
        {
          worker->run(worker_id);
          worker->garbageCollectAllocatedTasks();
        }
      }

      JobAssert(worker->num_allocated_tasks < k_MaxTasksPerWorker, "Too many tasks allocated.");

      Task* const      task     = worker->task_memory.allocate(worker_id, function, parent ? parent->toTaskPtr() : TaskPtr{NullTaskHandle, NullTaskHandle});
      const TaskHandle task_hdl = TaskHandle(worker->task_memory.indexOf(task));

      if (parent)
      {
        parent->num_unfinished_tasks.fetch_add(1u);
      }

      worker->allocated_tasks[worker->num_allocated_tasks++] = task_hdl;
      ++task->ref_count;

      return task;
    }

    TaskData taskGetData(Task* const task) noexcept
    {
      const std::size_t user_data_start = sizeof(task->padding) - task->user_storage_size;
      return {task->padding.data() + user_data_start, task->user_storage_size};
    }

    void taskAddContinuation(Task* const self, Task* const continuation) noexcept
    {
      JobAssert(self->q_type == k_InvalidQueueType, "The task should not have already been submitted to a queue.");
      JobAssert(continuation->q_type == k_InvalidQueueType, "A continuation must not have already been submitted to a queue.");
      JobAssert(continuation->next_continuation.isNull(), "A continuation must not have already been added to another task.");

      const TaskPtr new_head = continuation->toTaskPtr();
      TaskPtr       expected_head;
      do
      {
        expected_head                   = self->first_continuation.load();
        continuation->next_continuation = expected_head;

      } while (!std::atomic_compare_exchange_weak(&self->first_continuation, &expected_head, new_head));
    }

    template<typename QueueType>
    static void taskSubmitQPushHelper(Task* const self, const WorkerID worker_id, QueueType& queue)
    {
      ThreadWorker* const worker   = s_JobCtx.workers() + worker_id;
      const TaskPtr       task_ptr = self->toTaskPtr();

      // Loop until we have successfully pushed to the queue.
      while (!queue.push(task_ptr))
      {
        // If we could not push to the queues then just do some work.
        worker->run(worker_id);
      }
    }

    Task* taskSubmit(Task* const self, QueueType queue) noexcept
    {
      const WorkerID num_workers = numWorkers();
      const WorkerID worker_id   = currentWorker();

      // If we only have one thread running using the working queue is invalid.
      if (num_workers == 1u && queue == QueueType::WORKER)
      {
        queue = QueueType::NORMAL;
      }

      JobAssert(self->q_type == k_InvalidQueueType, "A task cannot be submitted to a queue multiple times.");

      ThreadWorker* const worker = s_JobCtx.workers() + worker_id;

      self->q_type = queue;

      switch (queue)
      {
        case QueueType::NORMAL:
        {
          taskSubmitQPushHelper(self, worker_id, worker->hi_queue);
          break;
        }
        case QueueType::MAIN:
        {
          taskSubmitQPushHelper(self, worker_id, s_JobCtx.main_queue);
          break;
        }
        case QueueType::WORKER:
        {
          taskSubmitQPushHelper(self, worker_id, worker->bg_queue);
          break;
        }
      }

      if (queue != QueueType::MAIN)
      {
        const std::int32_t num_pending_jobs = s_JobCtx.num_available_jobs.fetch_add(1);

        if (num_pending_jobs >= num_workers)
        {
          s_JobCtx.wakeUpAllWorkers();
        }
        else
        {
          s_JobCtx.wakeUpOneWorker();
        }
      }

      return self;
    }

    void taskIncRef(Task* const task)
    {
      const auto old_ref_count = task->ref_count.fetch_add(1, std::memory_order_relaxed);

      JobAssert(old_ref_count >= 1u || task->q_type == k_InvalidQueueType, "First call to taskIncRef should not happen after the task has been submitted.");
    }

    void taskDecRef(Task* const task)
    {
      const auto old_ref_count = task->ref_count.fetch_sub(1, std::memory_order_relaxed);

      JobAssert(old_ref_count >= 0, "taskDecRef: Called too many times.");
      (void)old_ref_count;
    }

    bool taskIsDone(const Task* const task) noexcept
    {
      return task->num_unfinished_tasks.load() == -1;
    }

    void workerGC()
    {
      const WorkerID worker_id = currentWorker();

      ThreadWorker& worker = s_JobCtx.workers()[worker_id];

      worker.garbageCollectAllocatedTasks();
    }

    void waitOnTask(const Task* const task) noexcept
    {
      const WorkerID worker_id = currentWorker();

      JobAssert(task->q_type != k_InvalidQueueType, "The Task must be submitted to a queue before you wait on it.");
      JobAssert(task->owning_worker == worker_id, "You may only call this function with a task created on the current 'Worker'.");

      s_JobCtx.wakeUpAllWorkers();

      ThreadWorker& worker = s_JobCtx.workers()[worker_id];

      while (!taskIsDone(task))
      {
        worker.run(worker_id);
      }
    }

    void taskSubmitAndWait(Task* const self, const QueueType queue) noexcept
    {
      waitOnTask(taskSubmit(self, queue));
    }

    // Member Fn Definitions

    void JobSystem::sleep(const ThreadWorker* const worker)
    {
      if (is_running)
      {
        std::this_thread::yield();

        if (num_available_jobs.load(std::memory_order_relaxed) == 0u)
        {
          std::unique_lock<std::mutex> lock(worker_sleep_mutex);
          worker_sleep_cv.wait(lock, [this]() {
            // NOTE(SR):
            //   Because the stl wants 'false' to mean continue waiting the logic is a bit confusing :/
            //
            //   Returns false if the waiting should be continued, aka num_queued_jobs == 0u (also return true if not running).
            //
            //        Wait If:     running AND num_available_jobs == 0.
            // Do Not Wait If: not running  OR num_available_jobs != 0.
            //
            return !is_running || num_available_jobs.load(std::memory_order_relaxed) != 0;
          });
        }
      }
    }

    Task::Task(WorkerID worker, TaskFn fn, TaskPtr parent) :
      fn{fn},
      num_unfinished_tasks{1},
      ref_count{0u},
      user_storage_size{Task::k_TaskPaddingDataSize},
      q_type{k_InvalidQueueType}, /* Set to a valid value in 'submitTask' */
      parent{parent},
      first_continuation{nullptr},
      next_continuation{nullptr},
      owning_worker{worker},
      padding{}
    {
    }

    TaskPtr Task::toTaskPtr() const noexcept
    {
      const TaskHandle self_index = TaskHandle(s_JobCtx.workers()[owning_worker].task_memory.indexOf(this));

      return {owning_worker, self_index};
    }

    void Task::onFinish() noexcept
    {
      // NOTE(SR):
      //   make sure to store the result in a local variable and use that for
      //   comparing against zero. otherwise, the code contains a data race
      //   because other child tasks could change `num_unfinished_tasks` in the meantime.
      const std::int32_t num_jobs_left = --num_unfinished_tasks;

      if (num_jobs_left == 0)
      {
        if (!parent.isNull())
        {
          Task* const parent_task = taskPtrToPointer(parent);

          parent_task->onFinish();
        }

        --num_unfinished_tasks;

        TaskPtr continuation = first_continuation.load();

        while (!continuation.isNull())
        {
          Task* const   task              = taskPtrToPointer(continuation);
          const TaskPtr next_continuation = task->next_continuation;

          taskSubmit(task, q_type);

          continuation = next_continuation;
        }

        --ref_count;
      }
    }

    void ThreadWorker::start(const WorkerID worker_index, const std::uint64_t rng_seed)
    {
      const bool is_main_thread = worker_index == k_MainThreadID;

      pcg32_srandom_r(&rng_state, std::uint64_t(worker_index) + rng_seed, std::uint64_t(worker_index) * 2u + 1u + rng_seed);

      if (is_main_thread)
      {
        initThreadWorkerID();
      }
      else
      {
        new (worker()) std::thread([]() {
          initThreadWorkerID();

          const WorkerID      thread_id = currentWorker();
          ThreadWorker* const worker    = s_JobCtx.workers() + thread_id;

#if IS_WINDOWS
          const HANDLE handle = static_cast<HANDLE>(worker->worker()->native_handle());

          // Put each thread on to dedicated core

          const DWORD_PTR affinity_mask   = 1ull << thread_id;
          const DWORD_PTR affinity_result = SetThreadAffinityMask(handle, affinity_mask);

          if (affinity_result > 0)
          {
            // Increase thread priority

            // const BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_HIGHEST);
            // JobAssert(priority_result != 0, "Failed to set thread priority.");

            // Name the thread

            char      thread_name[32]                    = u8"";
            wchar_t   thread_name_w[sizeof(thread_name)] = L"";
            const int c_size                             = std::snprintf(thread_name, sizeof(thread_name), "bf::JobSystem_%i", int(thread_id));

            std::mbstowcs(thread_name_w, thread_name, c_size);

            const HRESULT hr = SetThreadDescription(handle, thread_name_w);
            JobAssert(SUCCEEDED(hr), "Failed to set thread name.");
            (void)hr;
          }
#endif

          while (!s_IsFullyInitialized)
          {
            /* Busy Loop Wait so there is no checking of this condition in the loop itself */
          }

          while (s_JobCtx.is_running)
          {
            if (!worker->run(thread_id))
            {
              s_JobCtx.sleep(worker);
            }
          }
        });
      }
    }

    void ThreadWorker::garbageCollectAllocatedTasks()
    {
      if (num_allocated_tasks)
      {
        const auto allocated_tasks_bgn = allocated_tasks.begin();

#if 0
        const auto allocated_tasks_end = allocated_tasks_bgn + num_allocated_tasks;
        
        const auto done_end = std::partition(
         allocated_tasks_bgn,
         allocated_tasks_end,
         [this](const TaskHandle task_hdl) {
           const bool task_is_finished = finished_tasks[task_hdl];

           if (task_is_finished)
           {
             task_memory.deallocate(task_memory.fromIndex(task_hdl));
           }

           return !task_is_finished;
         });

        num_allocated_tasks = TaskHandle(std::distance(allocated_tasks_bgn, done_end));
#else
        const TaskHandleType num_tasks = num_allocated_tasks;
        TaskHandleType       read_idx  = 0u;
        TaskHandleType       write_idx = 0u;

        while (read_idx != num_tasks)
        {
          const TaskHandle task_hdl         = allocated_tasks_bgn[read_idx];
          Task* const      task_ptr         = task_memory.fromIndex(task_hdl);
          const bool       task_is_finished = task_ptr->ref_count.load(std::memory_order_relaxed) == 0u;

          if (task_is_finished)
          {
            task_memory.deallocate(task_ptr);
          }
          else
          {
            allocated_tasks_bgn[write_idx++] = task_hdl;
          }

          ++read_idx;
        }

        num_allocated_tasks = write_idx;
#endif
      }
    }

    bool ThreadWorker::run(const WorkerID worker_id)
    {
      const bool is_main_thread = worker_id == k_MainThreadID;

      TaskPtr task_ptr = hi_queue.pop();

      if (task_ptr.isNull())
      {
        task_ptr = is_main_thread ? s_JobCtx.main_queue.pop() : bg_queue.pop();
      }

      if (task_ptr.isNull())
      {
        const WorkerID other_worker_id = randomWorker();

        if (other_worker_id != worker_id)
        {
          ThreadWorker* const other_worker = s_JobCtx.workers() + other_worker_id;

          task_ptr = other_worker->hi_queue.steal();

          if (task_ptr.isNull() && !is_main_thread)
          {
            task_ptr = other_worker->bg_queue.steal();
          }
        }
      }

      if (task_ptr.isNull())
      {
        return false;
      }

      s_JobCtx.num_available_jobs.fetch_sub(1);

      Task* const task = taskPtrToPointer(task_ptr);
      task->run();

      return true;
    }

    void ThreadWorker::stop()
    {
      // Join throws an exception if the thread is not joinable. this should always be true.
      worker()->join();
      worker()->~thread();
    }

    WorkerID ThreadWorker::randomWorker() noexcept
    {
      const std::uint32_t num_workers = s_JobCtx.num_workers;

      std::uint32_t weights[k_MaxThreadsSupported];
      std::uint32_t weight_sum = 0u;

      for (WorkerID i = 0u; i < num_workers; ++i)
      {
        const ThreadWorker& worker = s_JobCtx.workers()[i];

        weights[i] = worker.num_allocated_tasks;
        weight_sum += weights[i];
      }

      if (weight_sum != 0)
      {
        std::uint32_t random_number = pcg32_boundedrand_r(&rng_state, weight_sum);

        for (WorkerID i = 0u; i < num_workers; ++i)
        {
          if (random_number < weights[i])
          {
            return i;
          }

          random_number -= weights[i];
        }
      }

      return WorkerID(pcg32_boundedrand_r(&rng_state, num_workers));
    }

    // Helper Definitions

    static Task* taskPtrToPointer(TaskPtr ptr) noexcept
    {
      if (!ptr.isNull())
      {
        ThreadWorker* const worker = s_JobCtx.workers() + ptr.worker_id;
        Task* const         result = worker->task_memory.fromIndex(ptr.task_index);

        JobAssert(ptr.worker_id == result->owning_worker, "Corrupted worker ID.");

        return result;
      }

      return nullptr;
    }

    static void initThreadWorkerID() noexcept
    {
      s_ThreadLocalIndex = s_NextThreadLocalIndex++;

      JobAssert(s_ThreadLocalIndex < s_NextThreadLocalIndex, "Something went very wrong if this is not true.");
    }
  }  // namespace job
}  // namespace bf

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

  Copyright (c) 2020-2022 Shareef Abdoul-Raheem

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
