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
#if defined (_IPOP_WIN)
#include <mstcpip.h>
#endif
#include "tincan_control.h"
namespace tincan
{
string ipstring(uint8_t* ip4)
{
  ostringstream mac;
  for(short i = 0; i < 4; ++i)
  {
    if(i != 0) mac << '.';
    mac.width(3);
    mac.fill('0');
    mac << std::dec << (int)ip4[i];
  }
  return mac.str();
}

VirtualNetwork::VirtualNetwork(
  unique_ptr<VnetDescriptor> descriptor,
  IpopControllerLink & ctrl_handle) :
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
    descriptor_->mtu4,
    descriptor_->vip6,
    descriptor_->prefix6,
    descriptor_->mtu6};
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
  //TODO: Move to platform agnostic implementation in tincan_base.h
#if defined (_IPOP_WIN)
  const char *term;
  LONG err = RtlIpv4StringToAddress(descriptor_->vip4.c_str(),
    TRUE, &term, &descriptor_->vip4_addr);
  unsigned long scope_id;
  uint16_t port;
  err = RtlIpv6StringToAddressEx(descriptor_->vip6.c_str(),
    &descriptor_->vip6_addr, &scope_id, &port);
#endif
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
  vlink;
  lock_guard<mutex> lg(vn_mtx);
  for(uint8_t i = 0; i < kLinkConcurrentAIO; i++)
  {
    unique_ptr<TapFrame> tf = make_unique<TapFrame>();
    if(tf)
    {
      tf->Initialize();
      tdev_->Read(*tf.release());
    }//else its fine, enough AIOs are active in the system
  }
}

void
VirtualNetwork::DestroyTunnel(
  VirtualLink & vlink)
{
  vlink;
}

void
VirtualNetwork::AddRoute(
  MacAddressType mac_dest,
  MacAddressType mac_path)
{
  peer_network_->AddRoute(mac_dest, mac_path);
}

