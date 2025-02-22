/******************************************************************************/
/*!
 * @file   job_api.hpp
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
 * @copyright Copyright (c) 2020-2024 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#ifndef JOB_API_HPP
#define JOB_API_HPP

#include "job_assert.hpp"      // JobAssert
#include "job_init_token.hpp"  // InitializationToken

#include <cstdint>  // sized integer types
#include <new>      // placement new
#include <utility>  // forward, move

namespace Job
{
  // Fwd Declarations

  /*!
   * @brief
   *   A single 'job' to be run by this system.
   */
  struct Task;

  // Enums

  /*!
   * @brief
   *   Determines which threads the task will be allowed to run on.
   */
  enum class QueueType : std::uint8_t
  {
    NORMAL = 0,  //!< Tasks in this queue will run on either the main or worker threads.
    MAIN   = 1,  //!< Tasks in this queue will only be run by the main thread.
    WORKER = 2,  //!< Tasks in this queue will never run on the main thread.
  };

  // Type Aliases

  using WorkerID = std::uint16_t;    //!< The id type of each worker thread.
  using TaskFn   = void (*)(Task*);  //!< The signature of the type of function for a single Task.

  // Private

  namespace detail
  {
    QueueType taskQType(const Task* task) noexcept;
    void*     taskGetPrivateUserData(Task* const task, const std::size_t alignment) noexcept;
    void*     taskReservePrivateUserData(Task* const task, const std::size_t num_bytes, const std::size_t alignment) noexcept;
    bool      mainQueueTryRunTask(void) noexcept;
  }  // namespace detail

  /*!
   * @brief
   *   Makes some system calls to grab the number threads / processors on the device.
   *   This function can be called by any thread concurrently.
   *
   *   Can be called even if the job system has not been initialized.
   *
   * @return std::size_t
   *   The number threads / processors on the computer.
   */
  std::size_t NumSystemThreads() noexcept;

  // Main System API

  /*!
   * @brief
   *   The runtime configuration for the Job System.
   */
  struct JobSystemCreateOptions
  {
    std::uint8_t  num_user_threads   = 0;     //!< The number of threads not owned by this system but wants access to the Job API (The thread must call Job::SetupUserThread).
    std::uint8_t  num_threads        = 0;     //!< Use 0 to indicate using the number of cores available on the system.
    std::uint16_t main_queue_size    = 256;   //!< Number of tasks in the job system's `QueueType::MAIN` queue. (Must be power of two)
    std::uint16_t normal_queue_size  = 1024;  //!< Number of tasks in each worker's `QueueType::NORMAL` queue. (Must be power of two)
    std::uint16_t worker_queue_size  = 32;    //!< Number of tasks in each worker's `QueueType::WORKER` queue. (Must be power of two)
    std::uint64_t job_steal_rng_seed = 0u;    //!< The RNG for work queue stealing will be seeded with this value.
  };

  /*!
   * @brief
   *   The memory requirements for a given configuration `JobSystemCreateOptions`.
   */
  struct JobSystemMemoryRequirements
  {
    const JobSystemCreateOptions options;    //!< The options used to create the memory requirements.
    std::size_t                  byte_size;  //!< The number of bytes the job system needed.
    std::size_t                  alignment;  //!< The base alignment the pointer should be.

    JobSystemMemoryRequirements(const JobSystemCreateOptions& options = {}) noexcept;
  };

  /*!
   * @brief
   *   Sets up the Job system and creates all the worker threads.
   *   The thread that calls 'Job::Initialize' is considered the main thread.
   *
   * @param memory_requirements
   *   The customization parameters to initialize the system with.
   *   To be gotten from `Job::MemRequirementsForConfig`.
   *
   * @param memory
   *   Must be `memory_requirements.byte_size` in size and with alignment `memory_requirements.alignment`.
   *   If nullptr then the system heap will be used.
   *
   * @return
   *   The `InitializationToken` can be used by other subsystem to verify that the Job System has been initialized.
   */
  InitializationToken Initialize(const JobSystemMemoryRequirements& memory_requirements = {}, void* const memory = nullptr) noexcept;

  /*!
   * @brief
   *   Must be called in the callstack of the thread to be setup.
   *
   *   Sets up the state needed to be able to use the job system from this thread.
   *   The job system will not start up until all user threads have been setup.
   *
   * @warning
   *   Must never be called by either a thread setup by this system or the main thread.
   */
  void SetupUserThread();

  /*!
   * @brief
   *   An implementation defined name for the CPU architecture of the device.
   *   This function can be called by any thread concurrently.
   *
   * @return const char*
   *   Nul terminated name for the CPU architecture of the device.
   */
  const char* ProcessorArchitectureName() noexcept;

  /*!
   * @brief
   *   Returns the number of workers created by the system.
   *   This function can be called by any thread concurrently.
   *
   * @return std::size_t
   *   The number of workers created by the system.
   */
  std::uint16_t NumWorkers() noexcept;

  /*!
   * @brief
   *   The current id of the current thread.
   *   This function can be called by any thread concurrently.
   *
   *   The main thread will always be 0.
   *
   * @return WorkerID
   *   The current id of the current thread.
   */
  WorkerID CurrentWorker() noexcept;

  /*!
   * @brief
   *   Allows for querying if we are currently executing in the main thread.
   *
   * @return
   *   True if we are in the main thread, false otherwise.
   *
   * @warning
   *   Must only be called from a thread registered with the job system.
   */
  bool IsMainThread() noexcept;

  /*!
   * @brief
   *   This will deallocate any memory used by the system
   *   and shutdown any threads created by 'bfJob::initialize'.
   *
   *  @warning
   *    This function may only be called by the main thread.
   */
  void Shutdown() noexcept;

  // Task API

  /*!
   * @brief
   *   A buffer for user-data you can write to, maybe large enough to store task data inline.
   *
   *   If you store non trivial data remember to manually call it's destructor at the end of the task function.
   *
   *   If you call 'TaskEmplaceData' or 'TaskSetData' and need to update the data once more be sure to
   *   destruct the previous contents correctly if the data stored in the buffer is non trivial.
   */
  struct TaskData
  {
    void*       ptr;   //!< The start of the buffer you may write to.
    std::size_t size;  //!< The size of the buffer.
  };

  /*!
   * @brief
   *   Creates a new Task that should be later submitted by calling 'TaskSubmit'.
   *
   * @param function
   *   The function you want run by the scheduler.
   *
   * @param parent
   *   An optional parent Task used in conjunction with 'WaitOnTask' to force dependencies.
   *
   * @return Task*
   *   The newly created task.
   */
  Task* TaskMake(const TaskFn function, Task* const parent = nullptr) noexcept;

  /*!
   * @brief
   *   Returns you the user-data buffer you way write to get data into your TaskFn.
   *
   * @param task
   *   The task whose user-data you want to grab.
   *
   * @param alignment
   *   The required alignment the returned pointer must have.
   *
   * @return TaskData
   *   The user-data buffer you may read and write.
   */
  TaskData TaskGetData(Task* const task, const std::size_t alignment) noexcept;

  /*!
   * @brief
   *   A 'continuation' is a task that will be added to a queue after the 'self' Task has finished running.
   *
   *   Continuations will be added to the same queue as the queue from the task that submits it.
   *
   * @param self
   *   The task to add the 'continuation' to.
   *
   * @param continuation
   *   The Task to run after 'self' has finished.
   *   This task must not have already been submitted to a queue.
   *
   * @param queue
   *   The queue you want the task to run on.
   */
  void TaskAddContinuation(Task* const self, Task* const continuation, const QueueType queue = QueueType::NORMAL) noexcept;

  /*!
   * @brief
   *   Grabs the user-data pointer as the T you specified.
   *   No safety is guaranteed, this is just a dumb cast.
   *
   * @tparam T
   *   The type you want to receive the user-data buffer as.
   *
   * @param task
   *   The task whose data you are retrieving.
   *
   * @return T&
   *   The user-data buffer casted as a T.
   */
  template<typename T>
  T* TaskDataAs(Task* const task) noexcept;

  /*!
   * @brief
   *   Calls the constructor of T on the user-data buffer.
   *
   * @tparam T
   *   The type of T you want constructed in-place into the user-data buffer.
   *
   * @tparam Args
   *   The Argument types passed into the T constructor.
   *
   * @param task
   *   The task whose user-data buffer is affected.
   *
   * @param args
   *   The arguments passed into the constructor of the user-data buffer casted as a T.
   */
  template<typename T, typename... Args>
  void TaskEmplaceData(Task* const task, Args&&... args);

  /*!
   * @brief
   *   Copies 'data' into the user-data buffer by calling the T copy constructor.
   *
   * @tparam T
   *   The data type that will be emplaced into the user-data buffer.
   *
   * @param task
   *   The task whose user-data buffer is affected.
   *
   * @param data
   *   The data copied into the user-data buffer.
   */
  template<typename T>
  void TaskSetData(Task* const task, const T& data);

  /*!
   * @brief
   *   Helper for calling destructor on the task's user data.
   *
   * @tparam T
   *   The expected type of the data, called ~T on the user data buffer.
   *
   * @param task
   *   The task whose user-data buffer is affected.
   */
  template<typename T>
  void TaskDestructData(Task* const task);

  /*!
   * @brief
   *    Creates a new task making a copy of the closure.
   *
   * @tparam Closure
   *   The type of the callable.
   *
   * @param function
   *   The non pointer callable you want to store.
   *
   * @param parent
   *   An optional parent Task used in conjunction with 'WaitOnTask' to force dependencies.
   *
   * @return Task*
   *   The newly created task.
   */
  template<typename Closure>
  Task* TaskMake(Closure&& function, Task* const parent = nullptr);

  /*!
   * @brief
   *   Increments the task's ref count preventing it from being garbage collected.
   *
   *   This function should be called before `taskSubmit`.
   *
   * @param task
   *   The task's who's ref count should be incremented.
   */
  void TaskIncRef(Task* const task) noexcept;

  /*!
   * @brief
   *   Decrements the task's ref count allow it to be garbage collected.
   *
   * @param task
   *   The task's who's ref count should be decremented.
   */
  void TaskDecRef(Task* const task) noexcept;

  /*!
   * @brief
   *   Returns the done status of the task.
   *
   *   This is only safe to call after submitting the task if you have an active reference to
   *   the task through a call to TaskIncRef.
   *
   * @param task
   *   The task to check whether or not it's done.
   *
   * @return
   *   true  - The task is done running.
   *   false - The task is still running.
   *
   * @see TaskIncRef
   */
  bool TaskIsDone(const Task* const task) noexcept;

  /*!
   * @brief
   *   Runs tasks from the main queue as long as there are tasks available and \p condition returns true.
   *
   *   This function is not required to be called since the main queue will
   *   be evaluated during other calls to this API but allows for an easy way
   *   to flush the main queue guaranteeing a minimum latency.
   *
   * @tparam ConditionFn
   *   The type of the callable. Must be callable like: `bool operator()(void);`.
   *
   * @param condition
   *   The function object indicating if the main queue should continue being evaluated.
   *   Will be called after a task has been completed.
   *
   * @warning Must only be called from the main thread.
   */
  template<typename ConditionFn>
  void TickMainQueue(ConditionFn&& condition) noexcept
  {
    do
    {
      if (!detail::mainQueueTryRunTask())
      {
        break;
      }
    } while (condition());
  }

  /*!
   * @brief
   *   Runs tasks from the main queue until it is empty.
   *
   *   This function is not required to be called since the main queue will
   *   be evaluated during other calls to this API but allows for an easy way
   *   to flush the main queue guaranteeing a minimum latency.
   *
   * @warning Must only be called from the main thread.
   */
  inline void TickMainQueue() noexcept
  {
    TickMainQueue([]() { return true; });
  }

  /*!
   * @brief
   *   Submits the task to the specified queue.
   *
   *   The Task is not required to have been created on the same thread that submits.
   *
   *   You may now wait on this task using 'WaitOnTask'.
   *
   * @param self
   *   The task to submit.
   *
   * @param queue
   *   The queue you want the task to run on.
   */
  void TaskSubmit(Task* const self, const QueueType queue = QueueType::NORMAL) noexcept;

  /*!
   * @brief
   *   Waits until the specified `task` is done executing.
   *   This function will block but do work while being blocked so there is no wasted time.
   *
   *   You may only call this function with a task created on the current 'Worker'.
   *
   *   It is a logic error to call this function on a task that has not been submitted (\ref TaskSubmit).
   *
   * @param task
   *   The task to wait to finish executing.
   */
  void WaitOnTask(const Task* const task) noexcept;

  /*!
   * @brief Same as calling `taskSubmit` followed by `waitOnTask`.
   *
   * @param self
   *   The task to submit and wait to finish executing.
   *
   * @param queue
   *   The queue you want the task to run on.
   */
  void TaskSubmitAndWait(Task* const self, const QueueType queue = QueueType::NORMAL) noexcept;

  /*!
   * @brief
   *   CPU pause instruction to indicate when you are in a spin wait loop.
   */
  void PauseProcessor() noexcept;

  /*!
   * @brief
   *   Asks the OS to yield this threads execution to another thread on the current cpu core.
   */
  void YieldTimeSlice() noexcept;

  // Template Function Implementation //

  template<typename T>
  T* TaskDataAs(Task* const task) noexcept
  {
    const TaskData data = TaskGetData(task, alignof(T));

    return data.size >= sizeof(T) ? static_cast<T*>(data.ptr) : nullptr;
  }

  template<typename T, typename... Args>
  void TaskEmplaceData(Task* const task, Args&&... args)
  {
    const TaskData data = TaskGetData(task, alignof(T));

    JobAssert(data.size >= sizeof(T), "Attempting to store an object too large to fit within a task's storage buffer.");

    new (data.ptr) T(std::forward<Args>(args)...);
  }

  template<typename T>
  void TaskSetData(Task* const task, const T& data)
  {
    TaskEmplaceData<T>(task, data);
  }

  template<typename T>
  void TaskDestructData(Task* const task)
  {
    TaskDataAs<T>(task)->~T();
  }

  template<typename Closure>
  Task* TaskMake(Closure&& function, Task* const parent)
  {
    Task* const task = TaskMake(
     +[](Task* const task) -> void {
       Closure& function = *static_cast<Closure*>(detail::taskGetPrivateUserData(task, alignof(Closure)));
       function(task);
       function.~Closure();
     },
     parent);
    void* const private_data = detail::taskReservePrivateUserData(task, sizeof(Closure), alignof(Closure));
    new (private_data) Closure(std::forward<Closure>(function));

    return task;
  }

  // Parallel Algorithms API

  struct Splitter
  {
    /*!
     * @brief
     *   Splits work evenly across the threads depending on the number of workers.
     *
     *   Ex:
     *     total_num_items       = 400
     *     num_groups_per_thread = 2
     *     num_threads           = 4
     *
     *    Leads to 8 groups of work each with 50 items.
     *    If num_groups_per_thread was changed to 1 then you will get 4 groups of work each with 100 items.
     *
     * @param total_num_items
     *   The total number of items being processed.
     *
     * @param num_groups_per_thread
     *   The number of groups of items to be created per thread.
     *
     * @return
     *   A splitter object for `Job::ParallelFor`.
     */
    static Splitter EvenSplit(const std::size_t total_num_items, std::size_t num_groups_per_thread = 1u)
    {
      if (num_groups_per_thread < 1u)
      {
        num_groups_per_thread = 1u;
      }

      return Splitter{(total_num_items / num_groups_per_thread) / NumWorkers()};
    }

    static constexpr Splitter MaxItemsPerTask(const std::size_t max_items)
    {
      return Splitter{max_items};
    }

    template<typename T>
    static constexpr Splitter MaxDataSize(const std::size_t max_data_size)
    {
      return Splitter{max_data_size / sizeof(T)};
    }

    std::size_t max_count = 0u;

    constexpr bool operator()(const std::size_t count) const { return count > max_count; }
  };

  /*!
   * @brief
   *   Parallel for algorithm, splits the work up recursively splitting based on the
   *   \p splitter passed in.
   *
   *   Assumes all callable objects passed in can be invoked on multiple threads at the same time.
   *
   * @tparam F
   *   Type of function object passed in.
   *   Must be callable like: fn(Task* task, std::size_t index_range)
   *
   * @tparam S
   *   Callable splitter, must be callable like: splitter(std::size_t count)
   *
   * @param start
   *   Start index for the range to be parallelized.
   *
   * @param count
   *    \p start + count defines the end range.
   *
   * @param splitter
   *   Callable splitter, must be callable like: splitter(std::size_t count)
   *
   * @param fn
   *   Function object must be callable like: fn(Job::Task* const task, const std::size_t index)
   *
   * @param parent
   *   Parent task to add this task as a child of.
   *
   * @return
   *   The new task holding the work of the parallel for.
   */
  template<typename F, typename S>
  Task* ParallelFor(const std::size_t start, const std::size_t count, S&& splitter, F&& fn, Task* parent = nullptr)
  {
    return TaskMake(
     [=, splitter = std::move(splitter), fn = std::move(fn)](Task* const task) {
       if (count > 1u && splitter(count))
       {
         const std::size_t left_count    = count / 2;
         const std::size_t right_count   = count - left_count;
         const QueueType   parent_q_type = detail::taskQType(task);

         TaskSubmit(ParallelFor(start, left_count, splitter, fn, task), parent_q_type);
         TaskSubmit(ParallelFor(start + left_count, right_count, splitter, fn, task), parent_q_type);
       }
       else
       {
         for (std::size_t offset = 0u; offset < count; ++offset)
         {
           fn(task, start + offset);
         }
       }
     },
     parent);
  }

  template<typename Splitter, typename Reducer>
  Task* ParallelReduce(const std::size_t start, const std::size_t count, Splitter&& splitter, Reducer&& reduce, Task* parent = nullptr)
  {
    return TaskMake(
     [=, splitter = std::move(splitter), reduce = std::move(reduce)](Task* const task) {
       // NOTE(SR):
       //   Could also have a stride that increases each step.
       //   This would be bad for Cuda GPU (Shared Memory Bank Conflict)
       //   But good on CPU with better locality.
       //   https://developer.download.nvidia.com/assets/cuda/files/reduction.pdf

       const QueueType parent_q_type = detail::taskQType(task);
       std::size_t     count_left    = count;
       while (count_left > 1)
       {
         const std::size_t stride = count_left / 2;

         const auto ReduceRange = [stride, &reduce](Task* const sub_task, const std::size_t index) {
           reduce(sub_task, index, index + stride);
         };

         TaskSubmitAndWait(ParallelFor(start, stride, splitter, ReduceRange, nullptr), parent_q_type);

         if ((count_left & 1) != 0)
         {
           reduce(task, start, start + count_left - 1);
         }

         count_left = stride;
       }
     },
     parent);
  }

  /*!
   * @brief
   *   Parallel for algorithm, splits the work up recursively splitting based on the
   *   \p splitter passed in. This version is a helper for array data.
   *
   *   Assumes all callable objects passed in can be invoked on multiple threads at the same time.
   *
   * @tparam T
   *   Type of the array to process.
   *
   * @tparam F
   *   Type of function object passed in.
   *   Must be callable like: fn(Task* task, IndexRange index_range)
   *
   * @tparam S
   *   Callable splitter, must be callable like: splitter(std::size_t count)
   *
   * @param data
   *   The start of the array to process.
   *
   * @param count
   *   The number of elements in the \p data array.
   *
   * @param splitter
   *   Callable splitter, must be callable like: splitter(std::size_t count)
   *
   * @param fn
   *   Function object must be callable like: fn(job::Task* task, T* data_start, const std::size_t num_items)
   *
   * @param parent
   *   Parent task to add this task as a child of.
   *
   * @return
   *   The new task holding the work of the parallel for.
   */
  template<typename T, typename F, typename S>
  Task* ParallelFor(T* const data, const std::size_t count, S&& splitter, F&& fn, Task* parent = nullptr)
  {
    return ParallelFor(
     std::size_t(0), count, std::move(splitter), [data, fn = std::move(fn)](Task* const task, const std::size_t index) {
       fn(task, data + index);
     },
     parent);
  }

  /*!
   * @brief
   *   Invokes each passed in function object in parallel.
   *
   * @tparam ...F
   *   The function objects types.
   *   Must be callable like: fn(Task* task)
   *
   * @param parent
   *   Parent task to add this task as a child of.
   *
   * @param ...fns
   *    Function objects must be callable like: fn(Task* task)
   *
   * @return
   *   The new task holding the work of the parallel invoke.
   */
  template<typename... F>
  Task* ParallelInvoke(Task* const parent, F&&... fns)
  {
    return TaskMake(
     [=](Task* const parent_task) mutable {
       const QueueType parent_q_type = detail::taskQType(parent_task);
       (TaskSubmit(TaskMake(std::move(fns), parent_task), parent_q_type), ...);
     },
     parent);
  }
}  // namespace Job

#endif  // JOB_API_HPP

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
