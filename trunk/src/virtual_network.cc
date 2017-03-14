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
#include "virtual_network.h"
#include "webrtc/base/base64.h"
#include "tincan_control.h"
namespace tincan
{
VirtualNetwork::VirtualNetwork(
  unique_ptr<VnetDescriptor> descriptor,
  shared_ptr<IpopControllerLink> ctrl_handle) :
  tdev_(nullptr),
  peer_network_(nullptr),
  descriptor_(move(descriptor)),
  ctrl_link_(ctrl_handle)
{
  tdev_ = new TapDev();
  peer_network_ = new PeerNetwork(descriptor->name);
}

VirtualNetwork::~VirtualNetwork()
{
  delete peer_network_;
  delete tdev_;
}

void
VirtualNetwork::Configure()
{
  TapDescriptor ip_desc = {
    descriptor_->name,
    descriptor_->vip4,
    descriptor_->prefix4,
    descriptor_->mtu4
  };
  //initialize the Tap Device
  tdev_->Open(ip_desc);
  //create X509 identity for secure connections
  string sslid_name = descriptor_->name + descriptor_->uid;
  sslid_.reset(SSLIdentity::Generate(sslid_name, rtc::KT_RSA));
  if(!sslid_)
    throw TCEXCEPT("Failed to generate SSL Identity");
  local_fingerprint_.reset(
    SSLFingerprint::Create(rtc::DIGEST_SHA_1, sslid_.get()));
  if(!local_fingerprint_)
    throw TCEXCEPT("Failed to create the local finger print");
}

void
VirtualNetwork::Start()
{
  worker_.Start();
  if(descriptor_->l2tunnel_enabled)
  {
    tdev_->read_completion_.connect(this, &VirtualNetwork::TapReadCompleteL2);
    tdev_->write_completion_.connect(this, &VirtualNetwork::TapWriteCompleteL2);
  }
  else
  {
    tdev_->read_completion_.connect(this, &VirtualNetwork::TapReadComplete);
    tdev_->write_completion_.connect(this, &VirtualNetwork::TapWriteComplete);
  }
  tdev_->Up();
  peer_net_thread_.Start(peer_network_);
}

void
VirtualNetwork::Shutdown()
{
  tdev_->Down();
  tdev_->Close();
  peer_net_thread_.Stop();
}

void
VirtualNetwork::StartTunnel(
  VirtualLink & vlink)
{
  lock_guard<mutex> lg(vn_mtx);
  for(uint8_t i = 0; i < kLinkConcurrentAIO; i++)
  {
    unique_ptr<TapFrame> tf = make_unique<TapFrame>();
    tf->Initialize();
    tf->BufferToTransfer(tf->Payload());
    tf->BytesToTransfer(tf->PayloadCapacity());
    tdev_->Read(*tf.release());
  }
}

void
VirtualNetwork::DestroyTunnel(
  VirtualLink & vlink)
{
}

void
VirtualNetwork::UpdateRoute(
  MacAddressType mac_dest,
  MacAddressType mac_path)
{
  peer_network_->UpdateRoute(mac_dest, mac_path);
}

unique_ptr<VirtualLink>
VirtualNetwork::CreateVlink(
  unique_ptr<VlinkDescriptor> vlink_desc,
  unique_ptr<PeerDescriptor> peer_desc)
{
  vlink_desc->stun_addr = descriptor_->stun_addr;
  vlink_desc->turn_addr = descriptor_->turn_addr;
  vlink_desc->turn_user = descriptor_->turn_user;
  vlink_desc->turn_pass = descriptor_->turn_pass;
  unique_ptr<VirtualLink> vl = make_unique<VirtualLink>(
    move(vlink_desc), move(peer_desc));
  scoped_ptr<SSLIdentity> sslid_copy(sslid_->GetReference());
  vl->Initialize(descriptor_->uid, net_manager_, move(sslid_copy),
    *local_fingerprint_.get());
  if(descriptor_->l2tunnel_enabled)
  {
    vl->SignalMessageReceived.connect(this, &VirtualNetwork::VlinkReadCompleteL2);
  }
  else
  {
    vl->SignalMessageReceived.connect(this, &VirtualNetwork::ProcessIncomingFrame);
  }
  vl->SignalLinkReady.connect(this, &VirtualNetwork::StartTunnel);
  vl->SignalLinkBroken.connect(this, &VirtualNetwork::DestroyTunnel);
  return move(vl);
}

shared_ptr<VirtualLink>
VirtualNetwork::CreateVlinkEndpoint(
  unique_ptr<PeerDescriptor> peer_desc,
  unique_ptr<VlinkDescriptor> vlink_desc)
{
  LOG_F(LS_VERBOSE) << "Attempting to create a new virtual link for node "
    << peer_desc->uid;
  shared_ptr<VirtualLink> vl;
  MacAddressType mac;
  StringToByteArray(peer_desc->mac_address, mac.begin(), mac.end());
  if(peer_network_->IsAdjacent(mac))
  {
    vl = peer_network_->GetVlink(mac);
  }
  else
  {
    // create vlink using the worker thread
    CreateVlinkMsgData md;
    md.peer_desc = move(peer_desc);
    md.vlink_desc = move(vlink_desc);
    worker_.Post(this, MSGID_CREATE_LINK, &md);
    //block until it ready
    md.msg_event.Wait(MessageQueue::kForever);
    //grab it from the peer_network
    if (peer_network_->IsAdjacent(mac))
      vl = peer_network_->GetVlink(mac);
    else throw TCEXCEPT("The CreateVlinkEndpoint operation failed.");
  }
  return vl;
}

void
VirtualNetwork::ConnectToPeer(
  unique_ptr<PeerDescriptor> peer_desc,
  unique_ptr<VlinkDescriptor> vlink_desc)
{
  shared_ptr<VirtualLink> vl;
  MacAddressType mac;
  StringToByteArray(peer_desc->mac_address, mac.begin(), mac.end());
  if(peer_network_->IsAdjacent(mac))
  {
    vl = peer_network_->GetVlink(mac);
    vl->PeerCandidates(peer_desc->cas);
  }
  else
  {
    // create vlink using the worker thread
    CreateVlinkMsgData md;
    md.peer_desc = move(peer_desc);
    md.vlink_desc = move(vlink_desc);
    worker_.Post(this, MSGID_CREATE_LINK, &md);
    //block until it ready
    md.msg_event.Wait(Event::kForever);
    vl = peer_network_->GetVlink(mac);
  }
  if(vl)
  {
    VlinkMsgData * md = new VlinkMsgData;
    md->vl = vl;
    worker_.Post(this, MSGID_START_CONNECTION, md);
  }
  else throw TCEXCEPT("The ConnectToPeer operation failed, no virtual link was"
    " found in the peer network.");
}

void VirtualNetwork::RemovePeerConnection(
  const string & peer_mac)
{
  MacAddressType mac;
  StringToByteArray(peer_mac, mac.begin(), mac.end());
  if(peer_network_->IsAdjacent(mac))
  {
    MacMsgData * md = new MacMsgData;
    md->mac = mac;
    worker_.Post(this, MSGID_END_CONNECTION, md);
  }
}

VnetDescriptor &
VirtualNetwork::Descriptor()
{
  return *descriptor_.get();
}

string
VirtualNetwork::Name()
{
  return descriptor_->name;
}

string
VirtualNetwork::MacAddress()
{
  MacAddressType mac = tdev_->MacAddress();
  return ByteArrayToString(mac.begin(), mac.end(), 0);
}

string
VirtualNetwork::Fingerprint()
{
  return local_fingerprint_->ToString();
}

void
VirtualNetwork::IgnoredNetworkInterfaces(
  const vector<string>& ignored_list)
{
  net_manager_.set_network_ignore_list(ignored_list);
}

void VirtualNetwork::QueryNodeInfo(
  const string & node_mac,
  Json::Value & node_info)
{
  MacAddressType mac;
  StringToByteArray(node_mac, mac.begin(), mac.end());
  if(peer_network_->IsAdjacent(mac))
  {
    shared_ptr<VirtualLink> vl = peer_network_->GetVlink(mac);
    node_info[TincanControl::UID] = vl->PeerInfo().uid;
    node_info[TincanControl::VIP4] = vl->PeerInfo().vip4;
    node_info[TincanControl::VIP6] = vl->PeerInfo().vip6;
    node_info[TincanControl::MAC] = vl->PeerInfo().mac_address;
    node_info[TincanControl::Fingerprint] = vl->PeerInfo().fingerprint;

    if(vl->IsReady())
    {
      LinkStatsMsgData md;
      md.vl = vl;
      worker_.Post(this, MSGID_QUERY_NODE_INFO, &md);
      md.msg_event.Wait(Event::kForever);
      node_info[TincanControl::Stats].swap(md.stats);
      node_info[TincanControl::Status] = "online";
    }
    else
    {
      node_info[TincanControl::Status] = "offline";
      node_info[TincanControl::Stats] = Json::Value(Json::arrayValue);
    }
  }
  else
  {
    node_info[TincanControl::MAC] = node_mac;
    node_info[TincanControl::Status] = "unknown";
  }
}

void
VirtualNetwork::SendIcc(
  const string & recipient_mac,
  const string & data)
{
  unique_ptr<IccMessage> icc = make_unique<IccMessage>();
  icc->Message((uint8_t*)data.c_str(), (uint16_t)data.length());
  TransmitMsgData *md = new TransmitMsgData;
  md->tf = move(icc);
  MacAddressType mac;
  StringToByteArray(recipient_mac, mac.begin(), mac.end());
  md->vl = peer_network_->GetVlink(mac);
  worker_.Post(this, MSGID_SEND_ICC, md);
}

/*
Incoming frames off the vlink are one of:
pure ethernet frame - to be delivered to the TAP device
icc message - to be delivered to the local controller
The implementing entity needs access to the TAP and controller instances to
transmit the frame. These can be provided at initialization.
Responsibility: Identify the received frame type, perfrom a transformation of
the frame
if needed and transmit it.
 - Is this my ARP? Deliver to TAP.
 - Is this an ICC? Send to controller.
Types of Transformation:

*/
void
VirtualNetwork::VlinkReadCompleteL2(
  uint8_t * data,
  uint32_t data_len,
  VirtualLink & vlink)
{
  unique_ptr<TapFrame> frame = make_unique<TapFrame>(data, data_len);
  TapFrameProperties fp(*frame);
  assert(!fp.IsFwdMsg()); // no forwarding at the moment
  if(fp.IsIccMsg())
  { // this is an ICC message, deliver to the ipop-controller
    unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
    ctrl->SetControlType(TincanControl::CTTincanRequest);
    Json::Value & req = ctrl->GetRequest();
    req[TincanControl::Command] = TincanControl::ICC;
    req[TincanControl::InterfaceName] = descriptor_->name;
    req[TincanControl::Data] = string((char*)frame->Payload(), frame->PayloadLength());
    //LOG(TC_DBG) << " Delivering ICC to ctrl, data=\n" << req[TincanControl::Data].asString();
    ctrl_link_->Deliver(move(ctrl));
  }
  else if(fp.IsFwdMsg())
  { // a frame to be routed on the overlay
    if(peer_network_->IsRouteExists(fp.DestinationMac()))
    {
      shared_ptr<VirtualLink> vl = peer_network_->GetRoute(fp.DestinationMac());
      TransmitMsgData *md = new TransmitMsgData;
      md->tf = move(frame);
      md->vl = vl;
      worker_.Post(this, MSGID_FWD_FRAME, md);
    }
    else
    { //no route found, send to controller
      unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
      ctrl->SetControlType(TincanControl::CTTincanRequest);
      Json::Value & req = ctrl->GetRequest();
      req[TincanControl::Command] = TincanControl::UpdateRoutes;
      req[TincanControl::InterfaceName] = descriptor_->name;
      req[TincanControl::Data] = ByteArrayToString(frame->Payload(),
        frame->PayloadEnd());
      //LOG(TC_DBG) << "FWDing frame to ctrl, data=\n" << req[TincanControl::Data].asString();
      ctrl_link_->Deliver(move(ctrl));
    }
  }
  else if (fp.IsDtfMsg())
  {
    frame->Dump("Frame from vlink");
    frame->BufferToTransfer(frame->Payload()); //write frame payload to TAP
    frame->BytesToTransfer(frame->PayloadLength());
    frame->SetWriteOp();
    tdev_->Write(*frame.release());
  }
  else
  {
    LOG_F(LS_ERROR) << "Unknown frame type received!";
    frame->Dump("Invalid header");
  }
}

//
//AsyncIOCompletion Routines for TAP device
/*
Frames read from the TAP device are handled here. This is an ethernet frame
from the networking stack. The implementing entity needs access to the
recipient  - via its vlink, or to the controller - when there is no
vlink to the recipient.
Responsibility: Identify the recipient of the frame and route accordingly.
- Is this an ARP? Send to controller.
- Is this an IP packet? Use MAC to lookup vlink and forwrd or send to
controller.
- Is this for a device behind an IPOP switch

Note: Avoid exceptions on the IO loop
*/
void
VirtualNetwork::TapReadCompleteL2(
  AsyncIo * aio_rd)
{
  TapFrame * frame = static_cast<TapFrame*>(aio_rd->context_);
  if(!aio_rd->good_)
  {
    frame->Initialize();
    frame->BufferToTransfer(frame->Payload());
    frame->BytesToTransfer(frame->PayloadCapacity());
    if(0 != tdev_->Read(*frame))
      delete frame;
    return;
  }
  frame->PayloadLength(frame->BytesTransferred());
  TapFrameProperties fp(*frame);
  MacAddressType mac = fp.DestinationMac();
  frame->BufferToTransfer(frame->Begin()); //write frame header + PL to vlink
  frame->BytesToTransfer(frame->Length());
  if(peer_network_->IsAdjacent(mac))
  {
    frame->Header(kDtfMagic);
    frame->Dump("Unicast");
    shared_ptr<VirtualLink> vl = peer_network_->GetVlink(mac);
    TransmitMsgData *md = new TransmitMsgData;;
    md->tf.reset(frame);
    md->vl = vl;
    worker_.Post(this, MSGID_TRANSMIT, md);
  }
  else if(peer_network_->IsRouteExists(mac))
  {
    frame->Header(kFwdMagic);
    frame->Dump("Frame FWD");
    TransmitMsgData *md = new TransmitMsgData;
    md->tf.reset(frame);
    md->vl = peer_network_->GetRoute(mac);
    worker_.Post(this, MSGID_FWD_FRAME, md);
  }
  else
  {
    frame->Header(kIccMagic);
    if(fp.IsArpRequest())
    {
      frame->Dump("ARP Request");
    }
    else if(fp.IsArpResponse())
    {
      frame->Dump("ARP Response");
    }
    else if (memcmp(fp.DestinationMac().data(),"\xff\xff\xff\xff\xff\xff", 6) != 0)
    {
      frame->Dump("No Route Unicast");
    }
    // The IPOP Controller has to find a route to deliver this ARP as Tincan cannot
    unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
    ctrl->SetControlType(TincanControl::CTTincanRequest);
    Json::Value & req = ctrl->GetRequest();
    req[TincanControl::Command] = TincanControl::UpdateRoutes;
    req[TincanControl::InterfaceName] = descriptor_->name;
    req[TincanControl::Data] = ByteArrayToString(frame->Payload(),
      frame->PayloadEnd());
    ctrl_link_->Deliver(move(ctrl));
    //Post a new TAP read request
    frame->Initialize(frame->Payload(), frame->PayloadCapacity());
    if(0 != tdev_->Read(*frame))
      delete frame;
  }
}

void
VirtualNetwork::TapWriteCompleteL2(
  AsyncIo * aio_wr)
{
  TapFrame * frame = static_cast<TapFrame*>(aio_wr->context_);
  if(frame->IsGood())
    frame->Dump("TAP Write Completed");
  else
    LOG_F(LS_WARNING) << "Tap Write FAILED completion";
  delete frame;
}

void VirtualNetwork::OnMessage(Message * msg)
{
  switch(msg->message_id)
  {
  case MSGID_CREATE_LINK: //create vlink
  {
    unique_ptr<PeerDescriptor> peer_desc =
      move(((CreateVlinkMsgData*)msg->pdata)->peer_desc);
    unique_ptr<VlinkDescriptor> vlink_desc = 
    move(((CreateVlinkMsgData*)msg->pdata)->vlink_desc);
    shared_ptr<VirtualLink> vl = CreateVlink(move(vlink_desc), move(peer_desc));
    peer_network_->Add(vl);
    //set event indicating vlink is create and added to peer network
    ((CreateVlinkMsgData*)msg->pdata)->msg_event.Set();
  }
  break;
  case MSGID_START_CONNECTION :
  {
    shared_ptr<VirtualLink> vl = ((VlinkMsgData*)msg->pdata)->vl;
    delete msg->pdata;
    vl->StartConnections();
  }
  break;
  case MSGID_TRANSMIT:
  {
    unique_ptr<TapFrame> frame = move(((TransmitMsgData*)msg->pdata)->tf);
    shared_ptr<VirtualLink> vl = ((TransmitMsgData*)msg->pdata)->vl;
    vl->Transmit(*frame);
    delete msg->pdata;
    frame->Initialize(frame->Payload(), frame->PayloadCapacity());
    if(0 == tdev_->Read(*frame))
      frame.release();
  }
  break;
  case MSGID_SEND_ICC:
  {
    unique_ptr<TapFrame> frame = move(((TransmitMsgData*)msg->pdata)->tf);
    shared_ptr<VirtualLink> vl = ((TransmitMsgData*)msg->pdata)->vl;
    vl->Transmit(*frame);
    //LOG_F(TC_DBG) << "Sent ICC to=" <<vl->PeerInfo().vip4 << " data=\n" <<
    //  string((char*)(frame->begin()+4), *(uint16_t*)(frame->begin()+2));
    delete msg->pdata;
  }
  break;
  case MSGID_END_CONNECTION:
  {
    MacAddressType mac = ((MacMsgData*)msg->pdata)->mac;
    delete msg->pdata;
    peer_network_->Remove(mac);
    //DestroyTunnel(*vl);
  }
  break;
  case MSGID_QUERY_NODE_INFO:
  {
    shared_ptr<VirtualLink> vl = ((LinkStatsMsgData*)msg->pdata)->vl;
    vl->GetStats(((LinkStatsMsgData*)msg->pdata)->stats);
    ((LinkStatsMsgData*)msg->pdata)->msg_event.Set();
  }
  break;
  case MSGID_FWD_FRAME:
  case MSGID_FWD_FRAME_RD:
  {
    unique_ptr<TapFrame> frame = move(((TransmitMsgData*)msg->pdata)->tf);
    shared_ptr<VirtualLink> vl = ((TransmitMsgData*)msg->pdata)->vl;
    vl->Transmit(*frame);
    //LOG(TC_DBG) << "FWDing frame to " << vl->PeerInfo().vip4;
    if(msg->message_id == MSGID_FWD_FRAME_RD)
    {
      frame->Initialize(frame->Payload(), frame->PayloadCapacity());
      if(0 == tdev_->Read(*frame))
        frame.release();
    }
    delete msg->pdata;
  }
  break;
  }
}

void
VirtualNetwork::ProcessIncomingFrame(
  uint8_t * data,
  uint32_t data_len,
  VirtualLink & vlink)
{
}

void
VirtualNetwork::TapReadComplete(
  AsyncIo * aio_rd)
{
}

void
VirtualNetwork::TapWriteComplete(
  AsyncIo * aio_wr)
{
}

void
VirtualNetwork::InjectFame(
  string && data)
{
  unique_ptr<TapFrame> tf = make_unique<TapFrame>();
  tf->Initialize();
  if(data.length() > 2 * kTapBufferSize)
  {
    stringstream oss;
    oss << "Inject Frame operation failed - frame size " << data.length() / 2 <<
      " is larger than maximum accepted " << kTapBufferSize;
    throw TCEXCEPT(oss.str().c_str());
  }
  size_t len = StringToByteArray(data, tf->Payload(), tf->End());
  if(len != data.length() / 2)
    throw TCEXCEPT("Inject Frame operation failed - ICC decode failure");
  tf->SetWriteOp();
  tf->PayloadLength((uint32_t)len);
  tf->BufferToTransfer(tf->Payload());
  tf->BytesTransferred((uint32_t)len);
  tf->BytesToTransfer((uint32_t)len);
  tdev_->Write(*tf.release());
  //LOG(TC_DBG) << "Frame injected=\n" << data;
}
} //namespace tincan
