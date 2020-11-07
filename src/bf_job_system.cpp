/******************************************************************************/
/*!
 * @file   bf_job_api.cpp
 * @author Shareef Abdoul-Raheem (http://blufedora.github.io/)
 * @brief
 *    API for a multithreading job system.
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
 * @copyright Copyright (c) 2020 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#include "bf/job/bf_job_api.hpp"

#include "bf/PoolAllocator.hpp" /* PoolAllocator */

#include "bf_job_queue.hpp" /* JobQueueA */

#include <algorithm> /* partition, for_each, distance                   */
#include <array>     /* array                                           */
#include <cassert>   /* assert                                          */
#include <limits>    /* numeric_limits                                  */
#include <random>    /* default_random_engine, uniform_int_distribution */
#include <thread>    /* thread                                          */

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
    static constexpr std::size_t k_MaxTasksPerWorker = k_HiPriorityQueueSize + k_NormalPriorityQueueSize + k_BackgroundPriorityQueueSize;
    static constexpr WorkerID    k_MainThreadID      = 0;
    static constexpr QueueType   k_InvalidQueueType  = QueueType(int(QueueType::BACKGROUND) + 1);

    // Fwd Decls

    struct BaseTask;
    struct ThreadWorker;

    // Type Aliases

    using TaskHandle                           = std::uint16_t;
    using TaskHandleType                       = TaskHandle;
    static constexpr TaskHandle NullTaskHandle = std::numeric_limits<TaskHandle>::max();

    // Struct Definitions

    struct TaskPtr
    {
      WorkerID   worker_id;
      TaskHandle task_index;
    };

    struct JobSystem
    {
      std::default_random_engine              rand_engine        = {};
      std::uniform_int_distribution<WorkerID> rand_range         = {};
      std::size_t                             num_workers        = 0u;
      JobQueueM<k_MainQueueSize, Task*>       main_queue         = {};
      const char*                             sys_arch_str       = "Unknown Arch";
      ThreadWorker*                           workers            = nullptr;
      std::condition_variable                 worker_sleep_cv    = {};
      std::mutex                              worker_sleep_mutex = {};

      void sleep(ThreadWorker* worker);
      void wakeUpOneWorker() { worker_sleep_cv.notify_one(); }
      void wakeUpAllWorkers() { worker_sleep_cv.notify_all(); }
    };

    struct BaseTask
    {
      TaskFn      fn;
      AtomicInt32 num_unfinished_tasks;
      AtomicInt32 num_continuations;
      TaskPtr     parent;
      WorkerID    owning_worker;
      QueueType   q_type;
      TaskPtr     continuations[k_MaxTaskContinuations];

      BaseTask(WorkerID worker, TaskFn fn, TaskPtr parent);
    };

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

    struct ThreadWorker
    {
      using ThreadMemory = std::aligned_storage_t<sizeof(std::thread), alignof(std::thread)>;

      // TODO(SR): The Queues memory footprint can be reduced by replacing the `Task*` => `TaskHandle` but complicates the code.

      JobQueueA<k_HiPriorityQueueSize, Task*>         hi_queue            = {};
      JobQueueA<k_NormalPriorityQueueSize, Task*>     nr_queue            = {};
      JobQueueA<k_BackgroundPriorityQueueSize, Task*> bg_queue            = {};
      PoolAllocator<Task, k_MaxTasksPerWorker>        task_memory         = {};
      std::array<TaskHandle, k_MaxTasksPerWorker>     allocated_tasks     = {};
      ThreadMemory                                    worker_             = {};
      TaskHandleType                                  num_allocated_tasks = 0u;
      bool                                            is_running          = false;

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
      void        garbageCollectAllocatedTasks(const WorkerID thread_id);
      bool        run(const WorkerID thread_id);
      void        yieldTimeSlice();
      void        stop();
    };

    static_assert(k_MaxTasksPerWorker < NullTaskHandle, "Either request less Tasks or change 'TaskHandle' to a larger type.");

    // System Globals

    namespace
    {
      using WorkerThreadStorage = std::array<char, k_MaxThreadsSupported * sizeof(ThreadWorker)>;

      static JobSystem                 s_JobCtx                                   = {};
      static AtomicInt32               s_NextThreadLocalIndex                     = 0;
      static thread_local std::int32_t s_ThreadLocalIndex                         = 0x7FFFFFFF;
      static WorkerThreadStorage       s_ThreadWorkerMemory alignas(ThreadWorker) = {};
    }  // namespace

    // Helper Declarations

    static Task*    taskPtrToPointer(TaskPtr ptr) noexcept;
    static bool     isTaskPtrNull(TaskPtr ptr) noexcept;
    static WorkerID randomWorker() noexcept;
    static void     initThreadWorkerID() noexcept;
    static bool     taskIsDone(const Task* task) noexcept;

    void detail::checkTaskDataSize(std::size_t data_size) noexcept
    {
      assert(data_size <= k_TaskPaddingDataSize && "Attempting to store an object too large for this task.");
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

      s_JobCtx.rand_range  = std::uniform_int_distribution<WorkerID>{0u, WorkerID(num_threads - 1)};
      s_JobCtx.num_workers = num_threads;
      s_JobCtx.workers     = reinterpret_cast<ThreadWorker*>(s_ThreadWorkerMemory.data());

      for (std::size_t i = 0; i < num_threads; ++i)
      {
        ThreadWorker* const worker = new (s_JobCtx.workers + i) ThreadWorker();

        worker->start(i == k_MainThreadID);
      }

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
        Task* const task = s_JobCtx.main_queue.pop();

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

      {
        // std::unique_lock<std::mutex> lock(s_JobCtx.worker_sleep_mutex);

        for (std::size_t i = 1; i < num_workers; ++i)
        {
          ThreadWorker* const worker = s_JobCtx.workers + i;

          worker->is_running = false;
        }
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

      ThreadWorker* worker = s_JobCtx.workers + worker_id;

      // While we cannot allocate do some work.
      while (!(worker->num_allocated_tasks < k_MaxTasksPerWorker))
      {
        worker->garbageCollectAllocatedTasks(worker_id);
        worker->run(worker_id);
      }

      Task* const      task     = worker->task_memory.allocateT<Task>(worker_id, function, parent ? parent->toTaskPtr() : TaskPtr{NullTaskHandle, NullTaskHandle});
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
      assert(self->num_continuations < k_MaxTaskContinuations && "Too many continuations for a single task.");

      const std::int32_t count = ++self->num_continuations;

      self->continuations[count - 1] = continuation->toTaskPtr();
    }

    void taskSubmit(Task* self, QueueType queue) noexcept
    {
      const WorkerID worker_id = currentWorker();

      assert(worker_id < numWorkers() && "This thread was not created by the job system.");

      ThreadWorker* const worker     = s_JobCtx.workers + worker_id;
      bool                has_pushed = false;

      self->q_type = queue;

      while (true)
      {
        // TODO(SR): Check this!
        //   Hope the compiler optimizes this so that there is a not a branch every time through the loop.
        switch (queue)
        {
          case QueueType::MAIN:
          {
            has_pushed = s_JobCtx.main_queue.push(self);
            break;
          }
          case QueueType::HIGH:
          {
            has_pushed = worker->hi_queue.push(self);
            break;
          }
          case QueueType::NORMAL:
          {
            has_pushed = worker->nr_queue.push(self);
            break;
          }
          case QueueType::BACKGROUND:
          {
            has_pushed = worker->bg_queue.push(self);
            break;
          }
        }

        if (has_pushed)
        {
          break;
        }

        // If we could not push to the queues then just do some work.
        s_JobCtx.wakeUpAllWorkers();
        worker->run(worker_id);
      }

      s_JobCtx.wakeUpOneWorker();
    }

    void waitOnTask(const Task* task) noexcept
    {
      assert(task->q_type != k_InvalidQueueType && "The Task must be sumbitted to a queue before you wait on it.");

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

    void JobSystem::sleep(ThreadWorker* worker)
    {
      if (worker->is_running)
      {
        std::unique_lock<std::mutex> lock(worker_sleep_mutex);

        // NOTE(SR):
        //   This does not handle spurious wakeups but I do not consider this a problem just yet.
        worker_sleep_cv.wait(lock /*, [worker]() {
        return !worker->is_running;  // Returns false if the waiting should be continued.
        }*/
        );
      }
    }

    BaseTask::BaseTask(WorkerID worker, TaskFn fn, TaskPtr parent) :
      fn{fn},
      num_unfinished_tasks{1},
      num_continuations{0u},
      parent{parent},
      owning_worker{worker},
      q_type{k_InvalidQueueType}, /* Set to a valid value in 'bf::job::submitTask' */
      continuations{}
    {
      if (!isTaskPtrNull(parent))
      {
        Task* const parent_ptr = taskPtrToPointer(parent);

        ++parent_ptr->num_unfinished_tasks;

        /*
      std::atomic_fetch_add_explicit(
        &parent_ptr->num_unfinished_tasks,
        1,
        // NOTE(Shareef): There are no surrounding memory operations around 'm_UnFinishedJobs'
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

        const std::int32_t num_continuations_local = num_continuations;

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
        // TODO(SR): Make this only for debug builds.
        std::memset(allocated_tasks.data(), 0xFF, sizeof(allocated_tasks));

        new (worker()) std::thread([]() {
          startImpl();

          const WorkerID      thread_id = currentWorker();
          ThreadWorker* const self      = s_JobCtx.workers + thread_id;

          while (self->is_running)
          {
            self->garbageCollectAllocatedTasks(thread_id);

            if (!self->run(thread_id))
            {
              self->yieldTimeSlice();
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
      const HANDLE   handle = (HANDLE)self->worker()->native_handle();

      // Put each thread on to dedicated core

      const DWORD_PTR affinity_mask   = 1ull << wid;
      const DWORD_PTR affinity_result = SetThreadAffinityMask(handle, affinity_mask);

      assert(affinity_result > 0);

      // Increase thread priority

      const BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_HIGHEST);

      assert(priority_result != 0);

      // Name the thread

      char      thread_name[20]   = u8"";
      wchar_t   thread_name_w[20] = L"";
      const int c_size            = std::snprintf(thread_name, sizeof(thread_name), "bf::JobSystem_%i", int(wid));

      std::mbstowcs(thread_name_w, thread_name, c_size);

      const HRESULT hr = SetThreadDescription(handle, thread_name_w);

      assert(SUCCEEDED(hr));
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

           task_memory.deallocateT(task);
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
        task = s_JobCtx.main_queue.pop();
      }

#define TryGetFromQ(q_name)                                                    \
  if (!task)                                                                   \
  {                                                                            \
    task = q_name.pop();                                                       \
                                                                               \
    if (!task)                                                                 \
    {                                                                          \
      const auto other_worker_id = randomWorker();                             \
                                                                               \
      if (other_worker_id != thread_id)                                        \
      {                                                                        \
        ThreadWorker* const other_worker = s_JobCtx.workers + other_worker_id; \
                                                                               \
        task = other_worker->q_name.steal();                                   \
      }                                                                        \
    }                                                                          \
  }

      TryGetFromQ(hi_queue);
      TryGetFromQ(nr_queue);

      // The main thread should not be running tasks with background priority.
      if (thread_id != k_MainThreadID)
      {
        TryGetFromQ(bg_queue);
      }
#undef TryGetFromQ

      if (task)
      {
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

    // Helper Definitions

    static Task* taskPtrToPointer(TaskPtr ptr) noexcept
    {
      ThreadWorker* const worker = s_JobCtx.workers + ptr.worker_id;
      Task* const         result = static_cast<Task*>(worker->task_memory.fromIndex(ptr.task_index));

      assert(ptr.worker_id == result->owning_worker);

      return result;
    }

    bool isTaskPtrNull(TaskPtr ptr) noexcept
    {
      return ptr.task_index == NullTaskHandle;
    }

    static WorkerID randomWorker() noexcept
    {
      return WorkerID(s_JobCtx.rand_range(s_JobCtx.rand_engine));
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

#undef IS_WINDOWS
#undef IS_POSIX
#undef IS_SINGLE_THREADED

/******************************************************************************/
/*
  MIT License

  Copyright (c) 2020 Shareef Abdoul-Raheem

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
