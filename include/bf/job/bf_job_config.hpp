/******************************************************************************/
/*!
 * @file   bf_job_config.hpp
 * @author Shareef Abdoul-Raheem (https://blufedora.github.io/)
 * @brief
 *    This header contains configurable constants for this Job System library.
 *    You may edit this file to configure this library at compile time.
 *
 * @version 0.0.1
 * @date    2020-09-03
 *
 * @copyright Copyright (c) 2020-2021 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#ifndef BF_JOB_CONFIG_HPP
#define BF_JOB_CONFIG_HPP

#include <cstddef> /* size_t */

namespace bf
{
  namespace job
  {
    // Constants / Configuration

    static constexpr std::size_t k_MainQueueSize               = 128;   //!< The number of tasks that can be contained in the main queue.
    static constexpr std::size_t k_HiPriorityQueueSize         = 2048;  //!< The number of tasks that can be contained in each worker's high priority queue.
    static constexpr std::size_t k_BackgroundPriorityQueueSize = 512;   //!< The number of tasks that can be contained in each worker's low priority queue.
    static constexpr std::size_t k_MaxTaskContinuations        = 16;    //!< The max amount of continuations a single task may have.
    static constexpr std::size_t k_MaxThreadsSupported         = 32;    //!< The maximum number of threads that can be created, this is so that the library can be non dynamically allocating.

  }  // namespace job
}  // namespace bf

#endif /* BF_JOB_CONFIG_HPP */
