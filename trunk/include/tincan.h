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
#ifndef TINCAN_TINCAN_H_
#define TINCAN_TINCAN_H_
#include "tincan_base.h"
#pragma warning( push )
#pragma warning(disable:4996)
#include "webrtc/base/event.h"
#pragma warning( pop )
#include "control_listener.h"
#include "control_dispatch.h"
#include "virtual_network.h"

namespace tincan {
class Tincan :
  public TincanDispatchInterface,
  public sigslot::has_slots<>
{
public:
  Tincan();
  ~Tincan();
  //
  //TincanDispatchInterface interface

  void ConnectToPeer(
    const Json::Value & link_desc) override;

  void CreateVlinkListener(
    const Json::Value & link_desc,
    TincanControl & ctrl) override;

  void CreateVNet(
    unique_ptr<VnetDescriptor> lvecfg) override;
  
  void InjectFrame(
    const Json::Value & frame_desc) override;

  void QueryNodeInfo(
    const string & tap_name,
    const string & node_mac,
    Json::Value & node_info) override;

  void RemoveVlink(
    const Json::Value & link_desc) override;

  void SendIcc(
    const Json::Value & icc_desc) override;

  void SetIgnoredNetworkInterfaces(
    const string & tap_name,
    vector<string> & ignored_list) override;

  void SetIpopControllerLink(
    shared_ptr<IpopControllerLink> ctrl_handle) override;

  void UpdateRoute(
    const string & tap_name,
    const string & dest_mac,
    const string & path_mac) override;
//
//
  void OnLocalCasUpdated(
    string lcas);

  void Run();
private:
  void GetLocalNodeInfo(
    VirtualNetwork & vnet,
    Json::Value & node_info);

  VirtualNetwork & VnetFromName(
    const string & tap_name);

  void OnStop();
  void Shutdown();
  //TODO:Code cleanup
#if defined(_IPOP_WIN)
  static BOOL __stdcall ControlHandler(
    DWORD CtrlType);
#endif // _IPOP_WIN

  vector<unique_ptr<VirtualNetwork>> vnets_;
  ControlListener * ctrl_listener_;
  shared_ptr<IpopControllerLink> ctrl_link_;
  list<unique_ptr<TincanControl>> inprogess_controls_;
  Thread ctl_thread_;
  static Tincan * self_;
  std::mutex vnets_mutex_;
  std::mutex inprogess_controls_mutex_;
  rtc::Event exit_event_;

};
} //namespace tincan
#endif //TINCAN_TINCAN_H_

