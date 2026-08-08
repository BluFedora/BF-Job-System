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
 * @copyright Copyright (c) 2020-2026 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#ifndef JOB_API_HPP
#define JOB_API_HPP

#include <atomic>   // atomic_uint64_t
#include <cstdint>  // sized integer types
#include <new>      // placement new
#include <utility>  // forward, move

#ifndef JOB_SYS_ASSERTIONS
#define JOB_SYS_ASSERTIONS 1  //!< Should be turned on during development as it catches API misuse, then for release switched off.
#endif

#if JOB_SYS_ASSERTIONS
#define JobAssert(expr, msg) (::job::internal::AssertHandler)((expr), __FILE__, __LINE__, msg)
#else
#define JobAssert(expr, msg)
#endif

namespace job
{
  using WorkerID = std::uint16_t;  //!< The id type of each worker thread.

  /*!
   * @brief
   *   The only syncronization mechanism.
   *   Allows you to wait on tasks you asssociated with this counter.
   *
   * @see job::WaitOn
   */
  struct Counter
  {
    std::atomic_uint64_t unfinished_tasks = 0u;
  };

  struct Ctx
  {
    Counter*    task_counter   = nullptr;  //!< The counter this task will decrement when done.
    WorkerID    current_worker = 0u;       //!< The worker the current task is running on.
    const char* task_name      = 0u;       //!< The debug name of the task.
  };

  /*!
   * @brief
   *   Determines which threads the task will be allowed to run on.
   */
  enum class QueueMode : std::uint8_t
  {
    Default,     //!< Tasks in this queue will run on either the main or worker threads.
    WorkerOnly,  //!< Tasks in this queue will never run on the main thread.
  };

  /*!
   * @brief
   *   Makes system calls to grab the number threads / processors on the device.
   *   This function can be called by any thread concurrently.
   *
   *   Can be called even if the job system has not been initialized.
   *
   * @return std::size_t
   *   The number threads / processors on the computer.
   */
  std::size_t NumSystemThreads() noexcept;

  /*!
   * @brief
   *   The runtime configuration for the Job System.
   */
  struct JobSystemCreateOptions
  {
    std::uint16_t num_threads        = 0;     //!< Use 0 to indicate using the number of cores available on the system.
    std::uint16_t normal_queue_size  = 1024;  //!< Number of tasks in each worker's `QueueType::Default` queue. (Must be power of two)
    std::uint16_t worker_queue_size  = 512;   //!< Number of tasks in each worker's `QueueType::WorkerOnly` queue. (Must be power of two)
    std::uint64_t job_steal_rng_seed = 0u;    //!< The RNG for work queue stealing will be seeded with this value.
  };

  /*!
   * @brief
   *   The memory requirements for a given configuration `JobSystemCreateOptions`.
   */
  struct JobSystemMemoryRequirements
  {
    JobSystemCreateOptions options;    //!< The options used to create the memory requirements.
    std::size_t            byte_size;  //!< The number of bytes the job system needed.
    std::size_t            alignment;  //!< The base alignment the pointer should be.

    JobSystemMemoryRequirements(const JobSystemCreateOptions& options = {}) noexcept;
  };

  /*!
   * @brief
   *   Sets up the Job system and creates all the worker threads.
   *   The thread that calls 'job::Initialize' is considered the main thread.
   *
   * @param memory_requirements
   *   The customization parameters to initialize the system with.
   *   To be gotten from `job::MemRequirementsForConfig`.
   *
   * @param memory
   *   Must be `memory_requirements.byte_size` in size and with alignment `memory_requirements.alignment`.
   *   If nullptr then the system heap will be used.
   */
  void Initialize(const JobSystemMemoryRequirements& memory_requirements = {}, void* const memory = nullptr) noexcept;

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
   *   and shutdown any threads created by 'bfjob::initialize'.
   *
   *  @warning
   *    This function may only be called by the main thread.
   */
  void Shutdown() noexcept;

  /*!
   * @brief
   *   Main API entrypoint, Pushes a task onto the queue.
   *
   * @tparam Closure
   *   Callable Type with `void Closure(const job::Ctx& ctx);`.
   *
   * @param name
   *   Optional name for the task, can be null. Not used internally for anything.
   *
   * @param counter
   *   The counter to associate the job with.
   *
   * @param Callback
   *   Callable with `[](const job::Ctx& ctx) -> void {}`.
   *
   * @param queue
   *   Which queue to push the task to.
   *
   * @warning If the no free tasks are avaiable from the pool the task will run inline of this thread regardles of the \p queue mode.
   */
  template<typename Closure>
  void Dispatch(const char* const name, Counter* const counter, const Closure& Callback, const QueueMode queue = QueueMode::Default) noexcept;

  /*!
   * @brief
   *   Blocks until all tasks associated with \p counter are done while
   *   This function will block but do work while being blocked.
   *
   * @param counter
   *   The counter to wait for on.
   */
  void WaitOn(const Counter& counter) noexcept;

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

