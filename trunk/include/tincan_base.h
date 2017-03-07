/*
* ipop-project
* Copyright 2016, University of Florida
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
#ifndef TINCAN_BASE_H_
#define TINCAN_BASE_H_
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <array>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stack>
#include <string>
#include <vector>
namespace tincan
{
using MacAddressType = std::array<uint8_t, 6>;
using IP4AddressType = std::array<uint8_t, 4>;
//using namespace std;
using std::array;
using std::chrono::milliseconds;
using std::chrono::steady_clock;
using std::cout;
using std::endl;
using std::exception;
using std::istringstream;
using std::list;
using std::lock_guard;
using std::make_shared;
using std::make_unique;
using std::map;
using std::milli;
using std::move;
using std::mutex;
using std::ostringstream;
using std::out_of_range;
using std::shared_ptr;
using std::stack;
using std::string;
using std::stringstream;
using std::unique_ptr;
using std::vector;
using std::memcpy;


#define TC_DBG LS_ERROR
///////////////////////////////////////////////////////////////////////////////
static const uint16_t kTincanVerMjr = 2;
//
static const uint16_t kTincanVerMnr = 0;
//
static const uint16_t kTincanVerRev = 0;
//
static const uint8_t kTincanControlVer = 4;
//
static const uint8_t kTincanLinkVer = 1;
//
static const uint16_t kMaxMtuSize = 1500;
//
static const uint16_t kTapHeaderSize = 2;

static const uint16_t kEthHeaderSize = 14;

static const uint16_t kEthernetSize = kEthHeaderSize + kMaxMtuSize;
//
static const uint16_t kTapBufferSize = kTapHeaderSize + kEthernetSize;
//
//Port Allocator Flags
static const uint32_t kFlags = 0;

static const uint8_t kFT_DTF = 0x0A;
static const uint8_t kFT_FWD = 0x0B;
static const uint8_t kFT_ICC = 0x0C;
static const uint16_t kDtfMagic = 0x0A01;
static const uint16_t kFwdMagic = 0x0B01;
static const uint16_t kIccMagic = 0x0C01;

static const char kCandidateDelim = ':';
//
static const char kIceUfrag[] = "_001IPOPICEUFRAG";
//
static const char kIcePwd[] = "_00000001IPOPICEPASSWORD";
//
static const char kLocalHost[] = "127.0.0.1";
//
static const char kLocalHost6[] = "::1";
//
static bool kIpv6Enabled = true;
//
static uint16_t kUdpPort = 5800;
//
static const uint8_t kLinkConcurrentAIO = 1;
//
static const uint32_t kCacheIOMax = 32;

struct TincanParameters
{
public:
  void ParseCmdlineArgs(
    int argc,
    char **args)
  {
    if(argc == 2 && strncmp(args[1], "-v", 2) == 0) {
      kVersionCheck = true;
    }
    else if(argc == 2 && strncmp(args[1], "-h", 2) == 0) {
      kNeedsHelp;
    }
    if(argc == 2 && strncmp(args[1], "-p=", 3) == 0) {
      kUdpPort = (uint16_t)atoi(args[1]+3);
    }
  }

  static bool kVersionCheck;

  static bool kNeedsHelp;

};
#if !defined(TINCAN_MAIN)
extern TincanParameters tp;
#endif
///////////////////////////////////////////////////////////////////////////////
template<typename InputIter>
string ByteArrayToString(
  InputIter first,
  InputIter last,
  uint32_t line_breaks = 0,
  bool use_sep = false,
  char sep = ':',
  bool use_uppercase = true)
{
  assert(sizeof(*first) == 1);
  ostringstream oss;
  oss << std::hex << std::setfill('0');
  if(use_uppercase)
    oss << std::uppercase;
  int i = 0;
  while(first != last)
  {
    oss << std::setw(2) << static_cast<int>(*first++);
    if(use_sep && first != last)
      oss << sep;
    if(line_breaks && !(++i % line_breaks)) oss << endl;
  }
  return oss.str();
}
//Fixme: Doesn't handle line breaks
template<typename OutputIter>
size_t StringToByteArray(
  const string & src,
  OutputIter first,
  OutputIter last,
  bool sep_present = false)
{
  assert(sizeof(*first) == 1);
  size_t count = 0;
  istringstream iss(src);
  char val[3];
  while(first != last && iss.peek() != std::istringstream::traits_type::eof())
  {
    size_t nb = 0;
    iss.get(val, 3);
    (*first++) = (uint8_t)std::stoi(val, &nb, 16);
    count++;
    if(sep_present)
      iss.get();
  }
  return count;
}
///////////////////////////////////////////////////////////////////////////////
//IpOffsets
class IpOffsets
{
public:
  IpOffsets(uint8_t * ip_packet) :
    pkt_(ip_packet)
  {}
  uint8_t* Version()
  {
    return pkt_;
  }
  uint8_t* IpHeaderLen()
  {
    return pkt_;
  }
  uint8_t* TotalLength()
  {
    return &pkt_[2];
  }
  uint8_t* Ttl()
  {
    return &pkt_[8];
  }
  uint8_t* SourceIp()
  {
    return &pkt_[12];
  }
  uint8_t* DestinationIp()
  {
    return &pkt_[16];
  }
  uint8_t* Payload()
  {
    return &pkt_[24];
  }
private:
  uint8_t * pkt_;
};
///////////////////////////////////////////////////////////////////////////////
class EthOffsets
{
public:
  EthOffsets(uint8_t * eth_frame) :
    frm_(eth_frame)
  {}
  uint8_t* DestinationMac()
  {
    return frm_;
  }
  uint8_t* SourceMac()
  {
    return &frm_[6];
  }
  uint8_t* Type()
  {
    return &frm_[12];
  }
  uint8_t* Payload()
  {
    return &frm_[14];
  }
private:
  uint8_t * frm_;
};
} // namespace tincan
#endif // TINCAN_BASE_H_
