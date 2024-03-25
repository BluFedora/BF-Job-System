# BluFedora Job System Library

This is a C++17 library for handling of Tasks / Jobs in a multi-threaded environment.

## Examples

Minimal Example

```cpp

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
   Job::Initialize(Job::MemRequirementsForConfig({}));

  // Initialize Dummy Data
  for (int i = 0; i < k_DataSize; ++i)
  {
    s_ExampleData[i] = i;
  }

  std::printf("Before:\n");
  printFirst20Items();

  auto* t = Job::ParallelFor(
   s_ExampleData, 
   k_DataSize, 
   Job::CountSplitter{6}, 
   [](int* data, std::size_t data_size) {
     for (std::size_t i = 0; i < data_size; ++i)
     {
       data[i] = data[i] * 5;
     }
   });

  Job::TaskSubmit(t);

  Job::WaitOnTask(t);

  std::printf("After:\n");
  printFirst20Items();

  // Check that the jobs finished working on all items.
  for (int i = 0; i < k_DataSize; ++i)
  {
    assert(s_ExampleData[i] == i * 5);
  }

  Job::Shutdown();

  return 0;
}

```

## Architecture

### Task

A `Task` is a single unit of work that can be scheduled by the Job System. Each `Task` has a total sizeof of 128bytes (2 * hardware interference size)
with some of the bytes taken by essential bookkeeping date then the rest used for user storage.

A `Task`s can be added as a child of another task, this means that when you wait on the parent `Task` then it will wait for all child `Task` as well.

### Queues

A Queue hold a list of `Task`s waiting to be executed. There are four different types of queues.

- `MAIN` This queue has a guarantee that the task will be run on the main thread.
- `NORMAL` Slightly lower priority than 'QueueType::HIGH'.
- `BACKGROUND` This queue has a guarantee that the task will never be run on the main thread.

## Dependencies

- C++17 or higher

## Libraries Used

- [PCG Random](https://www.pcg-random.org/)
