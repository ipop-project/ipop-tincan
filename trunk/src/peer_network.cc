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
/*
Adds a new adjacent node to the peer network. This is used when a new vlink is
created. It also updates the UID map (for now).
*/
void PeerNetwork::Add(shared_ptr<VirtualLink> vlink)
{
  shared_ptr<Hub> hub = make_shared<Hub>();
  hub->vlink = vlink;
  hub->is_valid = true;
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
    lock_guard<mutex> lg(mac_map_mtx_);
    if(mac_map.count(mac) == 1)
    {
      LOG_F(LS_ERROR) << "Entry " << vlink->PeerInfo().mac_address <<
        " already exists in peer net. It will be updated.";
    }
    mac_map[mac] = hub;
  }
  {
    lock_guard<mutex> lg(uid_map_mtx_);
    uid_map[vlink->PeerInfo().uid] = hub;
  }
  LOG_F(LS_ERROR) << "Added node " << vlink->PeerInfo().mac_address;
  //LOG_F(LS_VERBOSE) << "Added node " << vlink->PeerInfo().mac_address;
}

void PeerNetwork::AddRoute(
  MacAddressType & dest,
  MacAddressType & route)
{
  lock_guard<mutex> lg(mac_map_mtx_);
  if(dest == route || mac_map.count(route) == 0 ||
    (mac_map.count(route) && !mac_map.at(route)->is_valid))
  {
    stringstream oss;
    oss << "Attempt to add INVALID route! DEST=" <<
      ByteArrayToString(dest.begin(), dest.end()) << " ROUTE=" <<
      ByteArrayToString(route.begin(), route.end());
    throw TCEXCEPT(oss.str().c_str());
  }
  if(mac_map.count(dest) == 1)
    mac_map.erase(dest);
  shared_ptr<Hub> hub = mac_map.at(route);
  mac_map[dest] = hub;
  LOG_F(LS_ERROR) << "Added route to node=" << 
    ByteArrayToString(dest.begin(), dest.end()) << " through node=" << 
    ByteArrayToString(route.begin(), route.end()) << " vlink obj=" << 
    hub->vlink.get();
}
/*
Used when a vlink is removed and the peer is no longer adjacent. All routes that
use this path must be removed as well.
*/
void
PeerNetwork::Remove(
  const string & peer_uid)
  //const MacAddressType & mac)
{
  try
  {
    lock_guard<mutex> lgu(uid_map_mtx_);
    auto peer = uid_map.find(peer_uid);
    shared_ptr<Hub> hub;
    if(peer != uid_map.end())
    {
      hub = peer->second;
      uid_map.erase(peer);
    }
    lock_guard<mutex> lg(mac_map_mtx_);
    MacAddressType mac;
    size_t nb = StringToByteArray(hub->vlink->PeerInfo().mac_address,
      mac.begin(), mac.end());
    if(sizeof(MacAddressType) != nb)
    {
      ostringstream oss;
      oss << "Failed to convert MAC address :" << hub->vlink->PeerInfo().mac_address;
      throw TCEXCEPT(oss.str().c_str());
    }
    hub->is_valid = false;
    hub->vlink.reset();
    mac_map.erase(mac); //remove the MAC for the adjacent node

    //things that happen implicitly
    //when hub goes out of scope ref count is decr, if it is 0 it is deleted 
    LOG_F(LS_ERROR) << "Removed node " << hub->vlink->PeerInfo().mac_address <<
      " hub use count=" << hub.use_count() << " vlink obj=" << hub->vlink.get();
    //LOG_F(LS_VERBOSE) << "Removed node " << hub->vlink->PeerInfo().mac_address;
  }
  catch(exception & e)
  {
    LOG_F(LS_WARNING) << e.what();
  }
  catch(...)
  {
    ostringstream oss;
    oss << "The Peer Network Remove operation failed. UID " << peer_uid
      << " was not found in peer network: " << name_;
    LOG_F(LS_WARNING) << oss.str().c_str();
  }
}

void
PeerNetwork::RemoveRoute(
  const MacAddressType & dest)
{
  lock_guard<mutex> lg(mac_map_mtx_);
  shared_ptr<Hub> hub = mac_map.at(dest);
  mac_map.erase(dest);
  LOG_F(LS_ERROR) << "Removed route to " << ByteArrayToString(dest.begin(), dest.end()) <<
    " hub use count=" << hub.use_count() << " vlink obj=" << hub->vlink.get();
}

shared_ptr<VirtualLink>
PeerNetwork::UidToVlink(
  const string & uid)
{
  lock_guard<mutex> lgu(uid_map_mtx_);
  return uid_map.at(uid)->vlink;
}

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
  return mac_map.at(mac)->vlink;
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

void
PeerNetwork::Run(Thread* thread)
{
  if(!thread->ProcessMessages(60000))
    return;
  {
    list<MacAddressType> ml;
    lock_guard<mutex> lgm(mac_map_mtx_);
    for(auto i : mac_map)
    {
      if(!i.second->is_valid)
        ml.push_back(i.first);
    }
    for(auto m : ml)
    {
      MacAddressType mac = ml.front();
      LOG_F(LS_ERROR) << "Scavenging route to " << ByteArrayToString(mac.begin(), mac.end());
      mac_map.erase(mac);
    }
  }
}
} // namespace tincan
