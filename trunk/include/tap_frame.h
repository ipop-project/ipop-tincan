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
#ifndef TINCAN_TAP_FRAME_H_
#define TINCAN_TAP_FRAME_H_
#include "tincan_base.h"
#include "async_io.h"
namespace tincan
{
/*
The TapFrameBuffer (TFB) is the byte container for a tincan frame's data. This
includes the tincan specific headers as well as the payload data received from
the TAP device or tincan link. A TFB facilitates
decoupling of the raw data from the meta-data required to manage it.
*/
class TapFrameBuffer
{
  friend class TapFrame;
  friend class TapFrameCache;
  friend class TapFrameProperties;
  TapFrameBuffer() = default;
  ~TapFrameBuffer() = default;
  uint8_t fb_[kTapBufferSize];
};

/*
A TapFrame encapsulates a TapFrameBuffer and defines the control and access
semantics for that type. A single TapFrame can own and manage multiple TFBs
over its life time. It provides easy access to commonly used offsets and
indexing capabilty throught bytes [0..Capacity).
There are no copy semantics but move semantics are provided to support single
ownership of the TBF.
*/
class  TapFrame : virtual public AsyncIo
{
  friend class TapFrameCache;
  friend class TapFrameProperties;
public:
  explicit TapFrame(bool alloc_tfb = false);

  TapFrame(const TapFrame & rhs);

  TapFrame(TapFrame && rhs);

  //Copies the specifed amount of data into the TFB.
  TapFrame(uint8_t* data, uint32_t len);

  ~TapFrame();

  //Copies the TFB from the rhs frame to the lhs
  TapFrame &operator= (TapFrame & rhs);
  //Moves the TFB from the rhs frame to the lhs
  TapFrame &operator= (TapFrame && rhs);

  //Compares based on the pointer address of the TFB
  bool operator==(const TapFrame & rhs) const;

  bool operator!=(const TapFrame & rhs) const;

  bool operator !() const;

  //Used to byte address into the payload
  uint8_t & operator[](uint32_t index);

  const uint8_t & operator[](uint32_t const index) const;

  TapFrame & Initialize();

  const uint8_t * EthernetHeader() const;

  uint8_t * EthernetHeader();

  uint8_t * begin();

  uint8_t * end();

  uint8_t * EthernetPl();

  /*
  Capacity is the maximum size ie., of the raw buffer
  */
  uint32_t Capacity() const;

  void Dump(const string & label);

 private:
   TapFrameBuffer * tfb_;
};

/*
WIP:
Holds a fixed pool of TFBs and acts as a custom allocator for the
TapFrameBuffer type.
*/
class TapFrameCache
{
public:
  TapFrameCache();
  ~TapFrameCache();
  uint32_t Reservation(size_t entity);
  uint32_t CancelReservation(size_t entity);
  TapFrame * Acquire();
  TapFrame * Acquire(
    const uint8_t * data,
    uint32_t data_len);
  void Reclaim(TapFrame* tf);
  bool IsOverProvisioned();

private:
  TapFrame * AcquireMustSucceed();
  uint32_t reservation_count_;
  const uint32_t allocation_factor_;
  const uint32_t max_allocation_;
  uint32_t commitment_;
  const uint32_t high_threshold_;
  stack<TapFrame*> available_;
  vector<TapFrame> master_;
  mutex cache_mtx_;
#if !defined(NDEBUG)
  uint32_t realloc_count;
#endif
};


///////////////////////////////////////////////////////////////////////////////
//IpOffsets
///////////////////////////////////////////////////////////////////////////////
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
uint8_t* Data()
{
  return &pkt_[24];
}
private:
  uint8_t * pkt_;
};

///////////////////////////////////////////////////////////////////////////////
//TapFrameProperties is a ready only accessor class for querying compound
//properties of the TFB.
///////////////////////////////////////////////////////////////////////////////
class TapFrameProperties
{
public:
  TapFrameProperties(TapFrame & tf) :
    tf_(tf),
    ipp_(tf.EthernetPl())
  {}

  bool IsIccMsg() const
  {
    return memcmp(tf_.EthernetHeader(), &kIccMagic,
      sizeof(kIccMagic)) == 0;
  }
  bool IsFwdMsg() const
  {
    return memcmp(tf_.EthernetHeader(), &kFwdMagic,
      sizeof(kFwdMagic)) == 0;
  }
  bool IsIp4()
  {
    const uint8_t* eth = tf_.EthernetHeader();
    return *(uint16_t*)&eth[12] == 0x0008 && *ipp_.Version() >> 4 == 4;
  }

  bool IsIp6()
  {
    return *ipp_.Version() >> 4 == 6;
  }

  bool IsArpRequest() const
  {
    const uint8_t* eth = tf_.EthernetHeader();
    return (*(uint16_t*)&eth[12]) == 0x0608 && eth[21] == 0x01;
  }

  bool IsArpResponse() const
  {
    const uint8_t* eth = tf_.EthernetHeader();
    return (*(uint16_t*)&eth[12]) == 0x0608 && eth[21] == 0x02;
  }

  int CompareDestinationIp4(uint32_t ip4_addr)
  {
    return memcmp(ipp_.DestinationIp(), &ip4_addr, 4);
  }

  bool IsEthernetBroadcast() const
  {
    uint64_t b = 0xffffffffffff;
    return memcmp(tf_.EthernetHeader(), &b, 6) == 0;
  }

  uint32_t DestinationIp4Address()
  {
    return *(uint32_t*)ipp_.DestinationIp();
  }

  MacAddressType & DestinationMac()
  {
    return *(MacAddressType *)(tf_.EthernetHeader());
  }

private:
  TapFrame & tf_;
  IpOffsets ipp_;
};

class TapArp4
{
public:
  TapArp4(uint8_t * eth_data)
  {
    buf_ = eth_data;
    //if((*(uint16_t*)&eth_header[12]) == 0x0608){
    //}
  }
  void IsRquest(){}

  void IsReply()
  {}

  uint32_t DestinationIp()
  {
      return *(uint32_t*)(&buf_[24]);
  }

  MacAddressType & DestinationMac()
  {
    return *(MacAddressType*)(&buf_[18]);
  }
  
  uint32_t SourceIp()
  {
    return *(uint32_t*)(&buf_[14]);
  }

  MacAddressType & SourceMac()
  {
    return *(MacAddressType*)(&buf_[8]);
  }
private:
  uint8_t * buf_;
};
} //namespace tincan
#endif  // TINCAN_TAP_FRAME_H_
