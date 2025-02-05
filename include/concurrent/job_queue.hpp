/******************************************************************************/
/*!
 * @file   job_queue.hpp
 * @author Shareef Abdoul-Raheem (https://blufedora.github.io/)
 * @brief
 *   Concurrent Queue Implmementations for different situations.
 *
 * @copyright Copyright (c) 2024-2025 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#ifndef JOB_QUEUE_HPP
#define JOB_QUEUE_HPP

#include "job_api.hpp"     // PauseProcessor
#include "job_assert.hpp"  // JobAssert

#include <algorithm>  // copy_n
#include <atomic>     // atomic<T>
#include <cstddef>    // size_t
#include <iterator>   // make_move_iterator
#include <mutex>      // mutex
#include <new>        // hardware_destructive_interference_size
#include <utility>    // move

// Some Interesting Links:
//   - [A lock-free, concurrent, generic queue in 32 bits](https://nullprogram.com/blog/2022/05/14/)
//

namespace Job
{
  static constexpr std::size_t k_FalseSharingPadSize = std::hardware_destructive_interference_size;

#define Job_CacheAlign alignas(k_FalseSharingPadSize)

  template<typename T>
  class LockedQueue
  {
   public:
    using size_type = std::size_t;

   private:
    std::mutex m_Lock;
    T*         m_Data;
    size_type  m_Capacity;
    size_type  m_CapacityMask;
    size_type  m_WriteIndex;
    size_type  m_Size;

   public:
    void Initialize(T* const memory_backing, const size_type capacity) noexcept
    {
      m_Data         = memory_backing;
      m_Capacity     = capacity;
      m_CapacityMask = capacity - 1;
      m_WriteIndex   = 0u;
      m_Size         = 0u;

      JobAssert((m_Capacity & m_CapacityMask) == 0, "Capacity must be a power of 2.");
    }

    bool Push(const T& value)
    {
      const std::lock_guard<std::mutex> guard(m_Lock);
      (void)guard;

      if (m_Size == m_Capacity)
      {
        return false;
      }

      *elementAt(m_WriteIndex++) = value;
      ++m_Size;

      return true;
    }

    bool Pop(T* const out_value)
    {
      JobAssert(out_value != nullptr, "`out_value` cannot be a nullptr.");

      const std::lock_guard<std::mutex> guard(m_Lock);
      (void)guard;

      if (m_Size == 0u)
      {
        return false;
      }

      *out_value = *elementAt(m_WriteIndex - m_Size);
      --m_Size;

      return true;
    }

   private:
    size_type mask(const size_type raw_index) const noexcept
    {
      return raw_index & m_CapacityMask;
    }

    T* elementAt(const size_type raw_index) const noexcept
    {
      return m_Data + mask(raw_index);
    }
  };

  // [https://www.youtube.com/watch?v=K3P_Lmq6pw0]
  //
  // Single Producer, Single Consumer Lockfree Queue
  //
  template<typename T>
  class SPSCQueue
  {
   public:
    using size_type        = std::size_t;
    using atomic_size_type = std::atomic<size_type>;

   private:
    // Writer Thread

    Job_CacheAlign atomic_size_type m_ProducerIndex;
    unsigned char                   m_Padding0[k_FalseSharingPadSize - sizeof(m_ProducerIndex)];
    Job_CacheAlign size_type        m_CachedConsumerIndex;
    unsigned char                   m_Padding1[k_FalseSharingPadSize - sizeof(m_CachedConsumerIndex)];

    // Reader Thread

    Job_CacheAlign atomic_size_type m_ConsumerIndex;
    unsigned char                   m_Padding2[k_FalseSharingPadSize - sizeof(m_ConsumerIndex)];
    Job_CacheAlign size_type        m_CachedProducerIndex;
    unsigned char                   m_Padding3[k_FalseSharingPadSize - sizeof(m_CachedProducerIndex)];

    // Shared 'Immutable' State

    Job_CacheAlign T* m_Data;
    size_type         m_Capacity;
    size_type         m_CapacityMask;

    static_assert(atomic_size_type::is_always_lock_free, "Expected to be lockfree.");

   public:
    SPSCQueue()  = default;
    ~SPSCQueue() = default;

    // NOTE(SR): Not thread safe.
    void Initialize(T* const memory_backing, const size_type capacity) noexcept
    {
      m_ProducerIndex.store(0, std::memory_order_relaxed);
      m_CachedConsumerIndex = 0;
      m_ConsumerIndex.store(0, std::memory_order_relaxed);
      m_CachedProducerIndex = 0;
      m_Data                = memory_backing;
      m_Capacity            = capacity;
      m_CapacityMask        = capacity - 1;

      JobAssert((m_Capacity & m_CapacityMask) == 0, "Capacity must be a power of 2.");
    }

    bool Push(const T& value)
    {
      return PushLazy([&value](T* const destination) { *destination = value; });
    }

    bool Pop(T* const out_value)
    {
      JobAssert(out_value != nullptr, "`out_value` cannot be a nullptr.");

      return PopLazy([out_value](T&& value) { *out_value = std::move(value); });
    }

    // Memory passed into `callback` is uninitialized, must be placement new'ed into.
    template<typename CallbackFn>
    bool PushLazy(CallbackFn&& callback)
    {
      const size_type write_index = m_ProducerIndex.load(std::memory_order_relaxed);

      if (IsFull(write_index, m_CachedConsumerIndex))
      {
        m_CachedConsumerIndex = m_ConsumerIndex.load(std::memory_order_acquire);
        if (IsFull(write_index, m_CachedConsumerIndex))
        {
          return false;
        }
      }

      callback(ElementAt(write_index));
      m_ProducerIndex.store(write_index + 1, std::memory_order_release);

      return true;
    }

    template<typename CallbackFn>
    bool PopLazy(CallbackFn&& callback)
    {
      const size_type read_index = m_ConsumerIndex.load(std::memory_order_relaxed);

      if (IsEmpty(m_CachedProducerIndex, read_index))
      {
        m_CachedProducerIndex = m_ProducerIndex.load(std::memory_order_acquire);
        if (IsEmpty(m_CachedProducerIndex, read_index))
        {
          return false;
        }
      }

      T* const element = ElementAt(read_index);
      callback(std::move(*element));
      element->~T();
      m_ConsumerIndex.fetch_add(1, std::memory_order_release);

      return true;
    }

   private:
    bool IsFull(const size_type head, const size_type tail) const noexcept
    {
      return ((head + 1) & m_CapacityMask) == tail;
    }

    static bool IsEmpty(const size_type head, const size_type tail) noexcept
    {
      return head == tail;
    }

    T* ElementAt(const size_type index) const noexcept
    {
      return m_Data + (index & m_CapacityMask);
    }
  };

  enum class SPMCDequeStatus
  {
    SUCCESS,      //!< Returned from Push, Pop and Steal
    FAILED_RACE,  //!< Returned from Pop and Steal
    FAILED_SIZE,  //!< Returned from Push, Pop and Steal
  };

  // [Dynamic Circular Work-Stealing Deque](https://www.dre.vanderbilt.edu/~schmidt/PDF/work-stealing-dequeue.pdf)
  // [Correct and Efficient Work-Stealing for Weak Memory Models](https://fzn.fr/readings/ppopp13.pdf)
  template<typename T>
  class SPMCDeque
  {
   private:
    using AtomicT = std::atomic<T>;

    static_assert(AtomicT::is_always_lock_free, "T Should be a small pointer-like type, expected to be lock-free when atomic.");

   public:
    using size_type        = std::int64_t;  // NOTE(SR): Must be signed for Pop to work correctly on empty queue.
    using atomic_size_type = std::atomic<size_type>;

   private:
    Job_CacheAlign atomic_size_type m_ProducerIndex;
    atomic_size_type                m_ConsumerIndex;
    unsigned char                   m_Padding0[k_FalseSharingPadSize - sizeof(m_ProducerIndex) - sizeof(m_ConsumerIndex)];

    // Shared 'Immutable' State

    Job_CacheAlign AtomicT* m_Data;
    size_type               m_Capacity;
    size_type               m_CapacityMask;

   public:
    SPMCDeque()  = default;
    ~SPMCDeque() = default;

    // NOTE(SR): Not thread safe.
    void Initialize(AtomicT* const memory_backing, const size_type capacity) noexcept
    {
      m_ProducerIndex = 0;
      m_ConsumerIndex = 0;
      m_Data          = memory_backing;
      m_Capacity      = capacity;
      m_CapacityMask  = capacity - 1;

      JobAssert((m_Capacity & m_CapacityMask) == 0, "Capacity must be a power of 2.");
    }

    // NOTE(SR): Must be called by owning thread.

    SPMCDequeStatus Push(const T& value)
    {
      const size_type write_index = m_ProducerIndex.load(std::memory_order_relaxed);
      const size_type read_index  = m_ConsumerIndex.load(std::memory_order_acquire);
      const size_type size        = write_index - read_index;

      if (size > m_CapacityMask)
      {
        return SPMCDequeStatus::FAILED_SIZE;
      }

      ElementAt(write_index)->store(value, std::memory_order_relaxed);

      m_ProducerIndex.store(write_index + 1, std::memory_order_release);

      return SPMCDequeStatus::SUCCESS;
    }

    SPMCDequeStatus Pop(T* const out_value)
    {
      const size_type producer_index = m_ProducerIndex.load(std::memory_order_relaxed) - 1;

      // Reserve the slot at the producer end.
      m_ProducerIndex.store(producer_index, std::memory_order_relaxed);

      // The above store needs to happen before this next read
      // to have consistent view of the buffer.
      //
      // `m_ProducerIndex` can only be written to by this thread
      // so first reserve a slot then we read what the other threads have to say.
      //
      std::atomic_thread_fence(std::memory_order_seq_cst);

      size_type consumer_index = m_ConsumerIndex.load(std::memory_order_relaxed);

      if (consumer_index <= producer_index)
      {
        if (consumer_index == producer_index)  // Only one item in queue
        {
          const bool successful_pop = m_ConsumerIndex.compare_exchange_strong(consumer_index, consumer_index + 1, std::memory_order_seq_cst, std::memory_order_relaxed);

          if (successful_pop)
          {
            *out_value = ElementAt(producer_index)->load(std::memory_order_relaxed);
          }

          m_ProducerIndex.store(producer_index + 1, std::memory_order_relaxed);
          return successful_pop ? SPMCDequeStatus::SUCCESS : SPMCDequeStatus::FAILED_RACE;
        }

        *out_value = ElementAt(producer_index)->load(std::memory_order_relaxed);
        return SPMCDequeStatus::SUCCESS;
      }

      // Empty Queue, so restore to canonical empty.
      m_ProducerIndex.store(producer_index + 1, std::memory_order_seq_cst);
      return SPMCDequeStatus::FAILED_SIZE;
    }

    // NOTE(SR): Must be called by non owning thread.

    SPMCDequeStatus Steal(T* const out_value)
    {
      size_type read_index = m_ConsumerIndex.load(std::memory_order_acquire);

      // Must fully read `m_ConsumerIndex` before we read the producer owned `m_ProducerIndex`.
      std::atomic_thread_fence(std::memory_order_seq_cst);

      const size_type write_index = m_ProducerIndex.load(std::memory_order_acquire);

      // if (next_read_index <= write_index)
      if (read_index < write_index)
      {
        // Must load result before the CAS, since a push can happen concurrently right after the CAS.
        T result = ElementAt(read_index)->load(std::memory_order_relaxed);

        // Need strong memory ordering to read the element before the cas.
        if (m_ConsumerIndex.compare_exchange_strong(read_index, read_index + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {
          *out_value = std::move(result);
          return SPMCDequeStatus::SUCCESS;
        }

        return SPMCDequeStatus::FAILED_RACE;
      }

      return SPMCDequeStatus::FAILED_SIZE;
    }

   private:
    AtomicT* ElementAt(const size_type index) const noexcept
    {
      return m_Data + (index & m_CapacityMask);
    }
  };

  // https://www.youtube.com/watch?v=_qaKkHuHYE0&ab_channel=CppCon
  class MPMCQueue
  {
   public:
    using size_type        = std::size_t;
    using atomic_size_type = std::atomic<size_type>;
    using value_type       = unsigned char;  // byte

   private:
    struct IndexRange
    {
      size_type start;
      size_type end;
    };

   private:
    Job_CacheAlign atomic_size_type m_ProducerPending;
    atomic_size_type                m_ProducerCommited;
    unsigned char                   m_Padding0[k_FalseSharingPadSize - sizeof(atomic_size_type) * 2];
    Job_CacheAlign atomic_size_type m_ConsumerPending;
    atomic_size_type                m_ConsumerCommited;
    unsigned char                   m_Padding1[k_FalseSharingPadSize - sizeof(atomic_size_type) * 2];
    Job_CacheAlign value_type*      m_Queue;
    size_type                       m_Capacity;

   public:
    MPMCQueue()  = default;
    ~MPMCQueue() = default;

    // NOTE(SR): Not thread safe.
    void Initialize(value_type* const memory_backing, const size_type capacity) noexcept
    {
      m_ProducerPending.store(0, std::memory_order_relaxed);
      m_ProducerCommited.store(0, std::memory_order_relaxed);
      m_ConsumerPending.store(0, std::memory_order_relaxed);
      m_ConsumerCommited.store(0, std::memory_order_relaxed);
      m_Queue    = memory_backing;
      m_Capacity = capacity;
    }

    //

    bool PushExact(const value_type* elements, const size_type num_elements)
    {
      return PushImpl<true>(elements, num_elements) != 0u;
    }

    size_type PushUpTo(const value_type* elements, const size_type num_elements)
    {
      return PushImpl<false>(elements, num_elements);
    }

    bool PopExact(value_type* out_elements, const size_type num_elements)
    {
      return PopImpl<true>(out_elements, num_elements) != 0u;
    }

    size_type PopUpTo(value_type* out_elements, const size_type num_elements)
    {
      return PopImpl<false>(out_elements, num_elements);
    }

   private:
    template<bool allOrNothing>
    size_type PushImpl(const value_type* elements, const size_type num_elements)
    {
      IndexRange range;
      if (RequestWriteRange<allOrNothing>(&range, num_elements))
      {
        const size_type written_elements = WriteElements(elements, range);
        Commit(&m_ProducerCommited, range);
        return written_elements;
      }

      return 0u;
    }

    template<bool allOrNothing>
    size_type PopImpl(value_type* out_elements, const size_type num_elements)
    {
      IndexRange range;
      if (RequestPopRange<allOrNothing>(&range, num_elements))
      {
        const size_type read_elements = ReadElements(out_elements, range);
        Commit(&m_ConsumerCommited, range);
        return read_elements;
      }

      return 0u;
    }

    template<bool allOrNothing>
    bool RequestWriteRange(IndexRange* out_range, const size_type num_items)
    {
      size_type old_head, new_head;

      old_head = m_ProducerPending.load(std::memory_order_relaxed);
      do
      {
        const size_type tail = m_ConsumerCommited.load(std::memory_order_acquire);

        size_type capacity_left = Distance(old_head, tail);
        if constexpr (allOrNothing)
        {
          if (capacity_left < num_items)
          {
            capacity_left = 0;
          }
        }

        if (capacity_left == 0)
        {
          return false;
        }

        const size_type num_element_to_write = capacity_left < num_items ? capacity_left : num_items;

        new_head = old_head + num_element_to_write;

      } while (!m_ProducerPending.compare_exchange_weak(old_head, new_head, std::memory_order_relaxed, std::memory_order_relaxed));

      *out_range = {old_head, new_head};
      return true;
    }

    template<bool allOrNothing>
    bool RequestPopRange(IndexRange* out_range, const size_type num_items)
    {
      size_type old_tail, new_tail;

      old_tail = m_ConsumerPending.load(std::memory_order_relaxed);
      do
      {
        const size_type head     = m_ProducerCommited.load(std::memory_order_acquire);
        const size_type distance = Distance(head, old_tail);

        size_t capacity_left = (m_Capacity - distance);
        if constexpr (allOrNothing)
        {
          if (capacity_left < num_items)
          {
            capacity_left = 0;
          }
        }

        if (!capacity_left)
        {
          return false;
        }

        const size_type num_element_to_read = capacity_left < num_items ? capacity_left : num_items;

        new_tail = old_tail + num_element_to_read;

      } while (!m_ConsumerPending.compare_exchange_weak(old_tail, new_tail, std::memory_order_relaxed, std::memory_order_relaxed));

      *out_range = {old_tail, new_tail};
      return true;
    }

    size_type WriteElements(const value_type* const elements, const IndexRange range)
    {
      const size_type real_start             = range.start % m_Capacity;
      const size_type write_size             = Distance(real_start, range.end % m_Capacity);
      const size_type capacity_before_split  = m_Capacity - real_start;
      const size_type num_items_before_split = write_size < capacity_before_split ? write_size : capacity_before_split;
      const size_type num_items_after_split  = write_size - num_items_before_split;

      std::copy_n(elements + 0u, num_items_before_split, m_Queue + real_start);
      std::copy_n(elements + num_items_before_split, num_items_after_split, m_Queue + 0u);

      return write_size;
    }

    size_type ReadElements(value_type* const out_elements, const IndexRange range) const
    {
      const size_type real_start             = range.start % m_Capacity;
      const size_type read_size              = Distance(real_start, range.end % m_Capacity);
      const size_type capacity_before_split  = m_Capacity - real_start;
      const size_type num_items_before_split = read_size < capacity_before_split ? read_size : capacity_before_split;
      const size_type num_items_after_split  = read_size - num_items_before_split;

      std::copy_n(std::make_move_iterator(m_Queue + real_start), num_items_before_split, out_elements + 0u);
      std::copy_n(std::make_move_iterator(m_Queue + 0u), num_items_after_split, out_elements + num_items_before_split);

      return read_size;
    }

    void Commit(atomic_size_type* commit, const IndexRange range) const
    {
      size_type start_copy;
      while (!commit->compare_exchange_weak(
       start_copy = range.start,
       range.end,
       std::memory_order_release,
       std::memory_order_relaxed))
      {
        PauseProcessor();
      }
    }

    size_type Distance(const size_type a, const size_type b) const
    {
      return (b > a) ? (b - a) : m_Capacity - a + b;
    }
  };

#undef Job_CacheAlign

}  // namespace Job

#endif  // JOB_QUEUE_HPP

/******************************************************************************/
/*
  MIT License

  Copyright (c) 2024-2025 Shareef Abdoul-Raheem

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
