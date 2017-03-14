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
#if !defined(_TINCAN_PEER_NETWORK_H_)
#define _TINCAN_PEER_NETWORK_H_
#include "tincan_base.h"
#include "virtual_link.h"
namespace tincan
{
class PeerNetwork :
  public Runnable
{
public:
  PeerNetwork(
    const string & name);
  ~PeerNetwork();
  void Add(shared_ptr<VirtualLink> vlink);
  void UpdateRoute(MacAddressType & dest, MacAddressType & route);
  void Remove(MacAddressType mac);
  shared_ptr<VirtualLink> GetVlink(const string & mac);
  shared_ptr<VirtualLink> GetVlink(const MacAddressType & mac);
  shared_ptr<VirtualLink> GetRoute(const MacAddressType & mac);
  bool IsAdjacent(const string & mac);
  bool IsAdjacent(const MacAddressType& mac);
  bool IsRouteExists(const MacAddressType& mac);
private:
struct Hub
{
  Hub() : is_valid(false)
  {}
  shared_ptr<VirtualLink> vlink;
  bool is_valid;
};
struct HubEx
{
  HubEx() : accessed(steady_clock::now())
  {}
  shared_ptr<Hub> hub;
  steady_clock::time_point accessed;
};
  const string & name_;
  mutex mac_map_mtx_;
  map<MacAddressType, shared_ptr<Hub>> mac_map_;
  map<MacAddressType, HubEx> mac_routes_;
  void Run(Thread* thread) override;
  milliseconds const scavenge_interval;
};
} // namespace tincan
#endif //_TINCAN_PEER_NETWORK_H_
