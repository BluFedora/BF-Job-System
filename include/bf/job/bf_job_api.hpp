/******************************************************************************/
/*!
 * @file   bf_job_api.hpp
 * @author Shareef Raheem (https://blufedora.github.io/)
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
 * @copyright Copyright (c) 2020-2023 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#ifndef BF_JOB_API_HPP
#define BF_JOB_API_HPP

#include "bf_job_config.hpp" /* size_t, Config Constants */

#include <cstdint> /* uint8_t, uint16_t */
#include <new>     /* placement new     */
#include <utility> /* forward           */

namespace bf
{
  namespace job
  {
    // Fwd Declarations

    struct Task;

    // Enums

    /*!
     * @brief
     *   Determines which threads the a task will be allowed to run on.
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
      void      checkTaskDataSize(const Task* task, std::size_t data_size) noexcept;
      QueueType taskQType(const Task* task) noexcept;
      void*     taskPaddingStart(Task* const task) noexcept;
      void      taskUsePadding(Task* task, std::size_t num_bytes) noexcept;
      bool      mainQueueRunTask(void) noexcept;
    }  // namespace detail

    // Struct Definitions

    /*!
     * @brief
     *   The runtime configuration for the Job System.
     */
    struct JobSystemCreateOptions
    {
      std::size_t num_threads = 0u;  //!< Use 0 to indicate using the number of cores available on the system.
    };

    /*!
     * @brief
     *   Makes some system calls to grab the number threads / processors on the device.
     *   This function can be called by any thread concurrently.
     *
     *   Can be called before and after job system initialization.
     *
     * @return std::size_t
     *   The number threads / processors on the computer.
     */
    std::size_t numSystemThreads() noexcept;

    // Main System API
    //
    // API functions can only be called by the thread that called 'job::initialize' or from a within a Task function.
    //

    /*!
     * @brief
     *   Sets up the Job system and creates all the worker threads.
     *   The thread that calls 'job::initialize' is considered the main thread.
     *
     * @param params
     *   The customization parameters to initialize the system with.
     */
    void initialize(const JobSystemCreateOptions& params = {}) noexcept;

    /*!
     * @brief
     *   Returns the number of workers created by the system.
     *   This function can be called by any thread concurrently.
     *
     * @return std::size_t
     *   The number of workers created by the system.
     */
    std::uint32_t numWorkers() noexcept;

    /*!
     * @brief
     *   An implementation defined name for the CPU architecture of the device.
     *   This function can be called by any thread concurrently.
     *
     * @return const char*
     *   Nul terminated name for the CPU architecture of the device.
     */
    const char* processorArchitectureName() noexcept;

    /*!
     * @brief
     *   The current id of the current thread.
     *   This function can be called by any thread concurrently.
     *
     * @return WorkerID
     *   The current id of the current thread.
     */
    WorkerID currentWorker() noexcept;

    /*!
     * @brief
     *   This will deallocate any memory used by the system
     *   and shutdown any threads created by 'bfJob::initialize'.
     *
     *  @warning
     *    This function may only be called by the main thread.
     */
    void shutdown() noexcept;

    // Task API

    /*!
     * @brief
     *   A buffer for user-data you can write to, maybe large enough to store task data inline.
     *
     *   If you store non trivial data remember to manually call it's destructor at the end of the task function.
     *
     *   If you call 'taskEmplaceData' or 'taskSetData' and need to update the data once more be sure to
     *   destruct the previous contents correctly if the data stored in the buffer is non trivial.
     */
    struct TaskData
    {
      void*       ptr;   //!< The start of the buffer you may write to.
      std::size_t size;  //!< The size of the buffer.
    };

    /*!
     * @brief
     *   Creates a new Task that should be later submitted by calling 'taskSubmit'.
     *
     * @param function
     *   The function you want run by the scheduler.
     *
     * @param parent
     *   An optional parent Task used in conjunction with 'waitOnTask' to force dependencies.
     *
     * @return Task*
     *   The newly created task.
     */
    Task* taskMake(TaskFn function, Task* const parent = nullptr) noexcept;

    /*!
     * @brief
     *   Returns you the user-data buffer you way write to get data into your TaskFn.
     *
     * @param task
     *   The task whose user-data you want to grab.
     *
     * @return TaskData
     *   The user-data buffer you may read and write.
     */
    TaskData taskGetData(Task* const task) noexcept;

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
     */
    void taskAddContinuation(Task* const self, Task* const continuation) noexcept;

