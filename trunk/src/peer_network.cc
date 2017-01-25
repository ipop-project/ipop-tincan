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
#include "peer_network.h"
#include "tincan_exception.h"
namespace tincan
{
PeerNetwork::PeerNetwork(
  const string & name) :
  name_(name)
{}

PeerNetwork::~PeerNetwork()
{}

void PeerNetwork::Add(shared_ptr<VirtualLink> vlink)
{
  uint32_t addr4;
  if(!inet_pton(AF_INET, vlink->PeerInfo().vip4.c_str(), &addr4))
    throw TCEXCEPT("Invalid IP4 address string specified");
  
  //in6_addr addr6;
  //if(!inet_pton(AF_INET6, vlink->PeerInfo().vip6.c_str(), &addr6))
  //  throw TCEXCEPT("Invalid IP6 address string specified");
  
  MacAddressType mac;
  size_t cnt = StringToByteArray(
    vlink->PeerInfo().mac_address, mac.begin(), mac.end());
  if(cnt != 6)
  {
    string emsg = "Converting the MAC to binary failed, the input string is: ";
    emsg.append(vlink->PeerInfo().mac_address);
    throw TCEXCEPT(emsg.c_str());
  }
  {
    lock_guard<mutex> lg(ip4_map_mtx_);
    ip4_map[addr4] = vlink;
  }
  //{
  //  lock_guard<mutex> lg(ip6_map_mutex);
  //  ip6_map[addr6] = vlink;
  //}
  {
    lock_guard<mutex> lg(mac_map_mtx_);
    mac_map[mac] = vlink;
  }
  {
    lock_guard<mutex> lg(uid_map_mtx_);
    uid_map[vlink->PeerInfo().uid] = vlink;
  }
}

shared_ptr<VirtualLink>
PeerNetwork::Remove(
  const string & peer_uid)
{
  LOG_F(LS_ERROR) << "Attempting to remove "<< peer_uid;
  auto peer = uid_map.find(peer_uid);

  lock_guard<mutex> lg4(ip4_map_mtx_);
  //lock_guard<mutex> lg6(ip6_map_mutex);
  lock_guard<mutex> lgm(mac_map_mtx_);
  lock_guard<mutex> lgu(uid_map_mtx_);
  shared_ptr<VirtualLink> vl;
  if(peer != uid_map.end())
  {
    vl = peer->second;
    uid_map.erase(vl->PeerInfo().uid);

    uint32_t addr4;
    if(!inet_pton(AF_INET, vl->PeerInfo().vip4.c_str(), &addr4))
      LOG_F(LS_ERROR) << "An invalid IP4 address string was encountred in the "
      << "VLink's Peerinfo. This is an anomoly. "
      << "The remove peer operation is incomplete.";
    else
      ip4_map.erase(addr4);

    //in6_addr addr6;
    //if(!inet_pton(AF_INET6, peer->second->PeerInfo().vip6.c_str(), &addr6))
    //  throw TCEXCEPT("Invalid IP6 address string specified");
    //ip6_map.erase(addr6);
    
    MacAddressType mac;
    size_t nb = StringToByteArray(vl->PeerInfo().mac_address, mac.begin(),
      mac.end());
    if(sizeof(MacAddressType) == nb)
    {
      mac_map.erase(mac);
    }
    else
    {
      LOG_F(LS_ERROR) << "Could not convert this MAC address :"
        << vl->PeerInfo().mac_address << ". Remove operation incomplete";
    }
  }
  else
  {
    ostringstream oss;
    oss << "Failed to peer with UID:" << peer_uid
      << "from peer network: " << name_ << ". It was not found";
    LOG_F(LS_INFO) << oss.str().c_str();
  }
  return vl;
}

shared_ptr<VirtualLink>
PeerNetwork::UidToVlink(
  const string & uid)
{
  lock_guard<mutex> lgu(uid_map_mtx_);
  return uid_map.at(uid);
}

shared_ptr<VirtualLink>
PeerNetwork::Ip4ToVlink(
  const string & ip4)
{
  int addr;
  if(!inet_pton(AF_INET, ip4.c_str(), &addr))
  {
    throw TCEXCEPT("Invalid IP4 address string specified");
  }
  return Ip4ToVlink(addr);
}

shared_ptr<VirtualLink> 
PeerNetwork::Ip4ToVlink(
  const uint32_t ip4)
{
  lock_guard<mutex> lg4(ip4_map_mtx_);
  return ip4_map.at(ip4);
}

//shared_ptr<VirtualLink>
//PeerNetwork::Ip6ToVlink(
//  const string & ip6) const
//{
//  struct in6_addr addr;
//  if(!inet_pton(AF_INET6, ip6.c_str(), &addr))
//  {
//    throw TCEXCEPT("Invalid IP6 address string specified");
//  }
//  return Ip6ToVlink(addr);
//}
//
//shared_ptr<VirtualLink>
//PeerNetwork::Ip6ToVlink(
//  const in6_addr & ip6) const
//{
//  return ip6_map.at(ip6);
//}

shared_ptr<VirtualLink> PeerNetwork::MacAddressToVlink(const string & mac)
{
  MacAddressType mac_arr;
  StringToByteArray(mac, mac_arr.begin(), mac_arr.end());
  return MacAddressToVlink(mac_arr);
}

shared_ptr<VirtualLink>
PeerNetwork::MacAddressToVlink(
  const MacAddressType& mac)
{
  lock_guard<mutex> lgm(mac_map_mtx_);
  return mac_map.at(mac);
}

bool
PeerNetwork::Exists(
  const string & id)
{
  lock_guard<mutex> lgu(uid_map_mtx_);
  return uid_map.count(id) == 1 ? true : false;
}

bool
PeerNetwork::Exists(
  const MacAddressType& mac)
{
  lock_guard<mutex> lgm(mac_map_mtx_);
  return mac_map.count(mac) == 1 ? true : false;
}

bool
PeerNetwork::Exists(
  uint32_t ip4)
{
  lock_guard<mutex> lg4(ip4_map_mtx_);
  return ip4_map.count(ip4) == 1 ? true : false;
}

} // namespace tincan
