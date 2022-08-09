/******************************************************************************/
/*!
  @file   bf_job_queue.hpp
  @author Shareef Abdoul-Raheem
  @brief
    These are basic Single Producer Multiple Consumer Queues.
    Push and Pop MST be called by the producer / owning thread.
    Steal MUST be called by anyone BUT the producer / owning thread.

    Copyright (C) 2020-2021 Shareef Abdoul-Raheem
*/
/******************************************************************************/
#ifndef BF_JOB_QUEUE_HPP
#define BF_JOB_QUEUE_HPP

#include <atomic> /* atomic_int32_t, atomic_signal_fence, atomic_thread_fence */
#include <mutex>  /* mutex, lock_guard                                        */

// NOTE(Shareef):
//   Prevents reordering of statements by the compiler.
//   These are the pre C11/C++11 ways of doing it for each compiler.
//    MSVC:  _ReadWriteBarrier()
//    Intel: __memory_barrier()
//    gcc:   asm volatile("" ::: "memory"); / __asm__ __volatile__ ("" ::: "memory");
#define BF_COMPILER_RW_BARRIER std::atomic_signal_fence(std::memory_order_acq_rel)

// NOTE(Shareef):
//   Prevents reordering of statements by the runtime of the CPU.
//   These are the pre C11/C++11 ways of doing it for each compiler.
//    MSVC:  MemoryBarrier()
//    Intel: __memory_barrier()
//    gcc:   __sync_synchronize();
#define BF_MEMORY_BARRIER std::atomic_thread_fence(std::memory_order_seq_cst)

namespace bf
{
  // Type Aliases

  using AtomicInt32 = std::atomic_int32_t;

  // NOTE(Shareef):
  //   kSize must be a Power of Two.
  //   T should be a pointer type.
  template<std::size_t kSize, typename T>
  class JobQueueM
  {
   public:
    static_assert((kSize & (kSize - 1u)) == 0u, "The size of this JobQueue must be a power of two.");

   private:
    static constexpr std::size_t WRAP_MASK = kSize - 1u;

    std::mutex m_CriticalLock;
    T          m_Queue[kSize];
    int        m_Bottom;
    int        m_Top;

   public:
    JobQueueM() :
      m_CriticalLock(),
      m_Queue(),
      m_Bottom(0),
      m_Top(0)
    {
    }

    bool push(const T& job)
    {
      std::lock_guard<std::mutex> guard(m_CriticalLock);
      (void)guard;

      m_Queue[m_Bottom & WRAP_MASK] = job;
      ++m_Bottom;
      return true;
    }

    T pop(void)
    {
      std::lock_guard<std::mutex> guard(m_CriticalLock);
      (void)guard;

      const auto num_jobs = m_Bottom - m_Top;

      if (num_jobs <= 0)
      {
        return nullptr;
      }

      --m_Bottom;
      return m_Queue[m_Bottom & WRAP_MASK];
    }
  };

  // NOTE(Shareef):
  //   kSize must be a Power of Two.
  //   T should be a pointer type.
  template<std::size_t kSize, typename T>
  class JobQueueA
  {
   private:
    static constexpr std::size_t WRAP_MASK = kSize - 1u;

   public:
    static_assert((kSize & WRAP_MASK) == 0u, "The size of this JobQueue must be a power of two.");

   private:
    std::array<T, kSize> m_Queue;
    AtomicInt32          m_Bottom;
    AtomicInt32          m_Top;

   public:
    JobQueueA() :
      m_Queue{},
      m_Bottom(0),
      m_Top(0)
    {
    }

    inline int size() const
    {
      const auto top = m_Top.load();
      const auto bot = m_Bottom.load();

#if 0
      if (bot - top < 0)
      {
        // __debugbreak();
      }
#endif

      return std::max(bot - top, 0);
    }
#if 0
    bool push(const T& job)
    {
      const int bottom = m_Bottom.load();

      m_Queue[bottom & WRAP_MASK] = job;
      BF_COMPILER_RW_BARRIER;
      m_Bottom.store(bottom + 1);

      return true;
    }

