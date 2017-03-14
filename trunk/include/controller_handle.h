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
#ifndef TINCAN_CONTROLLER_HANDLE_H_
#define TINCAN_CONTROLLER_HANDLE_H_
#include "tincan_base.h"
#include "tincan_control.h"
#include "vnet_descriptor.h"

namespace tincan {
  class IpopControllerLink
  {
  public:
    virtual void Deliver(
      TincanControl & ctrl_resp) = 0;

    virtual void Deliver(
      unique_ptr<TincanControl> ctrl_resp) = 0;
  };

  class DispatchToListenerInf
  {
  public:
    virtual void CreateIpopControllerLink(
      unique_ptr<SocketAddress> controller_addr) = 0;
    virtual IpopControllerLink & GetIpopControllerLink() = 0;
  };

  class TincanDispatchInterface
  {
  public:
    virtual void ConnectToPeer(
      const Json::Value & link_desc) = 0;

    virtual void CreateVNet(
      unique_ptr<VnetDescriptor> lvecfg) = 0;

    virtual void CreateVlinkListener(
      const Json::Value & link_desc,
      TincanControl & ctrl) = 0;
    
    virtual void InjectFrame(
      const Json::Value & frame_desc) = 0;

    virtual void QueryNodeInfo(
      const string & tap_name,
      const string & uid_node,
      Json::Value & state_data) = 0;

    virtual void RemoveVlink(
      const Json::Value & link_desc) = 0;

    virtual void SendIcc(
      const Json::Value & icc_desc) = 0;

    virtual void SetIgnoredNetworkInterfaces(
      const string & tap_name,
      vector<string> & ignored_list) = 0;

    virtual void SetIpopControllerLink(
      shared_ptr<IpopControllerLink> ctrl_link) = 0;

    virtual void UpdateRoute(
      const string & tap_name,
      const string & dest_mac,
      const string & path_mac) = 0;
};
}  // namespace tincan
#endif  // TINCAN_CONTROLLER_HANDLE_H_
