/******************************************************************************/
/*!
 * @file   bf_job_ext.hpp
 * @author Shareef Abdoul-Raheem (https://blufedora.github.io/)
 * @date   2020-09-03
 * @brief
 *    Contains extra functionality that is not essential to the core library.
 *
 * @copyright Copyright (c) 2020-2022 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#ifndef BF_JOB_EXT_HPP
#define BF_JOB_EXT_HPP

#include "bf_job_api.hpp" /* Job API */

#include <cstddef> /* size_t */

namespace bf
{
  namespace job
  {
    /*!
     * @brief
     *   Range of indices to iterator over.
     */
    struct index_iterator
    {
      std::size_t idx;

      index_iterator(const std::size_t idx) :
        idx{idx} {}

      index_iterator& operator++() { return ++idx, *this; }
      index_iterator  operator++(int) { return index_iterator{idx + 1}; }
      std::size_t     operator*() const { return idx; }
      friend bool     operator==(const index_iterator& lhs, const index_iterator& rhs) { return lhs.idx == rhs.idx; }
      friend bool     operator!=(const index_iterator& lhs, const index_iterator& rhs) { return lhs.idx != rhs.idx; }
    };

    struct IndexRange
    {
      std::size_t idx_bgn;
      std::size_t idx_end;

      std::size_t    length() const { return idx_end - idx_bgn; }
      index_iterator begin() const { return index_iterator(idx_bgn); }
      index_iterator end() const { return index_iterator(idx_end); }
    };

    template<std::size_t max_count>
    struct StaticCountSplitter
    {
      static_assert(max_count > 0, "The 'max_count' must be at least 1.");

      bool operator()(const std::size_t count) const { return count > max_count; }
    };

    struct CountSplitter
    {
      static CountSplitter EvenSplit(const std::size_t total_num_items, std::size_t num_items_per_thread = 1u)
      {
        if (num_items_per_thread < 1u)
        {
          num_items_per_thread = 1u;
        }

        return CountSplitter{(total_num_items / num_items_per_thread) / numWorkers()};
      }

      std::size_t max_count;

      bool operator()(const std::size_t count) const { return count > max_count; }
    };

    template<typename T, std::size_t max_size>
    struct StaticDataSizeSplitter
    {
      static_assert(max_size >= sizeof(T), "The 'max_size' must be at least the size of a single object.");

      bool operator()(const std::size_t count) const { return sizeof(T) * count > max_size; }
    };

    template<typename T>
    struct DataSizeSplitter
    {
      std::size_t max_size;

      bool operator()(const std::size_t count) const { return sizeof(T) * count > max_size; }
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
     *   Must be callable like: fn(Task* task, IndexRange index_range)
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
     *   Function object must be callable like: fn(Task* task, IndexRange index_range)
     *
     * @param parent
     *   Parent task to add this task as a child of.
     *
     * @return
     *   The new task holding the work of the parallel for.
     */
    template<typename F, typename S>
    Task* parallel_for(const std::size_t start, const std::size_t count, S&& splitter, F&& fn, Task* parent = nullptr)
    {
      return taskMake(
       [=, splitter = std::move(splitter), fn = std::move(fn)](Task* const task) {
         if (count > 1u && splitter(count))
         {
           const std::size_t left_count    = count / 2;
           const std::size_t right_count   = count - left_count;
           const QueueType   parent_q_type = detail::taskQType(task);

           if (left_count)
           {
             taskSubmit(parallel_for(start, left_count, splitter, fn, task), parent_q_type);
           }

           if (right_count)
           {
             taskSubmit(parallel_for(start + left_count, right_count, splitter, fn, task), parent_q_type);
           }
         }
         else
         {
           fn(task, IndexRange{start, start + count});
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
    Task* parallel_for(T* const data, const std::size_t count, S&& splitter, F&& fn, Task* parent = nullptr)
    {
      return parallel_for(
       std::size_t(0), count, std::move(splitter), [data, fn = std::move(fn)](Task* const task, const IndexRange index_range) {
         fn(task, data + index_range.idx_bgn, index_range.length());
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
    Task* parallel_invoke(Task* const parent, F&&... fns)
    {
      return taskMake(
       [=](Task* const parent_task) mutable {
         const QueueType parent_q_type = detail::taskQType(parent_task);
         (taskSubmit(taskMake(std::move(fns), parent_task), parent_q_type), ...);
       },
       parent);
    }
  }  // namespace job
}  // namespace bf

#endif /* BF_JOB_EXT_HPP */

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
