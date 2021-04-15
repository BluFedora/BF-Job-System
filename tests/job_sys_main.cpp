#include "bf/JobSystemExt.hpp"

#include <cassert>
#include <iostream>

static constexpr int k_DataSize = 100000;

static int s_ExampleData[k_DataSize] = {};

// Print the first 20 items for brevity
static void printFirst20Items()
{
  for (int i = 0; i < 20; ++i)
  {
    std::printf("data[%i] = %i\n", i, s_ExampleData[i]);
  }
}

int main()
{
  if (!bf::job::initialize())
  {
    return 1;
  }

  // Initialize Dummy Data
  for (int i = 0; i < k_DataSize; ++i)
  {
    s_ExampleData[i] = i;
  }

  const int multiplier = 5;

  std::printf("Before:\n");
  printFirst20Items();

  bf::job::Task* const t = bf::job::parallel_for(
   s_ExampleData, k_DataSize, bf::job::CountSplitter{6}, [multiplier](int* data, std::size_t data_size) {
     for (std::size_t i = 0; i < data_size; ++i)
     {
       data[i] = data[i] * multiplier;
     }
   });

  bf::job::taskSubmit(t);

  bf::job::waitOnTask(t);

  std::printf("After:\n");
  printFirst20Items();

  // Check that the jobs finished working on all items.
  for (int i = 0; i < k_DataSize; ++i)
  {
    assert(s_ExampleData[i] == i * multiplier);
  }

  bf::job::shutdown();

  return 0;
}
