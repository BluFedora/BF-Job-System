/******************************************************************************/
/*!
 * @file   bf_job_system.cpp
 * @author Shareef Abdoul-Raheem (https://blufedora.github.io/)
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
 * @version 0.0.1
 * @date    2020-09-03
 *
 * @copyright Copyright (c) 2020-2021 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#include "bf/job/bf_job_api.hpp"

#include "bf_job_queue.hpp" /* JobQueueA */

#include "pcg_basic.h"

#include <algorithm> /* partition, for_each, distance */
#include <array>     /* array                         */
#include <cassert>   /* assert                        */
#include <limits>    /* numeric_limits                */
#include <thread>    /* thread                        */

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

namespace bf
{
  namespace job
  {
    // Constants

    // NOTE(Shareef):
    //   C++17 has: hardware_constructive_interference_size / std::hardware_destructive_interference_size
    //   In core i7 the line (block) sizes in L1, L2 and L3 are the same (64 bytes)
    static constexpr std::size_t k_ExpectedTaskSize  = 128;
    static constexpr std::size_t k_MaxTasksPerWorker = k_HiPriorityQueueSize + k_BackgroundPriorityQueueSize;
    static constexpr WorkerID    k_MainThreadID      = 0;
    static constexpr QueueType   k_InvalidQueueType  = QueueType(int(QueueType::BACKGROUND) + 1);

    // Fwd Declarations

    struct BaseTask;
    struct ThreadWorker;

    // Type Aliases

    using TaskHandle     = std::uint16_t;
    using TaskHandleType = TaskHandle;

    static constexpr TaskHandle NullTaskHandle = std::numeric_limits<TaskHandle>::max();

    using ContinuationsCountType       = std::int8_t;
    using ContinuationsCountAtomicType = std::atomic<ContinuationsCountType>;

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

      bool isNull() const
      {
        return task_index == NullTaskHandle;
      }
    };

    struct JobSystem
    {
      JobQueueM<k_MainQueueSize, TaskPtr> main_queue         = {};
      std::uint32_t                       num_workers        = 0u;
      const char*                         sys_arch_str       = "Unknown Arch";
      ThreadWorker*                       workers            = nullptr;
      std::mutex                          worker_sleep_mutex = {};
      std::condition_variable             worker_sleep_cv    = {};
      std::atomic<std::uint64_t>          num_queued_jobs    = {};

      void sleep(const ThreadWorker* worker);
      void wakeUpOneWorker() { worker_sleep_cv.notify_one(); }
      void wakeUpAllWorkers() { worker_sleep_cv.notify_all(); }
    };

    struct BaseTask
    {
      using ContinuationsArray = std::array<TaskPtr, k_MaxTaskContinuations>;

      TaskFn                       fn;                    //!< The function that will be run.
      AtomicInt32                  num_unfinished_tasks;  //!< The number of children tasks.
      WorkerID                     owning_worker;         //!< The worker this task has been created on, needed for `Task::toTaskPtr` and various assertions.
      TaskPtr                      parent;                //!< The parent task, can be null.
      ContinuationsArray           continuations;         //!< The list of tasks to be added on completion.
      ContinuationsCountAtomicType num_continuations;     //!< The number of tasks to add to the queue on completion.
      QueueType                    q_type;                //!< The queue type this task has been submitted to.
      std::atomic<std::uint32_t>   flags;                 //!< RESERVED, Check to see if this needs to

      BaseTask(WorkerID worker, TaskFn fn, TaskPtr parent);
    };

    static_assert(
     k_MaxTaskContinuations <= std::numeric_limits<ContinuationsCountType>::max(),
     "Either upgrade `ContinuationsCountType` to a larger type or lower `k_MaxTaskContinuations`.");

    static_assert(sizeof(BaseTask) <= k_ExpectedTaskSize, "The task struct is expected to be this less than this size.");

    static constexpr std::size_t k_TaskPaddingDataSize = k_ExpectedTaskSize - sizeof(BaseTask);