    T pop()
    {
      auto bottom = m_Bottom.load(std::memory_order_acquire) - 1;
      m_Bottom    = bottom;  //.exchange(bottom/*, std::memory_order_acq_rel*/);
      BF_MEMORY_BARRIER;
      // m_Bottom.store(bottom, std::memory_order_release);
      auto top = m_Top.load(std::memory_order_acquire);

      if (top <= bottom)
      {
        T job = m_Queue[bottom & WRAP_MASK];

        if (top != bottom)
        {
          // More than one job left in the queue
          return job;
        }

        // auto expected = top;  // This is needed since 'compare_exchange_strong' changes 'expected' to actual value if operation fails.

        if (std::atomic_compare_exchange_strong(&m_Top, &top, top))
        {
          // Someone already took the last item, abort
          job = nullptr;
        }

        /*
        if (!m_Top.compare_exchange_strong(expected, top + 1, std::memory_order_acq_rel))
        {
          // Someone already took the last item, abort
          job = nullptr;
        }
        */
        m_Bottom.store(top + 1, std::memory_order_release);
        return job;
      }

      // Queue already empty
      m_Bottom.store(top, std::memory_order_release);
      return nullptr;
    }

    T steal()
    {
      auto top = m_Top.load(std::memory_order_acquire);

      BF_COMPILER_RW_BARRIER;

      auto bottom = m_Bottom.load(std::memory_order_acquire);

      // Not empty and don't steal if just one job
      if (top < bottom)
      {
        T job = m_Queue[top & WRAP_MASK];

        auto expected = top;  // This is needed since 'compare_exchange_strong' changes 'expected' to actual value if operation fails.

        if (!m_Top.compare_exchange_strong(expected, top + 1))
        {
          // Someone already took the last item, abort
          return nullptr;
        }

        return job;
      }

      return nullptr;
    }
#else
    bool push(const T& job)
    {
      const std::int32_t bottom = m_Bottom.load(std::memory_order_relaxed);
      const std::int32_t top    = m_Top.load(std::memory_order_acquire);

      if ((bottom - top) > std::int32_t(kSize - 1))
      {
        return false;
      }

      m_Queue[bottom & WRAP_MASK] = job;
      std::atomic_thread_fence(std::memory_order_release);
      m_Bottom.store(bottom + 1, std::memory_order_relaxed);

      return true;
    }

    T pop()
    {
      const auto bottom = m_Bottom.load(std::memory_order_relaxed) - 1;
      m_Bottom.store(bottom, std::memory_order_relaxed);
      BF_MEMORY_BARRIER;
      auto top = m_Top.load(std::memory_order_relaxed);

      if (top <= bottom)
      {
        T job = m_Queue[bottom & WRAP_MASK];

        if (top == bottom)
        {
          if (!m_Top.compare_exchange_strong(
               top,
               top + 1,
               std::memory_order_seq_cst,
               std::memory_order_relaxed))
          {
            job = nullptr;
          }

          m_Bottom.store(bottom + 1, std::memory_order_relaxed);
        }

        return job;
      }

      // Empty Queue
      m_Bottom.store(bottom + 1, std::memory_order_relaxed);
      return nullptr;
    }

    T steal()
    {
      auto top = m_Top.load(std::memory_order_acquire);
      BF_MEMORY_BARRIER;
      const auto bottom = m_Bottom.load(std::memory_order_acquire);

      // Empty Queue
      if (top >= bottom)
      {
        return nullptr;
      }

      T job = m_Queue[top & WRAP_MASK];

      if (!m_Top.compare_exchange_strong(
           top,
           top + 1,
           std::memory_order_seq_cst,
           std::memory_order_relaxed))
      {
        return nullptr;
      }

      return job;
    }
#endif
  };
}  // namespace bf

#endif /* BF_JOB_QUEUE_HPP */

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
