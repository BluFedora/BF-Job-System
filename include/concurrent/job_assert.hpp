/******************************************************************************/
/*!
 * @file   job_assert.hpp
 * @author Shareef Abdoul-Raheem (https://blufedora.github.io/)
 * @brief
 *   Assertion macro for this library.
 *
 * @copyright Copyright (c) 2024 Shareef Abdoul-Raheem
 */
/******************************************************************************/
#ifndef JOB_ASSERT_HPP
#define JOB_ASSERT_HPP

#ifndef JOB_SYS_ASSERTIONS
#define JOB_SYS_ASSERTIONS 1  //!< Should be turned on during development as it catches API misuse, then for release switched off.
#endif

#if JOB_SYS_ASSERTIONS
namespace Job
{
  namespace detail
  {
    void assertHandler(const bool condition, const char* const filename, const int line_number, const char* const msg);
  }
}  // namespace Job

#define JobAssert(expr, msg) ::Job::detail::assertHandler((expr), __FILE__, __LINE__, msg)
#else
#define JobAssert(expr, msg)
#endif

#endif  // JOB_SPSC_QUEUE_HPP

/******************************************************************************/
/*
  MIT License

  Copyright (c) 2024 Shareef Abdoul-Raheem

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
