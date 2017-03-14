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

#include "tincan.h"
#include "tincan_exception.h"
namespace tincan
{
Tincan::Tincan() :
  ctrl_listener_(nullptr),

  ctrl_link_(nullptr),

  exit_event_(false, false)
{}

Tincan::~Tincan()
{
  delete ctrl_listener_;
}

void Tincan::UpdateRoute(
  const string & tap_name,
  const string & dest_mac,
  const string & path_mac)
{
  VirtualNetwork & vn = VnetFromName(tap_name);
  MacAddressType mac_dest, mac_path;
  size_t cnt = StringToByteArray(dest_mac, mac_dest.begin(), mac_dest.end());
  if(cnt != 6)
    throw TCEXCEPT("UpdateRoute failed, destination MAC address was NOT successfully converted.");
  cnt = StringToByteArray(path_mac, mac_path.begin(), mac_path.end());
  if(cnt != 6)
    throw TCEXCEPT("UpdateRoute failed, path MAC address was NOT successfully converted.");
  vn.UpdateRoute(mac_dest, mac_path);
}

void
Tincan::ConnectToPeer(
  const Json::Value & link_desc)
{
  unique_ptr<PeerDescriptor> peer_desc = make_unique<PeerDescriptor>();
  peer_desc->uid = link_desc[TincanControl::PeerInfo][TincanControl::UID].asString();
  peer_desc->vip4 = link_desc[TincanControl::PeerInfo][TincanControl::VIP4].asString();
  peer_desc->vip6 = link_desc[TincanControl::PeerInfo][TincanControl::VIP6].asString();
  peer_desc->cas = link_desc[TincanControl::PeerInfo][TincanControl::CAS].asString();
  peer_desc->fingerprint = link_desc[TincanControl::PeerInfo][TincanControl::Fingerprint].asString();
  peer_desc->mac_address = link_desc[TincanControl::PeerInfo][TincanControl::MAC].asString();
  string tap_name = link_desc[TincanControl::InterfaceName].asString();
  unique_ptr<VlinkDescriptor> vl_desc = make_unique<VlinkDescriptor>();
  vl_desc->name = tap_name;
  vl_desc->sec_enabled = link_desc[TincanControl::EncryptionEnabled].asBool();

  VirtualNetwork & vn = VnetFromName(tap_name);
  vn.ConnectToPeer(move(peer_desc), move(vl_desc));
}

void 
Tincan::SetIpopControllerLink(
  shared_ptr<IpopControllerLink> ctrl_handle)
{
  ctrl_link_ = ctrl_handle;
}

void Tincan::CreateVNet(unique_ptr<VnetDescriptor> lvecfg)
{
  lock_guard<mutex> lg(vnets_mutex_);
  auto vnet = make_unique<VirtualNetwork>(move(lvecfg), ctrl_link_);
  vnet->Configure();
  vnet->Start();
  vnets_.push_back(move(vnet));
}

void
Tincan::CreateVlinkListener(
  const Json::Value & link_desc,
  TincanControl & ctrl)
{
  unique_ptr<PeerDescriptor> peer_desc = make_unique<PeerDescriptor>();
  peer_desc->uid = link_desc[TincanControl::PeerInfo][TincanControl::UID].asString();
  peer_desc->vip4 = link_desc[TincanControl::PeerInfo][TincanControl::VIP4].asString();
  peer_desc->vip6 = link_desc[TincanControl::PeerInfo][TincanControl::VIP6].asString();
  peer_desc->fingerprint = link_desc[TincanControl::PeerInfo][TincanControl::Fingerprint].asString();
  peer_desc->mac_address = link_desc[TincanControl::PeerInfo][TincanControl::MAC].asString();
  string tap_name = link_desc[TincanControl::InterfaceName].asString();
  unique_ptr<VlinkDescriptor> vl_desc = make_unique<VlinkDescriptor>();
  vl_desc->name = tap_name;
  vl_desc->sec_enabled = link_desc[TincanControl::EncryptionEnabled].asBool();
  
  VirtualNetwork & vn = VnetFromName(tap_name);
  shared_ptr<VirtualLink> vlink =
    vn.CreateVlinkEndpoint(move(peer_desc), move(vl_desc));
  //Fixme: Potential but unlikely race condition of CAS update being ready before control is queued and the the call back registered.
  if(vlink->Candidates().empty())
  {
    std::lock_guard<std::mutex> lg(inprogess_controls_mutex_);
    inprogess_controls_.push_back(make_unique<TincanControl>(move(ctrl)));
    vlink->SignalLocalCasReady.connect(this, &Tincan::OnLocalCasUpdated);
  }
  else
  {
    ctrl.SetResponse(vlink->Candidates(), true);
    ctrl_link_->Deliver(ctrl);
  }
}

void
Tincan::InjectFrame(
  const Json::Value & frame_desc)
{
  const string & tap_name = frame_desc[TincanControl::InterfaceName].asString();
  VirtualNetwork & vn = VnetFromName(tap_name);
  vn.InjectFame(move(frame_desc[TincanControl::Data].asString()));
}

void
Tincan::QueryNodeInfo(
  const string & tap_name,
  const string & node_mac,
  Json::Value & node_info)
{
  VirtualNetwork & vn = VnetFromName(tap_name);
  if(node_mac.length() == 0 || node_mac == vn.Descriptor().mac)
  {
    GetLocalNodeInfo(vn, node_info);
  }
  else
  {
    node_info[TincanControl::Type] = "peer";
    node_info[TincanControl::VnetDescription] = vn.Descriptor().description;
    node_info[TincanControl::InterfaceName] = vn.Name();
    vn.QueryNodeInfo(node_mac, node_info);
  }
}

void
Tincan::RemoveVlink(
  const Json::Value & link_desc)
{
  const string & tap_name = link_desc[TincanControl::InterfaceName].asString();
  VirtualNetwork & vn = VnetFromName(tap_name);
  const string & mac = link_desc[TincanControl::MAC].asString();
  vn.RemovePeerConnection(mac);
}

void
Tincan::SendIcc(
  const Json::Value & icc_desc)
{
  const string & tap_name = icc_desc[TincanControl::InterfaceName].asString();
  VirtualNetwork & vn = VnetFromName(tap_name);
  const string & mac = icc_desc[TincanControl::RecipientMac].asString();
  const string & data = icc_desc[TincanControl::Data].asString();
  vn.SendIcc(mac, data);
}

void
Tincan::SetIgnoredNetworkInterfaces(
  const string & tap_name,
  vector<string>& ignored_list)
{
  VirtualNetwork & vn = VnetFromName(tap_name);
  vn.IgnoredNetworkInterfaces(ignored_list);
}

void
Tincan::OnLocalCasUpdated(
  string lcas)
{
  if(lcas.empty())
  {
    lcas = "No local candidates available on this vlink";
    LOG_F(LS_WARNING) << lcas;
  }
  //this seemingly round-about code is to avoid locking the Deliver() call or setting up the excepton handler necessary for using the mutex directly.
  bool to_deliver = false;
  list<unique_ptr<TincanControl>>::iterator itr;
  unique_ptr<TincanControl> ctrl;
  {
    std::lock_guard<std::mutex> lg(inprogess_controls_mutex_);
    itr = inprogess_controls_.begin();
    for(; itr != inprogess_controls_.end(); itr++)
    {
      if((*itr)->GetCommand() == TincanControl::CreateLinkListener.c_str())
      {
        to_deliver = true;
        ctrl = move(*itr);
        inprogess_controls_.erase(itr);
        break;
      }
    }
  }
  if(to_deliver)
  {
    ctrl->SetResponse(lcas, true);
    ctrl_link_->Deliver(move(ctrl));
  }
}

void
Tincan::Run()
{
  //TODO:Code cleanup
#if defined(_IPOP_WIN)
  self_ = this;
  SetConsoleCtrlHandler(ControlHandler, TRUE);
#endif // _IPOP_WIN

  //Start tincan control to get config from Controller
  unique_ptr<ControlDispatch> ctrl_dispatch(new ControlDispatch);
  ctrl_dispatch->SetDispatchToTincanInf(this);
  ctrl_listener_ = new ControlListener(move(ctrl_dispatch));
  ctl_thread_.Start(ctrl_listener_);
  exit_event_.Wait(Event::kForever);
}

void Tincan::GetLocalNodeInfo(
  VirtualNetwork & vnet,
  Json::Value & node_info)
{
  VnetDescriptor & lcfg = vnet.Descriptor();
  node_info[TincanControl::Type] = "local";
  node_info[TincanControl::UID] = lcfg.uid;
  node_info[TincanControl::VIP4] = lcfg.vip4;
  node_info[TincanControl::VnetDescription] = lcfg.description;
  node_info[TincanControl::MAC] = vnet.MacAddress();
  node_info[TincanControl::Fingerprint] = vnet.Fingerprint();
  node_info[TincanControl::InterfaceName] = vnet.Name();
}

VirtualNetwork & 
Tincan::VnetFromName(
  const string & tap_name)
{
  lock_guard<mutex> lg(vnets_mutex_);
  for(auto const & vnet : vnets_) {
    if(vnet->Name().compare(tap_name) == 0)//list of vnets will be small enough where a linear search is best
      return *vnet.get();
  }
  string msg("No virtual network exists by this name: ");
  msg.append(tap_name);
  throw TCEXCEPT(msg.c_str());
}

//-----------------------------------------------------------------------------
void Tincan::OnStop() {
  Shutdown();
  exit_event_.Set();
}

void
Tincan::Shutdown()
{
  lock_guard<mutex> lg(vnets_mutex_);
  ctl_thread_.Stop();
  for(auto const & vnet : vnets_) {
    vnet->Shutdown();
  }
}

/*
FUNCTION:ControlHandler
PURPOSE: Handles keyboard signals
PARAMETERS: The signal code
RETURN VALUE: Success/Failure
++*/
#if defined(_IPOP_WIN)
BOOL __stdcall Tincan::ControlHandler(DWORD CtrlType) {
  switch(CtrlType) {
  case CTRL_BREAK_EVENT:  // use Ctrl+C or Ctrl+Break to send
  case CTRL_C_EVENT:      // termination signal
    cout << "Stopping tincan... " << std::endl;
    self_->OnStop();
    return(TRUE);
  }
  return(FALSE);
}
#endif // _IPOP_WIN
} // namespace tincan
