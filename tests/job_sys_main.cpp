
#include "bf/JobSystemExt.hpp"

#include <gtest/gtest.h>

#include <memory>   // unique_ptr
#include <numeric>  // iota


TEST(JobSystemTests, BasicParallelForRange)
{
  static constexpr int          k_DataSize   = 1000000;
  static constexpr int          k_DataSplit  = 250;
  const std::unique_ptr<bool[]> example_data = std::make_unique<bool[]>(k_DataSize);

  std::fill_n(example_data.get(), k_DataSize, false);

  bf::job::Task* const task = bf::job::parallel_for(
   0, k_DataSize, bf::job::CountSplitter{k_DataSplit}, [&example_data](bf::job::Task* parent, const bf::job::IndexRange index_range) {
     for (const std::size_t i : index_range)
     {
       example_data[i] = true;
     }
   });

  bf::job::waitOnTask(bf::job::taskSubmit(task));

  for (int i = 0; i < k_DataSize; ++i)
  {
    EXPECT_EQ(example_data[i], true) << "Failed to write to index " << i;
  }
}

TEST(JobSystemTests, BasicParallelForArray)
{
  static constexpr int k_DataSize  = 100000;
  static constexpr int k_DataSplit = 6;

  const int multiplier = 5;

  const std::unique_ptr<int[]> example_data = std::make_unique<int[]>(k_DataSize);

  std::iota(example_data.get(), example_data.get() + k_DataSize, 0);

  bf::job::Task* const task = bf::job::parallel_for(
   example_data.get(), k_DataSize, bf::job::CountSplitter{k_DataSplit}, [multiplier](int* data, std::size_t data_count) {
     EXPECT_LE(data_count, k_DataSplit);

     for (std::size_t i = 0; i < data_count; ++i)
     {
       data[i] = data[i] * multiplier;
     }
   });

  bf::job::waitOnTask(bf::job::taskSubmit(task));

  for (int i = 0; i < k_DataSize; ++i)
  {
    EXPECT_EQ(example_data[i], i * multiplier) << "Data incorrect at index " << i;
  }
}

TEST(JobSystemTests, BasicParallelInvoke)
{
  static constexpr int         k_DataSize   = 1000000;
  const std::unique_ptr<int[]> example_data = std::make_unique<int[]>(k_DataSize);

  std::fill_n(example_data.get(), k_DataSize, 0);

  const auto task = bf::job::parallel_invoke(
   nullptr,
   [&](bf::job::Task* task) {
     for (const std::size_t i : bf::job::IndexRange{0, k_DataSize / 2})
     {
       ++example_data[i];
     }
   },
   [&](bf::job::Task* task) {
     for (const std::size_t i : bf::job::IndexRange{k_DataSize / 2, k_DataSize})
     {
       ++example_data[i];
     }
   });

  bf::job::waitOnTask(bf::job::taskSubmit(task));

  for (int i = 0; i < k_DataSize; ++i)
  {
    EXPECT_EQ(example_data[i], 1) << "Each index must be written to exactly once: " << i;
  }
}

// TODO(SR): Test continuations.
// TODO(SR): Test Dependecies / parent child relationships.

int main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);

  bf::job::initialize();
  const int result = RUN_ALL_TESTS();
  bf::job::shutdown();

  return result;
}