    /*!
     * @brief 
     *   An Opaque handle to a single 'Job'.
     */
    struct Task final : public BaseTask
    {
      using Padding = std::aligned_storage_t<k_TaskPaddingDataSize>;  // std::array<std::uint8_t, k_TaskPaddingDataSize>;

      using BaseTask::BaseTask;

      Padding padding /* = {} */;

      void run()
      {
        fn(this);
        onFinish();
      }

      void    onFinish();
      TaskPtr toTaskPtr() const;
    };

    static_assert(sizeof(Task) == k_ExpectedTaskSize, "The task struct is expected to be this size.");

    static_assert(std::is_trivially_destructible_v<Task>, "Task must be trivially destructible.");

    template<std::size_t k_MaxTask>
    struct TaskPool
    {
      static_assert(k_MaxTask > 0u, "Must store at least one task in this pool.");

      union TaskMemoryBlock
      {
        TaskMemoryBlock*                                    next;
        std::aligned_storage_t<sizeof(Task), alignof(Task)> storage;
      };

      TaskMemoryBlock  memory[k_MaxTask];
      TaskMemoryBlock* current_block;

      TaskPool()
      {
        const std::size_t num_tasks_minus_one = k_MaxTask - 1;

        for (std::size_t i = 0u; i < num_tasks_minus_one; ++i)
        {
          memory[i].next = &memory[i + 1u];
        }
        memory[num_tasks_minus_one].next = nullptr;

        current_block = &memory[0];
      }

      Task* allocate(WorkerID worker, TaskFn fn, TaskPtr parent)
      {
        TaskMemoryBlock* const result = current_block;

        if (current_block)
        {
          current_block = current_block->next;

          new (result) Task(worker, fn, parent);
        }

        return reinterpret_cast<Task*>(result);
      }

      std::size_t indexOf(const Task* task) const
      {
        const TaskMemoryBlock* const block = reinterpret_cast<const TaskMemoryBlock*>(task);

        assert(block >= memory && block < (memory + k_MaxTask) && "Invalid task pointer passed in.");

        return block - memory;
      }

      Task* fromIndex(std::size_t idx)
      {
        assert(idx < k_MaxTask && "Invalid index.");

        return reinterpret_cast<Task*>(&memory[idx].storage);
      }

      void deallocate(Task* task)
      {
        task->~Task();

        TaskMemoryBlock* const block = reinterpret_cast<TaskMemoryBlock*>(task);

        block->next   = current_block;
        current_block = block;
      }
    };

    struct ThreadWorker
    {
      using ThreadStorage = std::aligned_storage_t<sizeof(std::thread), alignof(std::thread)>;

      JobQueueA<k_HiPriorityQueueSize, TaskPtr>         hi_queue            = {};
      JobQueueA<k_BackgroundPriorityQueueSize, TaskPtr> bg_queue            = {};
      TaskPool<k_MaxTasksPerWorker>                     task_memory         = {};
      std::array<TaskHandle, k_MaxTasksPerWorker>       allocated_tasks     = {};
      ThreadStorage                                     worker_             = {};
      TaskHandleType                                    num_allocated_tasks = 0u;
      std::atomic_bool                                  is_running          = ATOMIC_VAR_INIT(false);
      pcg_state_setseq_64                               rng_state           = {};

      ThreadWorker() = default;

      ThreadWorker(const ThreadWorker& rhs) = delete;
      ThreadWorker(ThreadWorker&& rhs)      = delete;
      ThreadWorker& operator=(const ThreadWorker& rhs) = delete;
      ThreadWorker& operator=(ThreadWorker&& rhs) = delete;

      std::thread* worker()
      {
        return reinterpret_cast<std::thread*>(&worker_);
      }

      void        start(bool is_main_thread);
      static void startImpl();
      void        garbageCollectAllocatedTasks(WorkerID thread_id);
      bool        run(WorkerID thread_id);
      static void yieldTimeSlice();
      void        stop();

      inline WorkerID randomWorker() noexcept;
    };

    static_assert(k_MaxTasksPerWorker < NullTaskHandle, "Either request less Tasks or change 'TaskHandle' to a larger type.");

    // System Globals

