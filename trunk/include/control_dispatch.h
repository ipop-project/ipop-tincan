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
#ifndef TINCAN_CONTROL_DISPATCH_H_
#define TINCAN_CONTROL_DISPATCH_H_
#include "tincan_base.h"
#pragma warning( push )
#pragma warning(disable:4996)
#include "webrtc/base/logging.h"
#pragma warning(pop)
#include <map>
#include <memory>
#include <mutex>
#include "controller_handle.h"
#include "tap_frame.h"
#include "tincan_control.h"
namespace tincan
{
class ControlDispatch
{
public:
  ControlDispatch();
  ~ControlDispatch();
  void operator () (TincanControl & control);
  void SetDispatchToTincanInf(TincanDispatchInterface * dtot);
  void SetDispatchToListenerInf(DispatchToListenerInf * dtol);

private:
  void UpdateRoutes(TincanControl & control);
  void ConnectToPeer(TincanControl & control);
  void CreateIpopControllerRespLink(TincanControl & control);
  void CreateLinkListener(TincanControl & control);
  void CreateVNet(TincanControl & control);
  void Echo(TincanControl & control);
  void InjectFrame(TincanControl & control);
  void QueryNodeInfo(TincanControl & control);
  void QueryStunCandidates(TincanControl & control);
  void RemovePeer(TincanControl & control);
  void SetLogLevel(TincanControl & control);
  void SetNetworkIgnoreList(TincanControl & control);
  void SendICC(TincanControl & control);

  map<std::string, void (ControlDispatch::*)(TincanControl & control)>control_map_;
  DispatchToListenerInf * dtol_;
  TincanDispatchInterface * tincan_;
  shared_ptr<IpopControllerLink> ctrl_link_;
  std::mutex disp_mutex_;
  class DisconnectedControllerHandle : virtual public IpopControllerLink {
  public:
    DisconnectedControllerHandle() {
      msg_ = "No connection to Controller exists. "
        "Create one with the set_ctrl_endpoint control operation";
    }
  private:
    virtual void Deliver(
      TincanControl & ctrl_resp) {
      LOG_F(LS_INFO) << msg_ << endl;
    }
    virtual void Deliver(
      unique_ptr<TincanControl> ctrl_resp)
    {
      LOG_F(LS_INFO) << msg_ << endl;
    }
    string msg_;
  };
}; // ControlDispatch
}  // namespace tincan
#endif  // TINCAN_CONTROL_DISPATCH_H_
