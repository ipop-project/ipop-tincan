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

#ifndef TINCAN_UTILS_H_
#define TINCAN_UTILS_H_
#pragma once

#if defined(LINUX)
#define LOG_TS(sev) LOG(sev) << tincan::CurrentTime() << __FUNCTION__ << ": "
#else
#define LOG_TS(sev) LOG_F(sev) <<  __FUNCTION__ << ": "
#endif //if defined(LINUX)

#if defined(LINUX)
#include <iomanip>
#include <iostream>
#include <sys/time.h>

namespace tincan {

/*
ipop_ver 0x01 for 14.01.1
ipop_ver 0x02 for 14.01.2
*/
static const char ipop_ver = 0x02;

/*
tincan_control : control message between controller and tincan
tincan_packet  : data packet forward from/to controllers
*/
static const char tincan_control = 0x01;
static const char tincan_packet = 0x02;

static const int tincan_header_size = 2;

class CurrentTime {
  friend std::ostream & operator << (std::ostream &, const CurrentTime &);
};

} //namespace tincan

#endif //if defined(LINUX)
#endif //ifndef TINCAN_UTILS_H_
