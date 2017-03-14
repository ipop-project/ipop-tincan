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
#include "control_dispatch.h"
#include "webrtc/base/json.h"
#include "tincan_exception.h"
#include "vnet_descriptor.h"

namespace tincan
{
using namespace rtc;
ControlDispatch::ControlDispatch() :
  dtol_(nullptr),
  ctrl_link_(make_shared<DisconnectedControllerHandle>())
{
  control_map_ = {
    { "UpdateMap", &ControlDispatch::UpdateRoutes },
    { "ConnectToPeer", &ControlDispatch::ConnectToPeer },
    { "CreateCtrlRespLink", &ControlDispatch::CreateIpopControllerRespLink },
    { "CreateLinkListener", &ControlDispatch::CreateLinkListener },
    { "CreateVnet", &ControlDispatch::CreateVNet },
    { "Echo", &ControlDispatch::Echo },
    { "ICC", &ControlDispatch::SendICC },
    { "InjectFrame", &ControlDispatch::InjectFrame },
    { "QueryStunCandidates", &ControlDispatch::QueryStunCandidates },
    { "QueryNodeInfo", &ControlDispatch::QueryNodeInfo },
    { "RemovePeer", &ControlDispatch::RemovePeer },
    { "SetIgnoredNetInterfaces", &ControlDispatch::SetNetworkIgnoreList },
    { "SetLoggingLevel", &ControlDispatch::SetLogLevel },
  };
}
ControlDispatch::~ControlDispatch()
{}

void
ControlDispatch::operator () (TincanControl & control)
{
  try {
    switch(control.GetControlType()) {
    case TincanControl::CTTincanRequest:
      (this->*control_map_.at(control.GetCommand()))(control);
      break;
    case TincanControl::CTTincanResponse:
    // todo: A controller response to something sent earlier
      break;
    default:
      LOG_F(LS_WARNING) << "Unrecognized control type received and discarded.";
      break;
    }
  }
  catch(out_of_range & e) {
    LOG_F(LS_WARNING) << "An invalid IPOP control operation was received and "
      "discarded: " << control.StyledString() << "Exception=" << e.what();
  }
  catch(exception & e)
  {
    LOG_F(LS_WARNING) << e.what();
  }
}
void
ControlDispatch::SetDispatchToTincanInf(
  TincanDispatchInterface * dtot)
{
  tincan_ = dtot;
}
void
ControlDispatch::SetDispatchToListenerInf(
  DispatchToListenerInf * dtol)
{
  dtol_ = dtol;
}

void
ControlDispatch::UpdateRoutes(
  TincanControl & control)
{
  bool status = false;
  Json::Value & req = control.GetRequest();
  const string tap_name = req[TincanControl::InterfaceName].asString();
  string msg = "The Add Routes operation failed.";
  lock_guard<mutex> lg(disp_mutex_);
  Json::Value rts = req[TincanControl::Routes];
  if(rts.isArray())
  {
    for(uint32_t i = 0; i < rts.size(); i++)
    {
      try
      {
        string route = rts[i].asString();
        string dest_mac = route.substr(0, 12);
        string path_mac = route.substr(13, 24);
        tincan_->UpdateRoute(tap_name, dest_mac, path_mac);
      } catch(exception & e)
      {
        LOG_F(LS_WARNING) << e.what() << ". Control Data=\n" <<
          control.StyledString();
      }
    }
    status = true;
    msg = "The Add Routes opertation completed successfully.";
  }
  else
  {
    msg += "The routes parameter is not an array. ";
  }
  control.SetResponse(msg, status);
  ctrl_link_->Deliver(control);
}

void
ControlDispatch::ConnectToPeer(
  TincanControl & control)
{
  Json::Value & req = control.GetRequest();
  string msg("Connection to peer node in progress.");
  bool status = false;
  lock_guard<mutex> lg(disp_mutex_);
  try
  {
    tincan_->ConnectToPeer(req);
    status = true;
  } catch(exception & e)
  {
    msg = "The ConnectToPeer operation failed.";
    LOG_F(LS_WARNING) << e.what() << ". Control Data=\n" <<
      control.StyledString();
  }
  control.SetResponse(msg, status);
  ctrl_link_->Deliver(control);
}
void ControlDispatch::CreateIpopControllerRespLink(
  TincanControl & control)
{
  Json::Value & req = control.GetRequest();
  string ip = req["IP"].asString();
  int port = req["Port"].asInt();
  string msg("Controller endpoint successfully created.");
  lock_guard<mutex> lg(disp_mutex_);
  try
  {
    unique_ptr<SocketAddress> ctrl_addr(new SocketAddress(ip, port));
    dtol_->CreateIpopControllerLink(move(ctrl_addr));
    ctrl_link_.reset(&dtol_->GetIpopControllerLink());
    tincan_->SetIpopControllerLink(ctrl_link_);
    control.SetResponse(msg, true);
    ctrl_link_->Deliver(control);
  }
  catch(exception & e)
  {
    //if this fails we can't indicate this to the controller so log with
    //high severity
    LOG_F(LS_ERROR) << e.what() << ". Control Data=\n" <<
      control.StyledString();
  }
}

void
ControlDispatch::CreateLinkListener(
  TincanControl & control)
{
  Json::Value & req = control.GetRequest();
  lock_guard<mutex> lg(disp_mutex_);
  try
  {
    tincan_->CreateVlinkListener(req, control); //don't use this instance of control after this line as its internals were moved.
  } catch(exception & e)
  {
    string msg = "The CreateLinkListener operation failed.";
    LOG_F(LS_WARNING) << e.what() << ". Control Data=\n" <<
      control.StyledString();
    //send fail here, send the cas when the op completes
    control.SetResponse(msg, false);
    ctrl_link_->Deliver(control);
  }
}

void
ControlDispatch::CreateVNet(
  TincanControl & control)
{
  Json::Value & req = control.GetRequest();
  string msg;
  bool status = false;
  lock_guard<mutex> lg(disp_mutex_);
  try
  {
    unique_ptr<VnetDescriptor> vn_desc(new VnetDescriptor);
    vn_desc->name = req[TincanControl::InterfaceName].asString();
    vn_desc->uid = req["LocalUID"].asString();
    vn_desc->vip4 = req["LocalVirtIP4"].asString();
    vn_desc->prefix4 = req["LocalPrefix4"].asUInt();
    vn_desc->mtu4 = req["Mtu4"].asUInt();
    vn_desc->l2tunnel_enabled = req["L2TunnelEnabled"].asBool();
    vn_desc->l3tunnel_enabled =req["L3TunnelEnabled"].asBool();
    vn_desc->stun_addr = req["StunAddress"].asString();
    vn_desc->turn_addr = req["TurnAddress"].asString();
    vn_desc->turn_pass = req["TurnPass"].asString();
    vn_desc->turn_user = req["TurnUser"].asString();
    tincan_->CreateVNet(move(vn_desc));
    status = true;
  } catch(exception & e)
  {
    msg = "The CreateVNet operation failed.";
    LOG_F(LS_ERROR) << e.what() << ". Control Data=\n" <<
      control.StyledString();
  }
  control.SetResponse(msg, status);
  ctrl_link_->Deliver(control);
}

void
ControlDispatch::QueryNodeInfo(
  TincanControl & control)
{
  Json::Value & req = control.GetRequest(), node_info;
  string mac = req[TincanControl::MAC].asString();
  string tap_name = req[TincanControl::InterfaceName].asString();
  string resp;
  bool status = false;
  lock_guard<mutex> lg(disp_mutex_);
  try
  {
    tincan_->QueryNodeInfo(tap_name, mac, node_info);
    resp = node_info.toStyledString();;
    status = true;
  } catch(exception & e)
  {
    resp = "The QueryNodeInfo operation failed. ";
    LOG_F(LS_WARNING) << resp << e.what() << ". Control Data=\n" <<
      control.StyledString();
  }
  control.SetResponse(resp, status);
  ctrl_link_->Deliver(control);
}

void ControlDispatch::Echo(TincanControl & control)
{
  Json::Value & req = control.GetRequest();
  string msg = req[TincanControl::Message].asString();
  control.SetResponse(msg, true);
  control.SetControlType(TincanControl::CTTincanResponse);
  ctrl_link_->Deliver(control);
}

void
ControlDispatch::InjectFrame(
  TincanControl & control)
{
  Json::Value & req = control.GetRequest();
  lock_guard<mutex> lg(disp_mutex_);
  try
  {
    tincan_->InjectFrame(req);
  } catch(exception & e)
  {
    string msg = "The Inject Frame operation failed - ";
    LOG_F(LS_WARNING) << msg << e.what() << ". Control Data=\n" <<
      control.StyledString();
  }
}

void
ControlDispatch::QueryStunCandidates(
  TincanControl & control)
{
  //TODO: implementation
}

void
ControlDispatch::RemovePeer(
  TincanControl & control)
{
  bool status = false;
  Json::Value & req = control.GetRequest();
  const string tap_name = req[TincanControl::InterfaceName].asString();
  const string mac = req[TincanControl::MAC].asString();
  string msg("The virtual link to ");
  msg.append(mac).append(" has been removed.");
  lock_guard<mutex> lg(disp_mutex_);
  try
  {
    if(!tap_name.empty() && !mac.empty())
    {
      tincan_->RemoveVlink(req);
      status = true;
    }
    else
    {
      ostringstream oss;
      oss << "Invalid parameters in request to remove link to peer node. " <<
        "Received: TAP Name=" << tap_name << " MAC=" << mac;
      msg = oss.str();
      throw TCEXCEPT(msg.c_str());
    }
  } catch(exception & e)
  {
    msg = "The RemovePeer operation failed.";
    LOG_F(LS_WARNING) << e.what() << ". Control Data=\n" <<
      control.StyledString();
  }
  control.SetResponse(msg, status);
  ctrl_link_->Deliver(control);
}

void
ControlDispatch::SetLogLevel(
  TincanControl & control)
{
  Json::Value & req = control.GetRequest();
  bool status = true;
  string msg("The log level has been set to ");
  string logging = req[TincanControl::LogLevel].asString();
  msg.append(logging);
  lock_guard<mutex> lg(disp_mutex_);
  if(logging == "NONE")
  {
    rtc::LogMessage::LogToDebug(rtc::LS_NONE);
  }
  else if(logging == "ERROR")
  {
    rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
  }
  else if(logging == "WARNING")
  {
    rtc::LogMessage::LogToDebug(rtc::LS_WARNING);
  }
  else if(logging == "INFO")
  {
    rtc::LogMessage::LogToDebug(rtc::LS_INFO);
  }
  else if(logging == "VERBOSE" || logging == "DEBUG")
  {
    rtc::LogMessage::LogToDebug(rtc::LS_VERBOSE);
  }
  else if(logging == "SENSITIVE")
  {
    rtc::LogMessage::LogToDebug(rtc::LS_SENSITIVE);
  }
  else
  {
    msg = "The SetLogLevel operation failed. ";
    LOG_F(LS_WARNING) << msg << ". Control Data=\n" <<
      control.StyledString();
    status = false;
  }
  control.SetResponse(msg, status);
  ctrl_link_->Deliver(control);
}

void
ControlDispatch::SetNetworkIgnoreList(
  TincanControl & control)
{
  Json::Value & req = control.GetRequest();
  string tap_name = req[TincanControl::InterfaceName].asString();
  int count = req[TincanControl::IgnoredNetInterfaces].size();
  Json::Value network_ignore_list = req[TincanControl::IgnoredNetInterfaces];
  string resp;
  bool status = false;
  lock_guard<mutex> lg(disp_mutex_);
  try
  {
    vector<string> ignore_list(count);
    for(int i = 0; i < count; i++)
    {
      ignore_list[i] = network_ignore_list[i].asString();
    }
    tincan_->SetIgnoredNetworkInterfaces(tap_name, ignore_list);
    status = true;
  } catch(exception & e)
  {
    resp = "The SetNetworkIgnoreList operation failed.";
    LOG_F(LS_WARNING) << e.what() << ". Control Data=\n" <<
      control.StyledString();
  }
  control.SetResponse(resp, status);
  ctrl_link_->Deliver(control);
}

void
ControlDispatch::SendICC(
  TincanControl & control)
{
  Json::Value & req = control.GetRequest();
  lock_guard<mutex> lg(disp_mutex_);
  try
  {
    tincan_->SendIcc(req);
  } catch(exception & e)
  {
    string msg = "The ICC operation failed.";
    LOG_F(LS_WARNING) << e.what() << ". Control Data=\n" <<
      control.StyledString();
    //send fail here, ack success when send completes
    control.SetResponse(msg, false);
    ctrl_link_->Deliver(control);
  }
}
}  // namespace tincan
