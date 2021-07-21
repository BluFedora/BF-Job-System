/******************************************************************************/
/*!
 * @file   bf_job_ext.hpp
 * @author Shareef Abdoul-Raheem (https://blufedora.github.io/)
 * @brief
 *    This header contains extra functionality for managing Tasks
 *    that isn't essential to the core library.
 *  
 * @version 0.0.1
 * @date    2020-09-03
 *
 * @copyright Copyright (c) 2020-2021 Shareef Abdoul-Raheem
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
    namespace detail
    {
      template<typename T>
      inline constexpr bool is_empty_base_candidate = std::is_empty_v<T> && !std::is_final_v<T>;

      template<typename T0, typename T1, bool is_T0_Empty = is_empty_base_candidate<T0>, bool is_T1_Empty = is_empty_base_candidate<T1>>
      struct CompressedPair;

      // clang-format off
      template<typename T0, typename T1>
      struct CompressedPair<T0, T1, true, true> : private T0, private T1
      // clang-format on
      {
        CompressedPair(T0 f, T1 s) :
          T0(std::move(f)),
          T1(std::move(s))
        {
        }

        T0&       first() { return *static_cast<T0*>(this); }
        const T0& first() const { return *static_cast<const T0*>(this); }
        T1&       second() { return *static_cast<T1*>(this); }
        const T1& second() const { return *static_cast<const T1*>(this); }
      };

      template<typename T0, typename T1>
      struct CompressedPair<T0, T1, true, false> : private T0
      {
        T1 second_;

        CompressedPair(T0 f, T1 s) :
          T0(std::move(f)),
          second_(std::move(s))
        {
        }

        T0&       first() { return *static_cast<T0*>(this); }
        const T0& first() const { return *static_cast<const T0*>(this); }
        T1&       second() { return second_; }
        const T1& second() const { return second_; }
      };

      template<typename T0, typename T1>
      struct CompressedPair<T0, T1, false, true> : private T1
      {
        T0 first_;

        CompressedPair(T0 f, T1 s) :
          T1(std::move(s)),
          first_(std::move(f))
        {
        }

        T0&       first() { return first_; }
        const T0& first() const { return first_; }
        T1&       second() { return *static_cast<T1*>(this); }
        const T1& second() const { return *static_cast<const T1*>(this); }
      };

      template<typename T0, typename T1>
      struct CompressedPair<T0, T1, false, false>
      {
        T0 first_;
        T1 second_;

        CompressedPair(T0 f, T1 s) :
          first_(std::move(f)),
          second_(std::move(s))
        {
        }

        T0&       first() { return first_; }
        const T0& first() const { return first_; }
        T1&       second() { return second_; }
        const T1& second() const { return second_; }
      };

      template<typename T, typename S, typename F>
      class ParallelForData final
      {
       private:
        CompressedPair<T*, F>          m_DataAndJob;
        CompressedPair<std::size_t, S> m_CountAndSplitter;

       public:
        ParallelForData(T* data, F job, std::size_t count, S splitter) :
          m_DataAndJob{data, std::forward<F>(job)},
          m_CountAndSplitter{count, std::forward<S>(splitter)}
        {
        }

        T*          data() const { return m_DataAndJob.first(); }
        F&          job() { return m_DataAndJob.second(); }
        std::size_t count() const { return m_CountAndSplitter.first(); }
        S&          splitter() { return m_CountAndSplitter.second(); }
      };

      template<typename T, typename F, typename S>
      void parallel_for_impl(Task* job);
    }  // namespace detail

    template<std::size_t max_count>
    class StaticCountSplitter
    {
      static_assert(max_count > 0, "The 'max_count' must be at least 1.");

     public:
      bool operator()(const std::size_t count) const
      {
        return count > max_count;
      }
    };

    class CountSplitter
    {
     public:
      std::size_t max_count;

      bool operator()(const std::size_t count) const
      {
        return count > max_count;
      }
    };

    template<typename T, std::size_t max_size>
    class StaticDataSizeSplitter
    {
      static_assert(max_size >= sizeof(T), "The 'max_size' must be at least the size of a single object.");

     public:
      bool operator()(const std::size_t count) const
      {
        return sizeof(T) * count > max_size;
      }
    };

    template<typename T>
    class DataSizeSplitter
    {
     public:
      std::size_t max_size;

      bool operator()(const std::size_t count) const
      {
        return sizeof(T) * count > max_size;
      }
    };

    template<typename T, typename F, typename S>
    Task* parallel_for(T* data, const std::size_t count, S splitter, F&& fn, Task* parent = nullptr)
    {
      Task* const task = taskMake(&detail::parallel_for_impl<T, F, S>, parent);

      taskEmplaceData<detail::ParallelForData<T, S, F>>(task, data, fn, count, splitter);

      return task;
    }

    namespace detail
    {
      template<typename T, typename F, typename S>
      void parallel_for_impl(Task* task)
      {
        using ParallelForDataType = ParallelForData<T, S, F>;

        ParallelForDataType& data       = taskDataAs<ParallelForDataType>(task);
        F&                   custom_f   = data.job();
        const std::size_t    data_count = data.count();

        if (data.splitter()(data_count))
        {
          const std::size_t left_count    = data_count / 2;
          const std::size_t right_count   = data_count - left_count;
          const QueueType   parent_q_type = taskQType(task);

          if (left_count)
          {
            Task* const left_task = taskMake(&detail::parallel_for_impl<T, F, S>, task);

            taskEmplaceData<ParallelForDataType>(left_task, data.data(), custom_f, left_count, data.splitter());

            taskSubmit(left_task, parent_q_type);
          }

          if (right_count)
          {
            Task* const right_task = taskMake(&detail::parallel_for_impl<T, F, S>, task);

            taskEmplaceData<ParallelForDataType>(right_task, data.data() + left_count, custom_f, right_count, data.splitter());

            taskSubmit(right_task, parent_q_type);
          }
        }
        else
        {
          custom_f(data.data(), data.count());
        }

        data.~ParallelForDataType();
      }
    }  // namespace detail
  }    // namespace job
}  // namespace bf

#endif /* BF_JOB_EXT_HPP */

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
