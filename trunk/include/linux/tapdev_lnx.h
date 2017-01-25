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
#ifndef TINCAN_TAPDEV_LNX_H_
#define TINCAN_TAPDEV_LNX_H_
#if defined (_IPOP_LINUX)

#include "async_io.h"
#include "tapdev_inf.h"
#include "tincan_base.h"

#include "webrtc/base/logging.h"
#include "webrtc/base/thread.h"
#include "webrtc/base/sigslot.h"

#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <net/route.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace tincan
{

namespace linux
{
using rtc::Message;
using rtc::MessageData;
using rtc::MessageHandler;
class TapDevLnx :
  public TapDevInf,
  public MessageHandler
{
public:
  TapDevLnx();
  ~TapDevLnx();
  sigslot::signal1<AsyncIo *> read_completion_;
  sigslot::signal1<AsyncIo *> write_completion_;
  void Open(
    const TapDescriptor & tap_desc) override;
  void Close() override;
  void Mtu(uint16_t);
  uint32_t Read(AsyncIo& aio_rd) override;
  uint32_t Write(AsyncIo& aio_wr) override;
  uint16_t Mtu() override;
  void Up() override;
  void Down() override;
  MacAddressType& MacAddress() override;
  enum MSG_ID
  {
    MSGID_READ,
    MSGID_WRITE,
  };
  class TapPayload :
    public MessageData
  {
  public:
    AsyncIo * aio_;
  };
private:
  rtc::Thread reader_;
  rtc::Thread writer_;
  struct ifreq ifr_;
  int fd_;
  int ip4_config_skt_;
  char ip4_[4];
  MacAddressType mac_;
  //uint16_t mtu4_;
  bool is_good_;
  void SetIpv4Addr(const char* a, unsigned int b, char *c);
  void SetFlags(short a, short b);
  void PlenToIpv4Mask(unsigned int a, struct sockaddr *b);
  void OnMessage(Message * msg) override;
};
}
}
#endif //_IPOP_LINUX
#endif 
