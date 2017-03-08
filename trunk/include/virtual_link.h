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
#ifndef TINCAN_VIRTUAL_LINK_H_
#define TINCAN_VIRTUAL_LINK_H_
#include "tincan_base.h"
#pragma warning( push )
#pragma warning(disable:4459)
#pragma warning(disable:4996)
#include "webrtc/base/asyncpacketsocket.h"
#include "webrtc/base/json.h"
#include "webrtc/base/network.h"
#include "webrtc/base/sslfingerprint.h"
#include "webrtc/base/thread.h"
#include "webrtc/p2p/base/basicpacketsocketfactory.h"
#include "webrtc/p2p/base/dtlstransport.h"
#include "webrtc/p2p/base/p2ptransport.h"
#include "webrtc/p2p/base/p2ptransportchannel.h"
#include "webrtc/p2p/client/basicportallocator.h"
#pragma warning( pop )
#include "tap_frame.h"
#include "peer_descriptor.h"

namespace tincan
{
using namespace rtc;
using DtlsP2PTransport = cricket::DtlsTransport<cricket::P2PTransport>;

struct  VlinkDescriptor
{
  bool sec_enabled;
  string name;
  string stun_addr;
  string turn_addr;
  string turn_user;
  string turn_pass;
};

class VirtualLink :
  public sigslot::has_slots<>
{
public:
  VirtualLink(
    unique_ptr<VlinkDescriptor> vlink_desc,
    unique_ptr<PeerDescriptor> peer_desc);
  ~VirtualLink();

  string Name();

  void Initialize(
    const string & local_uid,
    BasicNetworkManager & network_manager,
    scoped_ptr<SSLIdentity>sslid,
    SSLFingerprint const & local_fingerprint);

  PeerDescriptor& PeerInfo()
  {
    return *peer_desc_.get();
  }

  void StartConnections();

  void Disconnect();

  bool IsReady();

  void Transmit(TapFrame & frame);

  string Candidates();

  void PeerCandidates(const string & peer_cas);

  void GetStats(Json::Value & infos);

  sigslot::signal1<VirtualLink&> SignalLinkReady;
  sigslot::signal1<VirtualLink&> SignalLinkBroken;
  sigslot::signal1<string> SignalLocalCasReady;
  sigslot::signal3<uint8_t *, uint32_t, VirtualLink&>
    SignalMessageReceived;
private:
  void SetupTransport(
    BasicNetworkManager & network_manager,
    scoped_ptr<SSLIdentity>sslid);

  void SetupTURN(
    const string & turn_server,
    const string & username,
    const std::string & password);

  void OnCandidateGathered(
    cricket::TransportChannelImpl * ch,
    const cricket::Candidate & cnd);

  void OnGatheringState(
    cricket::TransportChannelImpl * channel);

  void OnWriteableState(
    cricket::TransportChannel * channel);

  void RegisterLinkEventHandlers();

  void AddRemoteCandidates(
    const string & candidates);

  void SetupICE(
    const string & local_uid,
    SSLFingerprint const & local_fingerprint);

  void OnReadPacket(
    cricket::TransportChannel* channel,
    const char* data,
    size_t len,
    const PacketTime & ptime,
    int flags);

  void OnSentPacket(
    cricket::TransportChannel * channel,
    const SentPacket & packet);

  unique_ptr<VlinkDescriptor> vlink_desc_;
  unique_ptr<PeerDescriptor> peer_desc_;
  BasicPacketSocketFactory packet_factory_;
  unique_ptr<cricket::BasicPortAllocator> port_allocator_;
  unique_ptr<cricket::P2PTransport> transport_;
  std::list<string> local_candidates_;
  const uint64_t tiebreaker_;
  cricket::ConnectionRole ice_conn_role_;

  unique_ptr<cricket::TransportDescription> local_description_;
  unique_ptr<cricket::TransportDescription> remote_description_;

  unique_ptr<SSLFingerprint> remote_fingerprint_;
  string content_name_;
  PacketOptions packet_options_;

  std::mutex cas_mutex_;
  bool cas_ready_;
};
} //namespace tincan
#endif // !TINCAN_VIRTUAL_LINK_H_