    namespace
    {
      using WorkerThreadStorage = std::array<char, k_MaxThreadsSupported * sizeof(ThreadWorker)>;

      JobSystem                 s_JobCtx                                   = {};
      AtomicInt32               s_NextThreadLocalIndex                     = 0;
      thread_local std::int32_t s_ThreadLocalIndex                         = 0x7FFFFFFF;
      WorkerThreadStorage       s_ThreadWorkerMemory alignas(ThreadWorker) = {};
      std::atomic_bool          s_IsFullyInitialized                       = ATOMIC_VAR_INIT(false);  //!< We need to wait for all workers to be initialized before we start doing work.

    }  // namespace

    // Helper Declarations

    static Task* taskPtrToPointer(TaskPtr ptr) noexcept;
    static bool  isTaskPtrNull(TaskPtr ptr) noexcept;
    static void  initThreadWorkerID() noexcept;
    static bool  taskIsDone(const Task* task) noexcept;

    void detail::checkTaskDataSize(std::size_t data_size) noexcept
    {
      assert(data_size <= k_TaskPaddingDataSize && "Attempting to store an object too large for this task.");
      (void)data_size;
    }

    QueueType detail::taskQType(const Task* task) noexcept
    {
      return task->q_type;
    }

    static WorkerID clampThreadCount(WorkerID value, WorkerID min, WorkerID max)
    {
      return std::min(std::max(min, value), max);
    }

