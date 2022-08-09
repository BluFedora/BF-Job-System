/******************************************************************************/
/*!
 * @file   bf_job_api.hpp
 * @author Shareef Abdoul-Raheem (https://blufedora.github.io/)
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
     *   The priority that you want a task to run at.
     */
    enum class QueueType : std::uint16_t
    {
      NORMAL     = 0,  //!< Normally you will want tasks to go into this queue,  Tasks in this queue will run on either the main or worker threads.
      MAIN       = 1,  //!< Use this value when you need a certain task to be run specifically by the main thread.
      BACKGROUND = 2,  //!< Low priority, good for asset loading. Tasks in this queue will never run on the main thread.
    };

    // Type Aliases

    using WorkerID = std::uint16_t;    //!< The id type of each worker thread.
    using TaskFn   = void (*)(Task*);  //!< The signature of the type of function for a single Task.

    // Private

    namespace detail
    {
      void      checkTaskDataSize(std::size_t data_size) noexcept;
      QueueType taskQType(const Task* task) noexcept;
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
    // API functions can only be called by the thread that called 'bfJob::initialize' or from a within a Task function.
    //

    /*!
     * @brief
     *   Sets up the Job system and creates all the worker threads.
     *   The thread that calls 'bfJob::initialize' is considered (and should be) the main thread.
     *
     * @param params
     *   The customization parameters to initialize the system with.
     *
     */
    void initialize(const JobSystemCreateOptions& params = {0u}) noexcept;

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
     *   This should be called as frequently as you want the
     *   main thread's queue to be flushed.
     *
     *   This function may only be called by the main thread.
     */
    void tick();

    /*!
     * @brief
     *   This will deallocate any memory used by the system
     *   and shutdown any threads created by 'bfJob::initialize'.
     *
     *   This function may only be called by the main thread.
     */
    void shutdown() noexcept;

    // Task API

    /*!
     * @brief
     *   Pair of a pointer and the size of the buffer you can write to.
     *   Essentially a buffer for user-data, maybe large enough to store content inline.
     *
     *   If you store non trivial data remember to manually call it's destructor at the bottom of the task function.
     *
     *   If you call 'taskEmplaceData' or 'taskSetData' and need to update the data once more be sure to
     *   free the previous contents correctly if the data stored in the buffer is not trivial.
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
    Task* taskSubmit(Task* const self, QueueType queue = QueueType::NORMAL) noexcept;

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
     *    Creates a new task by using the user-data buffer to store the closure.
     *
     *    When a task is created the `function` is copied into the userdata buffer
     *    so the first sizeof(Closure) bytes are storing the callable.
     *
     *    If you want to store more user-data either be very careful to not
     *    overwrite this function object or just store all needed data in
     *    the function object itself (the latter is much nicer to do and safer).
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
     *   Waits until the specified `task` is done executing.
     *   This function will block but do work while being blocked so there is no wasted time.
     *
     *   You may only call this function with a task created on the current 'Worker'.
     *
     *   It is a logic error to call this function on a task that has not been taskSubmit'd.
     *
     * @param task
     *   The task to wait to finish executing.
     */
    void waitOnTask(const Task* const task) noexcept;

    // Template Function Implementation

    template<typename T>
    T& taskDataAs(Task* const task) noexcept
    {
      detail::checkTaskDataSize(sizeof(T));

      return *static_cast<T*>(taskGetData(task).ptr);
    }

    template<typename T, typename... Args>
    void taskEmplaceData(Task* const task, Args&&... args)
    {
      detail::checkTaskDataSize(sizeof(T));

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
         Closure& function = taskDataAs<Closure>(task);
         function(task);
         function.~Closure();
         // std::destroy_at(&function);
       },
       parent);

      taskEmplaceData<Closure>(task, std::forward<Closure>(function));

      return task;
    }
  }  // namespace job
}  // namespace bf

#endif /* BF_JOB_API_HPP */

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
