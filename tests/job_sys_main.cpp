//
// Shareef Abdoul-Raheem
// job_sys_main.cpp
//
// Contains Unit Test for the Job System.
//
#include "concurrent/bf_job_ext.hpp"

#include <gtest/gtest.h>

#include <memory>   // unique_ptr
#include <numeric>  // iota

static constexpr int k_NumJobsForTestingOverhead = 65000;

std::unique_ptr<int[]> AllocateIntArray(const std::size_t num_elements)
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

// Tests the time it takes to creating empty jobs serially.
TEST(JobSystemTests, JobCreationOverheadSerial)
{
  Job::Task* const root = Job::taskMake([](Job::Task* root) {
    for (int i = 0u; i < k_NumJobsForTestingOverhead; ++i)
    {
      Job::taskSubmit(Job::taskMake([](Job::Task*) { /* NO-OP */ }, root));
    }
  });

  waitOnTask(taskSubmit(root));
}

// Tests the time it takes to creating empty jobs recursively split by the parallel_for.
TEST(JobSystemTests, JobCreationOverheadParallelFor)
{
  Job::Task* const task = Job::parallel_for(
   0, k_NumJobsForTestingOverhead, Job::CountSplitter{0}, [](Job::Task* parent, const Job::IndexRange& index_range) {
     /* NO-OP */
   });

  Job::waitOnTask(Job::taskSubmit(task));
}

// Tests `parallel_for` making sure each index is hit once.
TEST(JobSystemTests, BasicParallelForRange)
{
  static constexpr int         k_DataSize   = 1000000;
  static constexpr int         k_DataSplit  = 2500;
  const std::unique_ptr<int[]> example_data = AllocateIntArray(k_DataSize);

  std::fill_n(example_data.get(), k_DataSize, 0);

  Job::Task* const task = Job::parallel_for(
   0, k_DataSize, Job::CountSplitter{k_DataSplit}, [&example_data](Job::Task*, const Job::IndexRange index_range) {
     for (const std::size_t i : index_range)
     {
       ++example_data[i];
     }
   });

  Job::waitOnTask(Job::taskSubmit(task));

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

  Job::Task* const task = Job::parallel_for(
   example_data.get(), k_DataSize, Job::CountSplitter{k_DataSplit}, [multiplier](Job::Task*, int* data, std::size_t data_count) {
     EXPECT_LE(data_count, k_DataSplit);

     for (std::size_t i = 0; i < data_count; ++i)
     {
       data[i] = data[i] * multiplier;
     }
   });

  Job::waitOnTask(Job::taskSubmit(task));

  for (int i = 0; i < k_DataSize; ++i)
  {
    EXPECT_EQ(example_data[i], i * multiplier) << "Data incorrect at index " << i;
  }
}

// Test `parallel_invoke` making sure both tasks are run and finish.
TEST(JobSystemTests, BasicParallelInvoke)
{
  static constexpr int         k_DataSize   = 1000000;
  const std::unique_ptr<int[]> example_data = AllocateIntArray(k_DataSize);

  std::fill_n(example_data.get(), k_DataSize, 0);

  const auto task = Job::parallel_invoke(
   nullptr,
   [&](Job::Task* task) {
     for (const std::size_t i : Job::IndexRange{0, k_DataSize / 2})
     {
       ++example_data[i];
     }
   },
   [&](Job::Task* task) {
     for (const std::size_t i : Job::IndexRange{k_DataSize / 2, k_DataSize})
     {
       ++example_data[i];
     }
   });

  Job::waitOnTask(Job::taskSubmit(task));

  for (int i = 0; i < k_DataSize; ++i)
  {
    EXPECT_EQ(example_data[i], 1) << "Each index must be written to exactly once: " << i;
  }
}

// Tests keeping task alive through reference count API
TEST(JobSystemTests, GCReferenceCount)
{
  auto* const long_running_task = Job::taskMake([](Job::Task*) {
    ThreadSleep(std::chrono::milliseconds(12));
  });

  taskIncRef(long_running_task);
  taskSubmit(long_running_task, Job::QueueType::WORKER);

  if (Job::numWorkers() == 1u)
  {
    waitOnTask(long_running_task);
  }
  else
  {
    while (!taskIsDone(long_running_task))
    {
      std::printf("Waiting on long Running task...\n");
      ThreadSleep(std::chrono::milliseconds(1));
    }
  }

  ThreadSleep(std::chrono::milliseconds(12));

  Job::workerGC();

  // Task should still be valid, this call should not crash.

  if (taskIsDone(long_running_task))
  {
    taskDecRef(long_running_task);
  }
}

// Checks correct ref count API usage.
TEST(JobSystemTests, RefCountAPIUsage)
{
  auto* const long_running_task = Job::taskMake([](Job::Task*) {
    ThreadSleep(std::chrono::milliseconds(2));
  });

  // First call to `taskIncRef` must be before a submit.
  taskIncRef(long_running_task);
  taskSubmit(long_running_task, Job::QueueType::WORKER);

  // Any other calls can be at any time.
  taskIncRef(long_running_task);

  if (Job::numWorkers() == 1u)
  {
    waitOnTask(long_running_task);
  }
  else
  {
    while (!taskIsDone(long_running_task))
    {
      ThreadSleep(std::chrono::milliseconds(1));
    }
  }

  ThreadSleep(std::chrono::milliseconds(5));

  taskDecRef(long_running_task);
  taskDecRef(long_running_task);
}

// TODO(SR): Test continuations.
// TODO(SR): Test Dependencies / parent child relationships (this is implicitly tested by the every test though...).

int main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);

  Job::initialize();
  const int result = RUN_ALL_TESTS();
  Job::shutdown();

  return result;
}
