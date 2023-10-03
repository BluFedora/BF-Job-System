/******************************************************************************/
/*!
 * @file   initialization_token.hpp
 * @author Shareef Abdoul-Raheem (https://blufedora.github.io/)
 * @brief
 *   Token type used for other subsystems that rely on the job system to verify
 *   that the Job system has been initialized.
 *
 * @copyright Copyright (c) 2023 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#ifndef LIB_FOUNDATION_JOB_INITALIZATION_TOKEN_HPP
#define LIB_FOUNDATION_JOB_INITALIZATION_TOKEN_HPP

namespace Job
{
  /*!
     * @brief
     *   The runtime configuration for the Job System.
     */
  struct JobSystemCreateOptions
  {
    unsigned int num_threads = 0u;  //!< Use 0 to indicate using the number of cores available on the system.
  };

  struct InitializationToken;
}  // namespace Job

namespace bf::job
{
  Job::InitializationToken initialize(const Job::JobSystemCreateOptions& params) noexcept;
}

namespace Job
{
  struct InitializationToken
  {
    unsigned int num_workers_created = 0;

   private:
    InitializationToken(const unsigned int num_workers_created) :
      num_workers_created{num_workers_created}
    {
    }

    friend InitializationToken bf::job::initialize(const JobSystemCreateOptions&) noexcept;
  };

  void PauseProcessor();
  void YieldTimeSlice();

}  // namespace Job

#endif // LIB_FOUNDATION_JOB_INITALIZATION_TOKEN_HPP

/******************************************************************************/
/*
  MIT License

  Copyright (c) 2023 Shareef Abdoul-Raheem

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