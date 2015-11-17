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
ipop version 0x03 for 15.11.0
*/
static const char kIpopVer = 0x03;

static const unsigned short kIpopVerMjr = 15;
static const unsigned short kIpopVerMnr = 11;
static const unsigned short kIpopVerRev = 0;

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