void
VirtualNetwork::RemoveRoute(
  MacAddressType mac_dest)
{
  peer_network_->RemoveRoute(mac_dest);
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
    vl->SignalMessageReceived.connect(this, &VirtualNetwork::ProcessIncomingFrameL2);
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
  if(peer_network_->Exists(peer_desc->uid))
  {
    vl = peer_network_->UidToVlink(peer_desc->uid);
  }
  else
  {
    string uid = peer_desc->uid;
    // create vlink using the worker thread
    CreateVlinkMsgData md;
    md.peer_desc = move(peer_desc);
    md.vlink_desc = move(vlink_desc);
    worker_.Post(this, MSGID_CREATE_LINK, &md);
    //block until it ready
    md.msg_event.Wait(MessageQueue::kForever);
    //grab it from the peer_network
    if (peer_network_->Exists(uid))
      vl = peer_network_->UidToVlink(uid);
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
  if(peer_network_->Exists(peer_desc->uid))
  {
    vl = peer_network_->UidToVlink(peer_desc->uid);
    vl->PeerCandidates(peer_desc->cas);
  }
  else
  {
    string uid = peer_desc->uid;
    // create vlink using the worker thread
    CreateVlinkMsgData md;
    md.peer_desc = move(peer_desc);
    md.vlink_desc = move(vlink_desc);
    worker_.Post(this, MSGID_CREATE_LINK, &md);
    //block until it ready
    md.msg_event.Wait(Event::kForever);
    vl = peer_network_->UidToVlink(uid);
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
  const string & peer_uid)
{
  if(peer_network_->Exists(peer_uid))
  {
    UidMsgData * md = new UidMsgData;
    md->uid = peer_uid;
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
  return ByteArrayToString(tdev_->MacAddress().begin(),
    tdev_->MacAddress().end(), 0);
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
  const string & node_uid,
  Json::Value & node_info)
{
  if(peer_network_->Exists(node_uid))
  {
    shared_ptr<VirtualLink> vl = peer_network_->UidToVlink(node_uid);
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
    node_info[TincanControl::UID] = node_uid;
    node_info[TincanControl::Status] = "unknown";
  }
}

void
VirtualNetwork::SendIcc(
  const string & recipient_uid,
  const string & data)
{
  TransmitMsgData *md = new TransmitMsgData;
  md->tf = make_unique<TapFrame>(true);
  IccMessage * icc = reinterpret_cast<IccMessage*>(md->tf->begin());
  icc->MessageData((uint8_t*)data.c_str(), (uint16_t)data.length());
  md->tf->BytesToTransfer(icc->MessageLength());
  md->tf->BufferToTransfer(md->tf->begin());
  md->vl = peer_network_->UidToVlink(recipient_uid);
  /*LOG_F(LS_ERROR) << "SendICC: magic(" << icc->magic_ << ") length(" << icc->data_len_ << ")";*/
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
VirtualNetwork::ProcessIncomingFrameL2(
  uint8_t * data,
  uint32_t data_len,
  VirtualLink & vlink)
{
  vlink;
  //TapFrame * frame = tf_cache_.Acquire(data, data_len);
  unique_ptr<TapFrame> frame = make_unique<TapFrame>(data, data_len);
  TapFrameProperties fp(*frame);
  if(fp.IsIccMsg())
  { // this is an ICC message, deliver to the ipop-controller
    unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
    ctrl->SetControlType(TincanControl::CTTincanRequest);
    Json::Value & req = ctrl->GetRequest();
    req[TincanControl::Command] = TincanControl::ICC;
    req[TincanControl::InterfaceName] = descriptor_->name;
    IccMessage * icc = reinterpret_cast<IccMessage*>(frame->begin());
    req[TincanControl::Data] = string((char*)icc->data_, icc->data_len_);
    ctrl_link_.Deliver(move(ctrl));
    //tf_cache_.Reclaim(frame);
  }
  else
  {
    //unique_ptr<TapFrame> frame = make_unique<TapFrame>(data, data_len);
    //TapFrame * frame = tf_cache_.Acquire(data, data_len);
    //TapFrameProperties fp(*frame);
    frame->BufferToTransfer(frame->begin());
    frame->BytesToTransfer(frame->BytesTransferred());
    frame->SetWriteOp();
    tdev_->Write(*frame.release());
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
- Is this an ARP? Use IP to lookup vlink and forward or send to controller.
- Is this an IP packet? Use MAC to lookup vlink and forwrd or send to
controller.

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
    if(0 != tdev_->Read(*frame))
      delete frame;
      //tf_cache_.Reclaim(frame);
    return;
  }
  TapFrameProperties fp(*frame);
  MacAddressType mac = fp.DestinationMac();
  frame->BufferToTransfer(frame->begin());
  frame->BytesToTransfer(frame->BytesTransferred());
  bool found = peer_network_->Exists(mac);
  if(found)
  {
    frame->Dump("Unicast");
    shared_ptr<VirtualLink> vl = peer_network_->MacAddressToVlink(mac);
    TransmitMsgData *md = new TransmitMsgData;;
    md->tf.reset(frame);
    md->vl = vl;
    worker_.Post(this, MSGID_TRANSMIT, md);
  }
  else
  {
/*    // The IPOP Controller has to find a route to deliver this ARP as Tincan cannot
    unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
    ctrl->SetControlType(TincanControl::CTTincanRequest);
    Json::Value & req = ctrl->GetRequest();
    req[TincanControl::Command] = TincanControl::UpdateRoutes;
    req[TincanControl::InterfaceName] = descriptor_->name;
    req[TincanControl::Data] = ByteArrayToString(frame->BufferToTransfer(),
      frame->BufferToTransfer() + frame->BytesToTransfer());
    ctrl_link_.Deliver(move(ctrl));
    //Post a new TAP read request
    frame->Initialize();
    if(0 != tdev_->Read(*frame))
      delete frame;
    //tf_cache_.Reclaim(frame);
*/
    if (fp.IsArpRequest())
    {
      frame->Dump("ARP Request");
      // The IPOP Controller has to find a route to deliver this ARP as Tincan cannot
      unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
      ctrl->SetControlType(TincanControl::CTTincanRequest);
      Json::Value & req = ctrl->GetRequest();
      req[TincanControl::Command] = TincanControl::UpdateRoutes;
      req[TincanControl::InterfaceName] = descriptor_->name;
      req[TincanControl::Data] = ByteArrayToString(frame->BufferToTransfer(),
        frame->BufferToTransfer() + frame->BytesToTransfer());
      ctrl_link_.Deliver(move(ctrl));
      //Post a new TAP read request
      frame->Initialize();
      if(0 != tdev_->Read(*frame))
        delete frame;
        //tf_cache_.Reclaim(frame);
    }
    else if(fp.IsArpResponse())
    {
      frame->Dump("ARP Response");
      // The IPOP Controller has to find a route to deliver this ARP as Tincan cannot
      unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
      ctrl->SetControlType(TincanControl::CTTincanRequest);
      Json::Value & req = ctrl->GetRequest();
      req[TincanControl::Command] = TincanControl::UpdateRoutes;
      req[TincanControl::InterfaceName] = descriptor_->name;
      req[TincanControl::Data] = ByteArrayToString(frame->BufferToTransfer(),
        frame->BufferToTransfer() + frame->BytesToTransfer());
      ctrl_link_.Deliver(move(ctrl));
      //Post a new TAP read request
      frame->Initialize();
      if(0 != tdev_->Read(*frame))
        delete frame;
    }
    else
    {
      frame->Initialize();
      if(0 != tdev_->Read(*frame))
        delete frame;
        //tf_cache_.Reclaim(frame);
    }
  }
}

void
VirtualNetwork::TapWriteCompleteL2(
  AsyncIo * aio_wr)
{
  TapFrame * frame = static_cast<TapFrame*>(aio_wr->context_);
  frame->Dump("TAP Write Completed");
  delete frame;
  //tf_cache_.Reclaim(frame);
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
    frame->Initialize();
    if(0 == tdev_->Read(*frame))
      frame.release();
  }
  break;
  case MSGID_SEND_ICC:
  {
    unique_ptr<TapFrame> frame = move(((TransmitMsgData*)msg->pdata)->tf);
    shared_ptr<VirtualLink> vl = ((TransmitMsgData*)msg->pdata)->vl;
    vl->Transmit(*frame);
    LOG_F(LS_VERBOSE) << "Sent ICC to " << vl->PeerInfo().vip4;

    delete msg->pdata;
  }
  break;
  case MSGID_END_CONNECTION:
  {
    string uid = ((UidMsgData*)msg->pdata)->uid;
    delete msg->pdata;
    peer_network_->Remove(uid);
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
  }
}

void
VirtualNetwork::ProcessIncomingFrame(
  uint8_t * data,
  uint32_t data_len,
  VirtualLink & vlink)
{
  data, data_len, vlink;
}

void
VirtualNetwork::TapReadComplete(
  AsyncIo * aio_rd)
{
  aio_rd;
}

void
VirtualNetwork::TapWriteComplete(
  AsyncIo * aio_wr)
{
  aio_wr;
}

void
VirtualNetwork::InjectFame(
  string && data)
{
  unique_ptr<TapFrame> tf = make_unique<TapFrame>();
  tf->Initialize();
  tf->SetWriteOp();
  tf->BufferToTransfer(tf->begin());
  if(data.length() > 2 * kTapBufferSize)
  {
    stringstream oss;
    oss << "Inject Frame operation failed - frame size " << data.length() / 2 <<
      " is larger than maximum accepted " << kTapBufferSize;
    throw TCEXCEPT(oss.str().c_str());
  }
  size_t len = StringToByteArray(data, tf->begin(), tf->end());
  if(len != data.length() / 2)
    throw TCEXCEPT("Inject Frame operation failed - ICC decode failure");
  tf->BytesTransferred((uint32_t)len);
  tf->BytesToTransfer((uint32_t)len);
  tdev_->Write(*tf.release());
  LOG_F(LS_ERROR) << "ICC frame injected";
}
} //namespace tincan
