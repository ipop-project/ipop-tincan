/*
 * ipop-tincan
 * Copyright 2015, University of Florida
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
*/

#if defined(LINUX)
#include "tincan_utils.h"

namespace tincan {

  std::ostream & operator << (std::ostream & out, const CurrentTime & ct) {

    struct timeval time;
    struct tm * ttm = NULL;
    gettimeofday(&time, NULL);
    ttm = localtime(&time.tv_sec);

    out << "[" << std::setfill('0') << std::setw(2) << ttm->tm_mon + 1
        << "/" << std::setfill('0') << std::setw(2) << ttm->tm_mday
        << " " << std::setfill('0') << std::setw(2) << ttm->tm_hour
        << ":" << std::setfill('0') << std::setw(2) << ttm->tm_min
        << ":" << std::setfill('0') << std::setw(2) << ttm->tm_sec
        << "." << std::setfill('0') << std::setw(6) << time.tv_usec
        << "] ";

    return out;
  }

} //namespace tincan
#endif //if defined(LINUX)
