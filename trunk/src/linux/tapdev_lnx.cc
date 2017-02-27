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

#if defined (_IPOP_LINUX)
#include "tapdev_lnx.h"
#include "tincan_exception.h"
namespace tincan
{
namespace linux
{
static const char * const TUN_PATH = "/dev/net/tun";

TapDevLnx::TapDevLnx() :
  fd_(-1),
  ip4_config_skt_(-1),
  is_good_(false)
{}

TapDevLnx::~TapDevLnx()
{}

void TapDevLnx::Open(
  const TapDescriptor & tap_desc)
{
  string emsg("The Tap device open operation failed - ");
  //const char* tap_name = tap_desc.interface_name;
  if((fd_ = open(TUN_PATH, O_RDWR)) < 0)
    throw TCEXCEPT(emsg.c_str());
  ifr_.ifr_flags = IFF_TAP | IFF_NO_PI;
  if(tap_desc.interface_name.length() >= IFNAMSIZ)
  {
    emsg.append("the name length is longer than maximum allowed.");
    throw TCEXCEPT(emsg.c_str());
  }
  strncpy(ifr_.ifr_name, tap_desc.interface_name.c_str(), tap_desc.interface_name.length());
  //create the device
  if(ioctl(fd_, TUNSETIFF, (void *)&ifr_) < 0)
  {
    emsg.append("the device could not be created.");
    throw TCEXCEPT(emsg.c_str());
  }

//create a socket to be used when retrieving mac address from the kernel
  if((ip4_config_skt_ = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    emsg.append("a socket bind failed.");
    throw TCEXCEPT(emsg.c_str());
  }

  if((ioctl(ip4_config_skt_, SIOCGIFHWADDR, &ifr_)) < 0)
  {
    emsg.append("retrieving the device mac address failed");
    throw TCEXCEPT(emsg.c_str());
  }
  memcpy(mac_.data(), ifr_.ifr_hwaddr.sa_data, 6);
  SetIpv4Addr(tap_desc.Ip4.c_str(), tap_desc.prefix4, (char*)ip4_.data());
  if(!tap_desc.mtu4)
    Mtu(1500);
  else
    Mtu(tap_desc.mtu4);

  is_good_ = true;
}

void TapDevLnx::Close()
{
  close(fd_);
}

void TapDevLnx::SetIpv4Addr(
  const char* ipaddr,
  unsigned int prefix_len,
  char *my_ipv4)
{
  string emsg("The Tap device Set IP4 Address operation failed");
  struct sockaddr_in socket_address = { .sin_family = AF_INET,.sin_port = 0 };
  if(inet_pton(AF_INET, ipaddr, &socket_address.sin_addr) != 1)
  {
    emsg.append(" - the provided IP4 address could not be resolved.");
    throw TCEXCEPT(emsg.c_str());
  }

  //wrap sockaddr_in struct to sockaddr struct, which is used conventionaly for system calls
  memcpy(&ifr_.ifr_addr, &socket_address, sizeof(struct sockaddr));

  //copies ipv4 address to my my_ipv4. ipv4 address starts at sa_data[2]
  //and terminates at sa_data[5]
  memcpy(ip4_.data(), &ifr_.ifr_addr.sa_data[2], 4);

  //configure tap device with a ipv4 address
  if(ioctl(ip4_config_skt_, SIOCSIFADDR, &ifr_) < 0)
  {
    throw TCEXCEPT(emsg.c_str());
  }

  //get subnet mask intoi network byte order from int representation
  PlenToIpv4Mask(prefix_len, &ifr_.ifr_netmask);


  //configure the tap device with a netmask
  if(ioctl(ip4_config_skt_, SIOCSIFNETMASK, &ifr_) < 0)
  {
    LOG_F(LS_VERBOSE) << " failed to set ipv4 netmask on tap device";
  }

}

void TapDevLnx::PlenToIpv4Mask(
  unsigned int prefix_len,
  struct sockaddr * writeback)
{
  uint32_t net_mask_int = ~(0u) << (32 - prefix_len);
  net_mask_int = htonl(net_mask_int);
  struct sockaddr_in netmask = {
        .sin_family = AF_INET,
        .sin_port = 0
  };
  struct in_addr netmask_addr = { .s_addr = net_mask_int };
  netmask.sin_addr = netmask_addr;
  //wrap sockaddr_in into sock_addr struct
  memcpy(writeback, &netmask, sizeof(struct sockaddr));
}


/**
 * Given some flags to enable and disable, reads the current flags for the
 * network device, and then ensures the high bits in enable are also high in
 * ifr_flags, and the high bits in disable are low in ifr_flags. The results are
 * then written back. For a list of valid flags, read the "SIOCGIFFLAGS,
 * SIOCSIFFLAGS" section of the 'man netdevice' page. You can pass `(short)0` if
 * you don't want to enable or disable any flags.
 */

void TapDevLnx::SetFlags(
  short enable,
  short disable)
{
  string emsg("The TAP device set flags operation failed");
  if(ioctl(ip4_config_skt_, SIOCGIFFLAGS, &ifr_) < 0)
  {
    throw TCEXCEPT(emsg.c_str());
  }
  //set or unset the right flags
  ifr_.ifr_flags |= enable;
  ifr_.ifr_flags &= ~disable;
  //write back the modified flag states
  if(ioctl(ip4_config_skt_, SIOCSIFFLAGS, &ifr_) < 0)
    throw TCEXCEPT(emsg.c_str());
}

uint32_t TapDevLnx::Read(AsyncIo& aio_rd)
{
  if(!is_good_)
    return 1; //indicates a failure to setup async operation
  TapPayload *tp_ = new TapPayload;
  tp_->aio_ = &aio_rd;
  reader_.Post(this, MSGID_READ, tp_);
  return 0;
}

uint32_t TapDevLnx::Write(AsyncIo& aio_wr)
{
  if(!is_good_)
    return 1; //indicates a failure to setup async operation
  TapPayload *tp_ = new TapPayload;
  tp_->aio_ = &aio_wr;
  writer_.Post(this, MSGID_WRITE, tp_);
  return 0;
}

void TapDevLnx::Mtu(uint16_t mtu)
{
  ifr_.ifr_mtu = mtu;
  if(ioctl(ip4_config_skt_, SIOCSIFMTU, &ifr_) < 0)
  {
    throw TCEXCEPT("The TAP Device set MTU operation failed.");
  }
}

uint16_t TapDevLnx::TapDevLnx::Mtu()
{
  return ifr_.ifr_mtu;
}

MacAddressType TapDevLnx::MacAddress()
{
  return mac_;
}

  /**
  * Sets and unsets some common flags used on a TAP device, namely, it sets the
  * IFF_NOARP flag, and unsets IFF_MULTICAST and IFF_BROADCAST. Notably, if
  * IFF_NOARP is not set, when using an IPv6 TAP, applications will have trouble
  * routing their data through the TAP device (Because they'd expect an ARP
  * response, which we aren't really willing to provide).
  */
void TapDevLnx::Up()
{
  SetFlags(IFF_UP | IFF_RUNNING, 0);
  reader_.Start();
  writer_.Start();
}

void TapDevLnx::Down()
{
  //TODO: set the appropriate flags
  SetFlags(0, IFF_UP | IFF_RUNNING);
  LOG_F(LS_INFO) << "TAP DOWN";
}


void TapDevLnx::OnMessage(Message * msg)
{
  switch(msg->message_id)
  {
  case MSGID_READ:
  {
    AsyncIo* aio_read = ((TapPayload*)msg->pdata)->aio_;
    int nread = read(fd_, aio_read->BufferToTransfer(), aio_read->BytesToTransfer());


    if(nread < 0)
    {
      LOG_F(LS_WARNING) << "A TAP Read operation failed.";
      aio_read->good_ = false;
    }
    else
    {
      aio_read->good_ = true;
      aio_read->BytesTransferred(nread);
      read_completion_(aio_read);
    }
  }
  break;

  case MSGID_WRITE:
  {
    AsyncIo* aio_write = ((TapPayload*)msg->pdata)->aio_;
    int nwrite = write(fd_, aio_write->BufferToTransfer(), aio_write->BytesToTransfer());
    if(nwrite < 0)
    {
      LOG_F(LS_WARNING) << "A TAP Write operation failed.";
      aio_write->good_ = false;
    }
    else
    {
      aio_write->good_ = true;
      aio_write->BytesTransferred(nwrite);
      write_completion_(aio_write);
    }
  }
  break;
  }
}

IP4AddressType
TapDevLnx::Ip4()
{
  return ip4_;
}
} // linux
} // tincan
#endif // _IPOP_LINUX
