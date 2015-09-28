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
#endif //if defined(LINUX)

namespace tincan {

/*
ipop version 0x01 for 14.01.1
ipop version 0x02 for 14.01.2
*/
static const char kIpopVer = 0x02;

/*
Tincan Control : control message between controller and tincan
Tincan Packet  : data packet forward from/to controllers
*/
static const char kTincanControl = 0x01;
static const char kTincanPacket = 0x02;
static const char kICCControl = 0x03; //Intercontroller connection header
static const char kICCPacket = 0x04; //Intercontroller connection header

static const int kTincanVerOffset = 0;
static const int kTincanMsgTypeOffset = 1;

static const int kTincanHeaderSize = 2;
static const int kICCMacOffset = 5;

class CurrentTime {
  friend std::ostream & operator << (std::ostream &, const CurrentTime &);
};

} //namespace tincan

#endif //ifndef TINCAN_UTILS_H_