  namespace internal
  {
    struct PrivateCtx : public Ctx
    {
      void* user_data = nullptr;
    };

    using JobFn = void (*)(const PrivateCtx& ctx);

#if JOB_SYS_ASSERTIONS
    void AssertHandler(const bool condition, const char* const filename, const int line_number, const char* const msg);
#endif

    void DispatchImpl(const char* const name,
                      Counter* const    counter,
                      const QueueMode   queue,
                      const JobFn       func,
                      const std::size_t user_data_size,
                      const std::size_t user_data_alignment,
                      const void* const user_data,
                      void (*InitUserData)(void* const user_data, const void* const in_user_data)) noexcept;
  }

  template<typename Closure>
  void Dispatch(const char* const name, Counter* const counter, const Closure& Callback, const QueueMode queue) noexcept
  {
    const internal::JobFn ErasedCallback = +[](const internal::PrivateCtx& ctx) -> void {
      Closure* const typed_callback = static_cast<Closure*>(ctx.user_data);

      (*typed_callback)(static_cast<const Ctx&>(ctx));

      typed_callback->~Closure();
    };

    internal::DispatchImpl(name, counter, queue, ErasedCallback, sizeof(Closure), alignof(Closure), &Callback, [](void* const dst_user_data, const void* const src_user_data) -> void {
      ::new (dst_user_data) Closure(*static_cast<const Closure*>(src_user_data));
    });
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
     *   A splitter object for `job::ParallelFor`.
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
   *   Must be callable like: fn(const job::Ctx& ctx, const std::size_t index)
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
   *   Function object must be callable like: fn(const job::Ctx& ctx, const std::size_t index)
   *
   * @param parent
   *   Parent task to add this task as a child of.
   *
   * @return
   *   The new task holding the work of the parallel for.
   */
  template<typename F, typename S>
  void ParallelFor(const char* const name, Counter* const counter, const std::size_t start, const std::size_t count, S&& splitter, F&& fn, const QueueMode queue = QueueMode::Default)
  {
    job::Dispatch(name, counter, [=, splitter = std::forward<S>(splitter), fn = std::forward<F>(fn)](const job::Ctx& ctx) -> void {
      if (count > 1u && splitter(count))
      {
        const std::size_t left_count  = count / 2;
        const std::size_t right_count = count - left_count;

        job::ParallelFor(ctx.task_name, ctx.task_counter, start + 0, left_count, splitter, fn, queue);
        job::ParallelFor(ctx.task_name, ctx.task_counter, start + left_count, right_count, splitter, fn, queue);
      }
      else
      {
        for (std::size_t offset = 0u; offset < count; ++offset)
        {
          fn(ctx, start + offset);
        }
      } }, queue);
  }

  template<typename T, typename F, typename S>
  void ParallelFor(const char* const name, Counter* const counter, T* const data, const std::size_t count, S&& splitter, F&& fn, const QueueMode queue = QueueMode::Default)
  {
    return job::ParallelFor(name, counter, std::size_t(0), count, std::forward<S>(splitter), [=](const job::Ctx& ctx, const std::size_t index) { fn(ctx, data + index); }, queue);
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
  void ParallelInvoke(const char* const name, Counter* const counter, const QueueMode queue, F&&... fns)
  {
    (job::Dispatch(name, counter, std::forward<F>(fns), queue), ...);
  }

  template<typename Splitter, typename Reducer>
  void ParallelReduce(const char* const name, Counter* const counter, const std::size_t start, const std::size_t count, Splitter&& splitter, Reducer&& reduce, const QueueMode queue = QueueMode::Default)
  {
    const auto ParallelReduce_Impl = [=, splitter = std::forward<Splitter>(splitter), reduce = std::forward<Reducer>(reduce)](const job::Ctx& ctx) -> void {
      // NOTE(SR):
      //   Could also have a stride that increases each step.
      //   This would be bad for Cuda GPU (Shared Memory Bank Conflict)
      //   But good on CPU with better locality.
      //   https://developer.download.nvidia.com/assets/cuda/files/reduction.pdf

      std::size_t count_left = count;

      while (count_left > 1)
      {
        const std::size_t stride = count_left / 2;

        const auto ReduceRange = [stride, &reduce](const job::Ctx& ctx, const std::size_t index) -> void {
          reduce(ctx, index, index + stride);
        };

        Counter c{};
        ParallelFor(name, &c, start, stride, splitter, ReduceRange, queue);
        WaitOn(c);

        if ((count_left & 1) != 0)
        {
          reduce(ctx, start, start + count_left - 1);
        }

        count_left = stride;
      }
    };

    job::Dispatch(name, counter, ParallelReduce_Impl, queue);
  }
}

#endif  // JOB_API_HPP

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