    bool initialize(const JobSystemCreateOptions& params) noexcept
    {
      const WorkerID num_threads = clampThreadCount(WorkerID(params.num_threads ? params.num_threads : numSystemThreads()), WorkerID(1), WorkerID(k_MaxThreadsSupported));

      s_JobCtx.num_workers = num_threads;
      s_JobCtx.workers     = reinterpret_cast<ThreadWorker*>(s_ThreadWorkerMemory.data());

      std::atomic_init(&s_JobCtx.num_queued_jobs, 0u);

      const unsigned int random_seed = static_cast<unsigned int>(rand());

      for (std::size_t i = 0; i < num_threads; ++i)
      {
        ThreadWorker* const worker = new (s_JobCtx.workers + i) ThreadWorker();

        pcg32_srandom_r(&worker->rng_state, std::uint64_t(i), std::uint64_t(i) * 2u + 1u);

        worker->start(i == k_MainThreadID);
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

      return true;
    }

    std::size_t numWorkers() noexcept
    {
      return s_JobCtx.num_workers;
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

    const char* processorArchitectureName() noexcept
    {
      return s_JobCtx.sys_arch_str;
    }

    WorkerID currentWorker() noexcept
    {
      return WorkerID(s_ThreadLocalIndex);
    }

    void tick()
    {
      s_JobCtx.workers[k_MainThreadID].garbageCollectAllocatedTasks(k_MainThreadID);

      // Run any tasks from the special 'Main' queue.

      while (true)
      {
        Task* const task = taskPtrToPointer(s_JobCtx.main_queue.pop());

        if (!task)
        {
          break;
        }

        task->run();
      }
    }

    void shutdown() noexcept
    {
      const auto num_workers = s_JobCtx.num_workers;

      // Makes sure the sleeping threads do not continue to sleep.
      for (std::size_t i = 1; i < num_workers; ++i)
      {
        s_JobCtx.workers[i].is_running = false;
      }

      // Allow one last update loop to allow them to end.
      s_JobCtx.wakeUpAllWorkers();

      for (std::size_t i = 1; i < num_workers; ++i)
      {
        ThreadWorker* const worker = s_JobCtx.workers + i;

        worker->stop();
        worker->~ThreadWorker();
      }
    }

    Task* taskMake(TaskFn function, Task* parent) noexcept
    {
      const WorkerID worker_id = currentWorker();

      assert(worker_id < numWorkers() && "This thread was not created by the job system.");

      ThreadWorker* const worker = s_JobCtx.workers + worker_id;

      // While we cannot allocate do some work.
      while (!(worker->num_allocated_tasks < k_MaxTasksPerWorker))
      {
        worker->garbageCollectAllocatedTasks(worker_id);
        worker->run(worker_id);
      }

      Task* const      task     = worker->task_memory.allocate(worker_id, function, parent ? parent->toTaskPtr() : TaskPtr{NullTaskHandle, NullTaskHandle});
      const TaskHandle task_hdl = TaskHandle(worker->task_memory.indexOf(task));

      assert(worker->num_allocated_tasks < k_MaxTasksPerWorker);

      worker->allocated_tasks[worker->num_allocated_tasks++] = task_hdl;

      return task;
    }

    TaskData taskGetData(Task* task) noexcept
    {
      return {&task->padding, sizeof(task->padding)};
    }

    void taskAddContinuation(Task* self, const Task* continuation) noexcept
    {
      assert(continuation->q_type == k_InvalidQueueType && "A continuation must not have already been submitted to a queue.");
      assert(self->num_continuations < k_MaxTaskContinuations && "Too many continuations for a single task.");

      const ContinuationsCountType idx = self->num_continuations++;

      self->continuations[std::size_t(idx)] = continuation->toTaskPtr();
    }

    template<typename QueueType>
    static void taskSubmitQPushHelper(Task* const self, const WorkerID worker_id, QueueType& queue)
    {
      ThreadWorker* const worker   = s_JobCtx.workers + worker_id;
      const TaskPtr       task_ptr = self->toTaskPtr();

      // Loop until we have successfully pushed to the queue.
      while (!queue.push(task_ptr))
      {
        // If we could not push to the queues then just do some work.
        worker->run(worker_id);
      }
    }

    void taskSubmit(Task* self, QueueType queue) noexcept
    {
      const WorkerID worker_id = currentWorker();

      assert(self->q_type == k_InvalidQueueType && "A task cannot be submitted to a queue multiple times.");
      assert(worker_id < numWorkers() && "This thread was not created by the job system.");

      ThreadWorker* const worker = s_JobCtx.workers + worker_id;

      self->q_type = queue;

      switch (queue)
      {
        case QueueType::MAIN:
        {
          taskSubmitQPushHelper(self, worker_id, s_JobCtx.main_queue);
          break;
        }
        case QueueType::HIGH:
        {
          taskSubmitQPushHelper(self, worker_id, worker->hi_queue);
          break;
        }
        case QueueType::BACKGROUND:
        {
          taskSubmitQPushHelper(self, worker_id, worker->bg_queue);
          break;
        }
      }

      ++s_JobCtx.num_queued_jobs;
      s_JobCtx.wakeUpOneWorker();
    }

    void waitOnTask(const Task* task) noexcept
    {
      assert(task->q_type != k_InvalidQueueType && "The Task must be submitted to a queue before you wait on it.");

      const WorkerID worker_id = currentWorker();

      assert(worker_id < numWorkers() && "This thread was not created by the job system.");
      assert(task->owning_worker == worker_id && "You may only call this function with a task created on the current 'Worker'.");

      ThreadWorker* worker = s_JobCtx.workers + worker_id;

      s_JobCtx.wakeUpAllWorkers();

      while (!taskIsDone(task))
      {
        worker->run(worker_id);
      }
    }

    // Member Fn Definitions

    void JobSystem::sleep(const ThreadWorker* worker)
    {
      if (worker->is_running && num_queued_jobs == 0u)
      {
        std::unique_lock<std::mutex> lock(worker_sleep_mutex);

        worker->yieldTimeSlice();

        if (num_queued_jobs.load(std::memory_order_relaxed) != 0u)
        {
          return;
        }

        worker_sleep_cv.wait(lock, [worker, this]() {
          // NOTE(SR):
          //   Because the stl want 'false' to mean continue waiting the logic is a bit confusing :/
          //
          //   Returns false if the waiting should be continued, aka num_queued_jobs == 0u (also return true if not running).
          //
          //        Wait If: Worker is    running AND num_queued_jobs == 0u.
          // Do Not Wait If: Worker is not running OR num_queued_jobs != 0u.
          //
          return !worker->is_running || num_queued_jobs != 0u;
        });
      }
    }

    BaseTask::BaseTask(WorkerID worker, TaskFn fn, TaskPtr parent) :
      fn{fn},
      num_unfinished_tasks{1},
      owning_worker{worker},
      parent{parent},
      continuations{},
      num_continuations{0u},
      q_type{k_InvalidQueueType}, /* Set to a valid value in 'bf::job::submitTask' */
      flags{0x0}
    {
      if (!isTaskPtrNull(parent))
      {
        Task* const parent_ptr = taskPtrToPointer(parent);

        ++parent_ptr->num_unfinished_tasks;

        /*
        std::atomic_fetch_add_explicit(
          &parent_ptr->num_unfinished_tasks,
          1,
          // NOTE(Shareef): There are no surrounding memory operations around 'num_unfinished_tasks'
          std::memory_order_relaxed
        );
        */
      }
    }

    TaskPtr Task::toTaskPtr() const
    {
      const TaskHandle self_index = TaskHandle(s_JobCtx.workers[owning_worker].task_memory.indexOf(this));

      return {owning_worker, self_index};
    }

    void Task::onFinish()
    {
      // IMPORTANT(SR):
      //   make sure to store the result in a local variable and use that for
      //   comparing against zero later. otherwise, the code contains a data race
      //   because other child tasks could change m_UnFinishedJobs in the meantime.
      const std::int32_t num_jobs_left = --num_unfinished_tasks;

      if (num_jobs_left == 0)
      {
        if (!isTaskPtrNull(parent))
        {
          Task* const parent_task = taskPtrToPointer(parent);

          parent_task->onFinish();
        }

        --num_unfinished_tasks;

        const ContinuationsCountType num_continuations_local = num_continuations;

        for (std::int32_t i = 0; i < num_continuations_local; ++i)
        {
          taskSubmit(taskPtrToPointer(continuations[i]), q_type);
        }
      }
    }

    void ThreadWorker::start(bool is_main_thread)
    {
      is_running = true;

      if (is_main_thread)
      {
        initThreadWorkerID();
      }
      else
      {
#if defined(DEBUG)
        std::memset(allocated_tasks.data(), 0xFF, sizeof(allocated_tasks));
#endif
        new (worker()) std::thread([]() {
          startImpl();

          const WorkerID      thread_id = currentWorker();
          ThreadWorker* const self      = s_JobCtx.workers + thread_id;

          while (!s_IsFullyInitialized)
          {
            /* Busy Loop Wait so there is no checking of this condition in the loop itself */
          }

          while (self->is_running)
          {
            self->garbageCollectAllocatedTasks(thread_id);

            if (!self->run(thread_id))
            {
              s_JobCtx.sleep(self);
            }
          }
        });
      }
    }

    void ThreadWorker::startImpl()
    {
      initThreadWorkerID();

      const WorkerID      thread_id = currentWorker();
      ThreadWorker* const self      = s_JobCtx.workers + thread_id;

#ifdef IS_WINDOWS
      const WorkerID wid    = currentWorker();
      const HANDLE   handle = static_cast<HANDLE>(self->worker()->native_handle());

      // Put each thread on to dedicated core

      const DWORD_PTR affinity_mask   = 1ull << wid;
      const DWORD_PTR affinity_result = SetThreadAffinityMask(handle, affinity_mask);

      if (affinity_result > 0)
      {
        // Increase thread priority

        const BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_HIGHEST);

        assert(priority_result != 0);

        // Name the thread

        if (priority_result)
        {
          char      thread_name[20]   = u8"";
          wchar_t   thread_name_w[20] = L"";
          const int c_size            = std::snprintf(thread_name, sizeof(thread_name), "bf::JobSystem_%i", int(wid));

          std::mbstowcs(thread_name_w, thread_name, c_size);

          const HRESULT hr = SetThreadDescription(handle, thread_name_w);

          (void)hr;
          assert(SUCCEEDED(hr));
        }
      }
#endif  // _WIN32
    }

    void ThreadWorker::garbageCollectAllocatedTasks(const WorkerID thread_id)
    {
      if (num_allocated_tasks)
      {
        const auto allocated_tasks_bgn = allocated_tasks.begin();
        const auto allocated_tasks_end = allocated_tasks_bgn + num_allocated_tasks;

        // NOTE(SR):
        //   Cannot use `remove_if` since it moves the removed elements,
        //   but we want swaps so that we can safely read the removed
        //   elements from done_end => allocated_tasks_end. (aka that next for_each loop)
        const auto done_end = std::partition(
         allocated_tasks_bgn,
         allocated_tasks_end,
         [thread_id](TaskHandle task_hdl) {
           Task* const task = taskPtrToPointer({thread_id, task_hdl});

           return !taskIsDone(task);
         });

        std::for_each(
         done_end,
         allocated_tasks_end,
         [&task_memory = task_memory, thread_id](TaskHandle task_hdl) {
           Task* const task = taskPtrToPointer({thread_id, task_hdl});

           task_memory.deallocate(task);
         });

        num_allocated_tasks = TaskHandle(std::distance(allocated_tasks_bgn, done_end));
      }
    }

    bool ThreadWorker::run(const WorkerID thread_id)
    {
      Task* task = nullptr;

      // If is the Main Thread then prioritize grabbing from the main queue.
      if (thread_id == k_MainThreadID)
      {
        task = taskPtrToPointer(s_JobCtx.main_queue.pop());
      }

      const auto other_worker_id = randomWorker();

#define TryGetFromQ(q_name)                                                              \
  if (!task)                                                                             \
  {                                                                                      \
    task = taskPtrToPointer(q_name.pop());                                               \
                                                                                         \
    if (!task && other_worker_id != thread_id)                                           \
    {                                                                                    \
      ThreadWorker* const other_worker = s_JobCtx.workers + other_worker_id;             \
      task                             = taskPtrToPointer(other_worker->q_name.steal()); \
    }                                                                                    \
  }

      // clang-format off
      TryGetFromQ(hi_queue)

       // The main thread should not be running tasks with background priority.
      if (thread_id != k_MainThreadID)
      {
        TryGetFromQ(bg_queue)
      }

      // clang-format on

#undef TryGetFromQ

      if (task)
      {
        --s_JobCtx.num_queued_jobs;
        task->run();
      }

      return task != nullptr;
    }

    void ThreadWorker::yieldTimeSlice()
    {
      std::this_thread::yield();
      // std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    void ThreadWorker::stop()
    {
      // Joins throws an exception if the thread is not joinable. this should always be true.
      worker()->join();
      worker()->~thread();
    }

    inline WorkerID ThreadWorker::randomWorker() noexcept
    {
      return WorkerID(pcg32_boundedrand_r(&rng_state, s_JobCtx.num_workers));
    }

    // Helper Definitions

    static Task* taskPtrToPointer(TaskPtr ptr) noexcept
    {
      if (ptr.isNull())
      {
        return nullptr;
      }

      ThreadWorker* const worker = s_JobCtx.workers + ptr.worker_id;
      Task* const         result = static_cast<Task*>(worker->task_memory.fromIndex(ptr.task_index));

      assert(ptr.worker_id == result->owning_worker);

      return result;
    }

    bool isTaskPtrNull(TaskPtr ptr) noexcept
    {
      return ptr.task_index == NullTaskHandle;
    }

    static void initThreadWorkerID() noexcept
    {
      s_ThreadLocalIndex = s_NextThreadLocalIndex++;

      assert(s_ThreadLocalIndex < s_NextThreadLocalIndex && "Something went very wrong if this is not true.");
    }

    bool taskIsDone(const Task* task) noexcept
    {
      return task->num_unfinished_tasks.load() == -1;
    }
  }  // namespace job
}  // namespace bf

#if defined(_MSC_VER)

#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4146)

#endif

#include "pcg_basic.c"

#if defined(_MSC_VER)

#pragma warning(pop)

#endif

#undef IS_WINDOWS
#undef IS_POSIX
#undef IS_SINGLE_THREADED

/******************************************************************************/
/*
  MIT License

  Copyright (c) 2020-2021 Shareef Abdoul-Raheem

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
