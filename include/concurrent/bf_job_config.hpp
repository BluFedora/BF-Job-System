/******************************************************************************/
/*!
 * @file   bf_job_config.hpp
 * @author Shareef Raheem (https://blufedora.github.io/)
 * @date   2020-09-03
 * @brief
 *    This header contains configurable constants for this Job System library.
 *    You may edit this file to configure this library at compile time.
 *
 * @copyright Copyright (c) 2020-2024 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#ifndef BF_JOB_CONFIG_HPP
#define BF_JOB_CONFIG_HPP

#include <cstddef> /* size_t */

#ifndef JOB_SYS_ASSERTIONS
#define JOB_SYS_ASSERTIONS 1  //!< Should be turned on during development as it catches API misuse, then for release switched off.
#endif

#ifndef JOB_SYS_DETERMINISTIC_JOB_STEAL_RNG
#define JOB_SYS_DETERMINISTIC_JOB_STEAL_RNG 1  //!< The RNG for work queue stealing will be seeded in the same way.
#endif

namespace Job
{
  // Constants / Configuration

  static constexpr std::size_t k_MainQueueSize       = 256;   //!< The number of tasks that can be contained in the main queue.
  static constexpr std::size_t k_NormalQueueSize     = 1024;  //!< The number of tasks that can be contained in each worker's high priority queue.
  static constexpr std::size_t k_BackgroundQueueSize = 512;   //!< The number of tasks that can be contained in each worker's low priority queue.
  static constexpr std::size_t k_MaxThreadsSupported = 32;    //!< The maximum number of threads that can be created, this is so that the library can be non dynamically allocating.
}  // namespace Job

#endif // BF_JOB_CONFIG_HPP

/******************************************************************************/
/*
  MIT License

  Copyright (c) 2020-2024 Shareef Abdoul-Raheem

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
