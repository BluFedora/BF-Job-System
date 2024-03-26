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

// static constexpr int k_NumJobsForTestingOverhead = 6500000;
static constexpr int k_NumJobsForTestingOverhead = 6500;

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

TEST(JobSystemTests, JobUserData)
{
  struct TaskData
  {
    alignas(64) int x;
    float     y;
    TaskData* z;
  };

  Job::Task* const root = Job::TaskMake([](Job::Task* root) {
    const TaskData* const data = Job::TaskDataAs<TaskData>(root);

    EXPECT_NE(data, nullptr) << "Should be able to successfully get data.";
    EXPECT_EQ((std::uintptr_t)data % alignof(TaskData), 0) << "Pointer expected to be aligned.";
    EXPECT_EQ(data->x, 5) << "Failed to get x.";
    EXPECT_EQ(data->y, 4.32f) << "Failed to get y.";
    EXPECT_EQ(data->z, (TaskData*)0xDEADBEEF) << "Failed to get z.";

    Job::TaskDestructData<TaskData>(root);
  });

  Job::TaskSetData<TaskData>(root, TaskData{5, 4.32f, (TaskData*)0xDEADBEEF});

  Job::TaskSubmitAndWait(root);
}

// Tests the time it takes to creating empty jobs serially.
TEST(JobSystemTests, JobCreationOverheadSerial)
{
  Job::Task* const root = Job::TaskMake([](Job::Task* root) {
    for (int i = 0u; i < k_NumJobsForTestingOverhead; ++i)
    {
      Job::TaskSubmit(Job::TaskMake([](Job::Task*) { /* NO-OP */ }, root));
    }
  });

  WaitOnTask(TaskSubmit(root));
}

// Tests the time it takes to creating empty jobs recursively split by the ParallelFor.
TEST(JobSystemTests, JobCreationOverheadParallelFor)
{
  Job::Task* const task = Job::ParallelFor(
   0, k_NumJobsForTestingOverhead, Job::CountSplitter{0}, [](Job::Task* parent, const Job::IndexRange& index_range) {
     /* NO-OP */
   });

  Job::WaitOnTask(Job::TaskSubmit(task));
}

// Tests `parallel_for` making sure each index is hit once.
TEST(JobSystemTests, BasicParallelForRange)
{
  static constexpr int         k_DataSize   = 1000000;
  static constexpr int         k_DataSplit  = 2500;
  const std::unique_ptr<int[]> example_data = AllocateIntArray(k_DataSize);

  std::fill_n(example_data.get(), k_DataSize, 0);

  Job::Task* const task = Job::ParallelFor(
   0, k_DataSize, Job::CountSplitter{k_DataSplit}, [&example_data](Job::Task*, const Job::IndexRange index_range) {
     for (const std::size_t i : index_range)
     {
       ++example_data[i];
     }
   });

  Job::WaitOnTask(Job::TaskSubmit(task));

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

  Job::Task* const task = Job::ParallelFor(
   example_data.get(), k_DataSize, Job::CountSplitter{k_DataSplit}, [multiplier](Job::Task*, int* data, std::size_t data_count) {
     EXPECT_LE(data_count, k_DataSplit);

     for (std::size_t i = 0; i < data_count; ++i)
     {
       data[i] = data[i] * multiplier;
     }
   });

  Job::WaitOnTask(Job::TaskSubmit(task));

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

  const auto task = Job::ParallelInvoke(
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

  Job::WaitOnTask(Job::TaskSubmit(task));

  for (int i = 0; i < k_DataSize; ++i)
  {
    EXPECT_EQ(example_data[i], 1) << "Each index must be written to exactly once: " << i;
  }
}

// Tests keeping task alive through reference count API
TEST(JobSystemTests, GCReferenceCount)
{
  auto* const long_running_task = Job::TaskMake([](Job::Task*) {
    ThreadSleep(std::chrono::milliseconds(12));
  });

  TaskIncRef(long_running_task);
  TaskSubmit(long_running_task, Job::QueueType::WORKER);

  if (Job::NumWorkers() == 1u)
  {
    WaitOnTask(long_running_task);
  }
  else
  {
    while (!TaskIsDone(long_running_task))
    {
      std::printf("Waiting on long Running task...\n");
      ThreadSleep(std::chrono::milliseconds(1));
    }
  }

  ThreadSleep(std::chrono::milliseconds(12));

  // Task should still be valid, this call should not crash.

  if (TaskIsDone(long_running_task))
  {
    TaskDecRef(long_running_task);
  }
}

// Checks correct ref count API usage.
TEST(JobSystemTests, RefCountAPIUsage)
{
  auto* const long_running_task = Job::TaskMake([](Job::Task*) {
    ThreadSleep(std::chrono::milliseconds(2));
  });

  // First call to `taskIncRef` must be before a submit.
  TaskIncRef(long_running_task);
  TaskSubmit(long_running_task, Job::QueueType::WORKER);

  // Any other calls can be at any time.
  TaskIncRef(long_running_task);

  if (Job::NumWorkers() == 1u)
  {
    WaitOnTask(long_running_task);
  }
  else
  {
    while (!TaskIsDone(long_running_task))
    {
      ThreadSleep(std::chrono::milliseconds(1));
    }
  }

  ThreadSleep(std::chrono::milliseconds(5));

  TaskDecRef(long_running_task);
  TaskDecRef(long_running_task);
}

TEST(JobSystemTests, SPSCQueue)
{
  constexpr auto               backing_storage_capacity = (1 << 23);
  const std::unique_ptr<int[]> backing_storage          = AllocateIntArray(backing_storage_capacity);
  const std::unique_ptr<int[]> queue_result             = AllocateIntArray(backing_storage_capacity * 2);

  Job::SPSCQueue<int> q{};

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

// TODO(SR): Test continuations.

int main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);

  Job::Initialize();
  const int result = RUN_ALL_TESTS();
  Job::Shutdown();

  return result;
}
