/*
 * ipop-tincan
 * Copyright 2013, University of Florida
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
