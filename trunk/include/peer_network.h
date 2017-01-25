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
class PeerNetwork
{
public:
  PeerNetwork(
    const string & name);
  ~PeerNetwork();
  void Add(shared_ptr<VirtualLink> vlink);
  shared_ptr<VirtualLink> Remove(const string & peer_uid);
  shared_ptr<VirtualLink> UidToVlink(const string & id);
  shared_ptr<VirtualLink> Ip4ToVlink(const string & ip4);
  shared_ptr<VirtualLink> Ip4ToVlink(const uint32_t ip4);
  //shared_ptr<VirtualLink> Ip6ToVlink(const string & ip6);
  //shared_ptr<VirtualLink> Ip6ToVlink(const struct in6_addr & ip6);
  shared_ptr<VirtualLink> MacAddressToVlink(const string & mac);
  shared_ptr<VirtualLink> MacAddressToVlink(const MacAddressType & mac);
  bool Exists(const string & id);
  bool Exists(const MacAddressType& mac);
  bool Exists(uint32_t ip4);
private:
  const string & name_;
  //map by ip4
  mutex ip4_map_mtx_;
  map<uint32_t, shared_ptr<VirtualLink>> ip4_map;
  //map by ip6
  //mutex ip6_map_mtx_;
  //map<struct in6_addr, shared_ptr<VirtualLink>> ip6_map;
  //map by mac address
  mutex mac_map_mtx_;
  map<MacAddressType, shared_ptr<VirtualLink>> mac_map;
  //map by uid
  mutex uid_map_mtx_;
  map<string, shared_ptr<VirtualLink>> uid_map;
};
} // namespace tincan
#endif //_TINCAN_PEER_NETWORK_H_
