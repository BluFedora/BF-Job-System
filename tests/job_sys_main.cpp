//
// Shareef Abdoul-Raheem
// job_sys_main.cpp
//
// Contains Unit Test for the Job System.
//
#include "concurrent/job_queue.hpp"

#include <gtest/gtest.h>

#include <memory>   // unique_ptr
#include <numeric>  // iota

struct IndexIterator
{
  std::size_t idx;

  IndexIterator(const std::size_t idx) :
    idx{idx}
  {
  }

  IndexIterator& operator++() { return ++idx, *this; }
  IndexIterator  operator++(int) { return IndexIterator{idx++}; }
  std::size_t    operator*() const { return idx; }
  friend bool    operator==(const IndexIterator& lhs, const IndexIterator& rhs) { return lhs.idx == rhs.idx; }
  friend bool    operator!=(const IndexIterator& lhs, const IndexIterator& rhs) { return lhs.idx != rhs.idx; }
};

struct IndexRange
{
  std::size_t idx_bgn;
  std::size_t idx_end;

  std::size_t   length() const { return idx_end - idx_bgn; }
  IndexIterator begin() const { return IndexIterator(idx_bgn); }
  IndexIterator end() const { return IndexIterator(idx_end); }
};

// static constexpr int k_NumJobsForTestingOverhead = 6500000;
static constexpr int k_NumJobsForTestingOverhead = 6500;

static std::unique_ptr<int[]> AllocateIntArray(const std::size_t num_elements)
{
  return std::unique_ptr<int[]>(new int[num_elements]());
}

template<class _Rep, class _Period>
void ThreadSleep(const std::chrono::duration<_Rep, _Period>& time)
{
#if defined(__EMSCRIPTEN_PTHREADS__) || !defined(__EMSCRIPTEN__)
  std::this_thread::sleep_for(std::chrono::milliseconds(12));
#else
  // std::this_thread::sleep_for(time);
#endif
}

TEST(JobSystemTests, JobUserData)
{
  struct TaskWithData
  {
    alignas(64) int x;
    float         y;
    TaskWithData* z;

    void operator()(const job::Ctx&)
    {
      EXPECT_EQ((std::uintptr_t)this % alignof(TaskWithData), 0) << "Pointer expected to be aligned.";
      EXPECT_EQ(this->x, 5) << "Failed to get x.";
      EXPECT_EQ(this->y, 4.32f) << "Failed to get y.";
      EXPECT_EQ(this->z, reinterpret_cast<TaskWithData*>(std::uintptr_t(0xDEADBEEF))) << "Failed to get z.";
    }
  };

  job::Counter counter{};
  job::Dispatch("TestTask", &counter, TaskWithData{5, 4.32f, reinterpret_cast<TaskWithData*>(std::uintptr_t(0xDEADBEEF))});
  job::WaitOn(counter);
}

// Tests the time it takes to creating empty jobs serially.
TEST(JobSystemTests, JobCreationOverheadSerial)
{
  job::Counter counter{};
  {
    for (int i = 0u; i < k_NumJobsForTestingOverhead; ++i)
    {
      job::Dispatch("", &counter, [](const job::Ctx&) -> void { /* NO-OP */ });
    }
  }
  job::WaitOn(counter);
}

// Tests the time it takes to creating empty jobs recursively split by the ParallelFor.
TEST(JobSystemTests, JobCreationOverheadParallelFor)
{
  job::Counter counter{};
  job::ParallelFor("", &counter, 0, k_NumJobsForTestingOverhead, job::Splitter::MaxItemsPerTask(0), [](const job::Ctx&, const std::size_t) { /* NO-OP */ });
  job::WaitOn(counter);
}

// Tests `parallel_for` making sure each index is hit once.
TEST(JobSystemTests, BasicParallelForRange)
{
  static constexpr int         k_DataSize   = 2048;
  static constexpr int         k_DataSplit  = 512;
  const std::unique_ptr<int[]> example_data = AllocateIntArray(k_DataSize);

  std::fill_n(example_data.get(), k_DataSize, 0);

  job::Counter counter{};
  job::ParallelFor("", &counter, 0, k_DataSize, job::Splitter::MaxItemsPerTask(k_DataSplit), [&example_data](const job::Ctx&, const std::size_t index) {
    ++example_data[index];
  });
  job::WaitOn(counter);

  for (int i = 0; i < k_DataSize; ++i)
  {
    EXPECT_EQ(example_data[i], 1) << "Failed to write to index " << i;
  }
}

// Tests array data variant of `parallel_for`.
TEST(JobSystemTests, BasicParallelForArray)
{
  static constexpr int k_DataSize  = 100000;
  static constexpr int k_DataSplit = 6;

  const int multiplier = 5;

  const std::unique_ptr<int[]> example_data = AllocateIntArray(k_DataSize);

  std::iota(example_data.get(), example_data.get() + k_DataSize, 0);

  job::Counter counter{};
  job::ParallelFor("", &counter, example_data.get(), k_DataSize, job::Splitter::MaxItemsPerTask(k_DataSplit), [&example_data](const job::Ctx&, int* const data_item) {
    *data_item *= multiplier;
  });
  job::WaitOn(counter);

  for (int i = 0; i < k_DataSize; ++i)
  {
    EXPECT_EQ(example_data[i], i * multiplier) << "Data incorrect at index " << i;
  }
}

// Test `parallel_invoke` making sure both tasks are run and finish.
TEST(JobSystemTests, BasicParallelInvoke)
{
  static constexpr int         k_DataSize   = 2048;
  const std::unique_ptr<int[]> example_data = AllocateIntArray(k_DataSize);

  std::fill_n(example_data.get(), k_DataSize, 0);

  job::Counter counter{};
  job::ParallelInvoke(
   "ParallelInvokeTest",
   &counter,
   job::QueueMode::Default,
   [&](const job::Ctx&) {
     for (const std::size_t i : IndexRange{0, k_DataSize / 2})
     {
       ++example_data[i];
     }
   },
   [&](const job::Ctx&) {
     for (const std::size_t i : IndexRange{k_DataSize / 2, k_DataSize})
     {
       ++example_data[i];
     }
   });

  job::WaitOn(counter);

  for (int i = 0; i < k_DataSize; ++i)
  {
    EXPECT_EQ(example_data[i], 1) << "Each index must be written to exactly once: " << i;
  }
}

TEST(JobSystemTests, SPSCQueue)
{
  constexpr auto               backing_storage_capacity = (1<< 12);
  const std::unique_ptr<int[]> backing_storage          = AllocateIntArray(backing_storage_capacity);
  const std::unique_ptr<int[]> queue_result             = AllocateIntArray(backing_storage_capacity * 2);

  job::SPSCQueue<int> q{};

  q.Initialize(backing_storage.get(), backing_storage_capacity);

  std::thread t0{[&]() {
    for (int i = 0; i < backing_storage_capacity * 2; ++i)
    {
      while (!q.Push(i))
      {
      }
    }
  }};

  std::thread t1{[&]() {
    for (int i = 0; i < backing_storage_capacity * 2; ++i)
    {
      while (!q.Pop(queue_result.get() + i))
      {
      }

      queue_result.get()[i] *= 2;
    }
  }};

  t0.join();
  t1.join();
}

int main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);

  job::Initialize();
  const int result = RUN_ALL_TESTS();
  job::Shutdown();

  return result;
}