    /*!
     * @brief
     *   Submits the task to the specified queue.
     *
     *   The Task is not required to have been created on the same thread that submits.
     *
     *   You may now wait on this task using 'waitOnTask'.
     *
     * @param self
     *   The task to submit.
     *
     * @param queue
     *   The queue you want the task to run on.
     *
     * @return Task*
     *   Returns the task passed in.
     */
    Task* taskSubmit(Task* const self, const QueueType queue = QueueType::NORMAL) noexcept;

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
    T& taskDataAs(Task* const task) noexcept;

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
    void taskEmplaceData(Task* const task, Args&&... args);

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
    void taskSetData(Task* const task, const T& data);

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
     *   An optional parent Task used in conjunction with 'waitOnTask' to force dependencies.
     *
     * @return Task*
     *   The newly created task.
     */
    template<typename Closure>
    Task* taskMake(Closure&& function, Task* const parent = nullptr);

    /*!
     * @brief
     *   Increments the task's ref count preventing it from being garbage collected.
     *
     *   This function should be called before `taskSubmit`.
     *
     * @param task
     *   The task's who's ref count should be incremented.
     */
    void taskIncRef(Task* const task);

    /*!
     * @brief
     *   Decrements the task's ref count allow it to be garbage collected.
     *
     * @param task
     *   The task's who's ref count should be decremented.
     */
    void taskDecRef(Task* const task);

    /*!
     * @brief
     *   Returns the done status of the task.
     *
     *   This is only safe to call after submitting the task if you have an active reference to
     *   the task through a call to taskIncRef.
     *
     * @param task
     *   The task to check whether or not it's done.
     *
     * @return
     *   true  - The task is done running.
     *   false - The task is still running.
     *
     * @see taskIncRef
     */
    bool taskIsDone(const Task* const task) noexcept;

    /*!
     * @brief
     *   Garbage collects tasks allocated on the current worker.
     */
    void workerGC();

    /*!
     * @brief
     *   Runs tasks from the main queue as long as there are tasks available
     *   or \p condition returns false.
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
     * @param run_gc
     *   Whether or not the garbage collector should be run after running tasks.
     *
     * @warning Must only be called from the main thread.
     */
    template<typename ConditionFn>
    void tickMainQueue(ConditionFn&& condition, const bool run_gc = true) noexcept
    {
      do
      {
        if (!detail::mainQueueRunTask())
        {
          break;
        }
      } while (condition());

      if (run_gc)
      {
        workerGC();
      }
    }

    /*!
     * @brief
     *   Runs tasks from the main queue until it is empty.
     *
     *   This function is not required to be called since the main queue will
     *   be evaluated during other calls to this API but allows for an easy way
     *   to flush the main queue guaranteeing a minimum latency.
     *
     * @param run_gc
     *   Whether or not the garbage collector should be run after running tasks.
     *
     * @warning Must only be called from the main thread.
     */
    inline void tickMainQueue(const bool run_gc = true) noexcept
    {
      tickMainQueue([]() { return true; }, run_gc);
    }

    /*!
     * @brief
     *   Waits until the specified `task` is done executing.
     *   This function will block but do work while being blocked so there is no wasted time.
     *
     *   You may only call this function with a task created on the current 'Worker'.
     *
     *   It is a logic error to call this function on a task that has not been submitted (\ref taskSubmit).
     *
     * @param task
     *   The task to wait to finish executing.
     */
    void waitOnTask(const Task* const task) noexcept;

    /*!
     * @brief Same as calling `taskSubmit` followed by `waitOnTask`.
     *
     * @param self
     *   The task to submit and wait to finish executing.
     *
     * @param queue
     *   The queue you want the task to run on.
     */
    void taskSubmitAndWait(Task* const self, const QueueType queue = QueueType::NORMAL) noexcept;

    // Template Function Implementation

    template<typename T>
    T& taskDataAs(Task* const task) noexcept
    {
      detail::checkTaskDataSize(task, sizeof(T));
      return *static_cast<T*>(taskGetData(task).ptr);
    }

    template<typename T, typename... Args>
    void taskEmplaceData(Task* const task, Args&&... args)
    {
      detail::checkTaskDataSize(task, sizeof(T));
      new (taskGetData(task).ptr) T(std::forward<Args>(args)...);
    }

    template<typename T>
    void taskSetData(Task* const task, const T& data)
    {
      taskEmplaceData<T>(task, data);
    }

    template<typename Closure>
    Task* taskMake(Closure&& function, Task* const parent)
    {
      Task* const task = taskMake(
       +[](Task* task) {
         Closure& function = *static_cast<Closure*>(detail::taskPaddingStart(task));
         function(task);
         function.~Closure();
       },
       parent);
      taskEmplaceData<Closure>(task, std::forward<Closure>(function));
      detail::taskUsePadding(task, sizeof(Closure));

      return task;
    }
  }  // namespace job
}  // namespace bf

#endif /* BF_JOB_API_HPP */

/******************************************************************************/
/*
  MIT License

  Copyright (c) 2020-2023 Shareef Abdoul-Raheem

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
