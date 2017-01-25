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
#ifndef TINCAN_VIRTUAL_NETWORK_H_
#define TINCAN_VIRTUAL_NETWORK_H_
#include "tincan_base.h"
#pragma warning( push )
#pragma warning(disable:4996)
#pragma warning(disable:4100)
#include "webrtc/base/network.h"
#include "webrtc/base/sslidentity.h"
#include "webrtc/base/thread.h"
#include "webrtc/base/sigslot.h"
#include "webrtc/base/json.h"
#pragma warning( pop )
#include "async_io.h"
#include "controller_handle.h"
#include "peer_network.h"
#include "tapdev.h"
#include "tap_frame.h"
#include "tincan_exception.h"
#include "vnet_descriptor.h"
#include "virtual_link.h"

namespace tincan
{
class VirtualNetwork :
  public sigslot::has_slots<>,
  public MessageHandler
{
public:
  enum MSG_ID
  {
    MSGID_CREATE_LINK,
    MSGID_START_CONNECTION,
    MSGID_TRANSMIT,
    MSGID_SEND_ICC,
    MSGID_END_CONNECTION,
  };
  class IccMsgData : public MessageData
  {
  public:
    shared_ptr<VirtualLink> vl;
    unique_ptr<TapFrame> tf;
  };
  class TransmitMsgData : public MessageData
  {
  public:
    shared_ptr<VirtualLink> vl;
    unique_ptr<TapFrame> tf;
  };
  class VlinkMsgData : public MessageData
  {
  public:
    shared_ptr<VirtualLink> vl;
  };
  class CreateVlinkMsgData : public MessageData
  {
  public:
    unique_ptr<PeerDescriptor> peer_desc;
    unique_ptr<VlinkDescriptor> vlink_desc;
    rtc::Event msg_event;
    CreateVlinkMsgData() : msg_event(false, false)
    {}
    ~CreateVlinkMsgData() = default;
  };
  struct IccMessage
  {
    void MessageData(
      uint8_t * msg,
      uint16_t len)
    {
      if(len > kMaxIccMsgLen)
        throw TCEXCEPT("The create ICC message failed, the message length is greater than maximum allowed.");

      magic_ = kIccMagic;
      data_len_ = len;
      memmove(data_, msg, data_len_);
    }
    uint8_t * MessageData()
    {
      return data_;
    }
    uint32_t MessageLength() //length of entire message
    {
      return sizeof(magic_) + sizeof(data_len_) + data_len_;
    }
    uint16_t magic_;
    uint16_t data_len_; // number of occupied bytes in data_
    uint8_t data_[kMaxIccMsgLen];
  };
   VirtualNetwork(
     unique_ptr<VnetDescriptor> descriptor,
     IpopControllerLink & ctrl_handle);

  ~VirtualNetwork();
  void Configure();
  void Start();
  void Shutdown();
  void StartTunnel(
    VirtualLink & vlink);
  void DestroyTunnel(
    VirtualLink & vlink);
  //
  //Creates the listening endpoint of vlink and returns its candidate address
  //set for the connection.
  shared_ptr<VirtualLink> CreateVlinkEndpoint(
    unique_ptr<PeerDescriptor> peer_desc,
    unique_ptr<VlinkDescriptor> vlink_desc);
  //
  //Uses the remote's candidate set and actively attempts to connect to it.
  void ConnectToPeer(
    unique_ptr<PeerDescriptor> peer_desc,
    unique_ptr<VlinkDescriptor> vlink_desc);

  void RemovePeerConnection(
    const string & peer_uid);

  VnetDescriptor & Descriptor();

  string Name();
  string MacAddress();
  string Fingerprint();
  void IgnoredNetworkInterfaces(
    const vector<string>& ignored_list);
  
  void QueryNodeInfo(
    const string & node_uid,
    Json::Value & node_info);

  void SendIcc(
    const string & recipient_uid,
    const string & data);

  //
  //FrameHandler implementation
  void ProcessIncomingFrame(
    const uint8_t * data,
    uint32_t data_len,
    VirtualLink & vlink);

  void ProcessIncomingFrameL2(
    const uint8_t * data,
    uint32_t data_len,
    VirtualLink & vlink);

  //
  //AsyncIOComplete
  void TapReadComplete(
    AsyncIo * aio_rd);

  void TapReadCompleteL2(
    AsyncIo * aio_rd);

  void TapWriteCompleteL2(
    AsyncIo * aio_wr);

  void TapWriteComplete(
    AsyncIo * aio_wr);
  //
  //MessageHandler overrides
  void OnMessage(Message* msg) override;
private:
  unique_ptr<VirtualLink> CreateVlink(
    unique_ptr<VlinkDescriptor> vlink_desc,
    unique_ptr<PeerDescriptor> peer_desc);
  
  rtc::BasicNetworkManager net_manager_;
  unique_ptr<rtc::SSLIdentity> sslid_;
  unique_ptr<rtc::SSLFingerprint> local_fingerprint_;
  unique_ptr<VnetDescriptor> descriptor_;
  TapDev * tdev_;
  PeerNetwork * peer_network_;
  IpopControllerLink & ctrl_link_;//TODO: change to shared_ptr<IpopControllerLink> ctrl_link_;
  rtc::Thread worker_;
  int32_t link_count_;
  mutex vn_mtx;
  TapFrameCache tf_cache_;
};
}  // namespace tincan
#endif  // TINCAN_VIRTUAL_NETWORK_H_
