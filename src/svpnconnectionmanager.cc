/*
 * svpn-jingle
 * Copyright 2013, University of Florida
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <iostream>
#include <sstream>

#include "talk/base/logging.h"
#include "talk/base/json.h"

#include "svpnconnectionmanager.h"

namespace sjingle {

static const char kContentName[] = "svpn-jingle";
static const bool kAllowTcpListen = false;
static const char kIceUfrag[] = "ufrag";
static const char kIcePwd[] = "pwd";
static const int kBufferSize = 1500;
static const char kTapName[] = "svpn";
static const uint32 kFlags = cricket::PORTALLOCATOR_DISABLE_TCP;
static SvpnConnectionManager* g_manager = 0;

enum {
  MSG_QUEUESIGNAL = 0
};

SvpnConnectionManager::SvpnConnectionManager(
    SocialSenderInterface* social_sender,
    talk_base::Thread* signaling_thread,
    talk_base::Thread* worker_thread,
    struct threadqueue* send_queue,
    struct threadqueue* rcv_queue)
    : content_name_(kContentName),
      social_sender_(social_sender),
      packet_factory_(worker_thread),
      uid_map_(),
      transport_map_(),
      signaling_thread_(signaling_thread),
      worker_thread_(worker_thread),
      network_manager_(),
      svpn_id_(),
      identity_(),
      local_fingerprint_(),
      fingerprint_(),
      send_queue_(send_queue),
      rcv_queue_(rcv_queue),
      tiebreaker_(talk_base::CreateRandomId64()),
      svpn_ip4_(),
      svpn_ip6_(),
      tap_name_(kTapName) {
  g_manager = this;
  network_manager_.SignalNetworksChanged.connect(
      this, &SvpnConnectionManager::OnNetworksChanged);
}

void SvpnConnectionManager::Setup(
    const std::string& uid, const std::string& ip4, int ip4_mask,
    const std::string& ip6, int ip6_mask) {
  if (!svpn_id_.empty()) return;
  svpn_id_ = uid;
  identity_.reset(talk_base::OpenSSLIdentity::Generate(svpn_id_));
  local_fingerprint_.reset(talk_base::SSLFingerprint::Create(
      talk_base::DIGEST_SHA_1, identity_.get()));
  fingerprint_ = local_fingerprint_->GetRfc4572Fingerprint();
  int error = tap_set_ipv4_addr(ip4.c_str(), ip4_mask);
  error |= tap_set_ipv6_addr(ip6.c_str(), ip6_mask);
  error |= tap_set_mtu(MTU) | tap_set_base_flags() | tap_set_up();
  error |= peerlist_set_local_p(uid.c_str(), ip4.c_str(), ip6.c_str());
  ASSERT(error == 0);
  svpn_ip4_ = ip4;
  svpn_ip6_ = ip6;
}

void SvpnConnectionManager::OnNetworksChanged() {
  talk_base::NetworkManager::NetworkList networks;
  talk_base::SocketAddress ip6_addr(svpn_ip6_, 0);
  network_manager_.GetNetworks(&networks);
  for (uint32 i = 0; i < networks.size(); ++i) {
    if (networks[i]->name().compare(kTapName) == 0) {
      networks[i]->ClearIPs();
      // Set to a random ipv6 address in order to disable
      networks[i]->AddIP(ip6_addr.ipaddr());
    }
  }
}

void SvpnConnectionManager::OnRequestSignaling(
    cricket::Transport* transport) {
  transport->OnSignalingReady();
}

void SvpnConnectionManager::OnRWChangeState(
    cricket::Transport* transport) {
  std::string uid = transport_map_[transport];
  std::string status = "unknown";
  if (transport->readable() && transport->writable()) {
    status = "online";
    LOG_F(INFO) << "ONLINE " << uid << " " << talk_base::Time();
  }
  else if (transport->was_writable()) {
    status = "offline";
    LOG_F(INFO) << "OFFLINE " << uid << " " << talk_base::Time();
  }

  // TODO - For now, nid = 0 is the controller
  int nid = 0;
  social_sender_->SendToPeer(nid, uid, status);
}

void SvpnConnectionManager::OnCandidatesReady(
    cricket::Transport* transport, const cricket::Candidates& candidates) {
  std::string uid = transport_map_[transport];
  std::set<std::string>& candidate_list = uid_map_[uid]->candidate_list;
  for (int i = 0; i < candidates.size(); i++) {
    if (candidates[i].network_name().compare(kTapName) == 0) continue;
    std::ostringstream oss;
    std::string ip_string =
        talk_base::SocketAddress::IPToString(candidates[i].address().ip());
    oss << candidates[i].id() << ":" << candidates[i].component()
        << ":" << candidates[i].protocol() << ":" << ip_string
        << ":" << candidates[i].address().port() 
        << ":" << candidates[i].priority() 
        << ":" << candidates[i].username() 
        << ":" << candidates[i].password() 
        << ":" << candidates[i].type() 
        << ":" << candidates[i].network_name() 
        << ":" << candidates[i].generation() 
        << ":" << candidates[i].foundation(); 
    candidate_list.insert(oss.str());
  }
}

void SvpnConnectionManager::OnCandidatesAllocationDone(
    cricket::Transport* transport) {
  std::string uid = transport_map_[transport];
  std::set<std::string>& candidates = uid_map_[uid]->candidate_list;
  int nid = uid_map_[uid]->nid;
  std::string data(fingerprint());
  for (std::set<std::string>::const_iterator it = candidates.begin();
       it != candidates.end(); ++it) {
    data += " ";
    data += *it;
  }
  if (transport_map_.find(transport) != transport_map_.end()) {
    social_sender_->SendToPeer(nid, transport_map_[transport], data);
  }
}

void SvpnConnectionManager::OnReadPacket(cricket::TransportChannel* channel, 
    const char* data, size_t len, int flags) {
  if (len < kHeaderSize) return;
  const char* dest_id = data + kIdSize + 2;
  std::string source(data, kIdSize);
  std::string dest(dest_id, kIdSize);
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  if (uid_map_.find(source) != uid_map_.end() && 
      uid_map_[source]->transport->GetChannel(component) == channel) {
    int count = thread_queue_bput(rcv_queue_, data, len);
  }
}

void SvpnConnectionManager::HandlePacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  if (len < (kHeaderSize)) return;
  const char* dest_id = data + kIdSize + 2;
  std::string source(data, kIdSize);
  std::string dest(dest_id, kIdSize);
  if (uid_map_.find(dest) != uid_map_.end()) {
    int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
    cricket::TransportChannelImpl* channel = 
        uid_map_[dest]->transport->GetChannel(component);
    int count = channel->SendPacket(data, len, 0);
  }
}

void SvpnConnectionManager::SetupTransport(PeerState* peer_state) {
  peer_state->transport->SetTiebreaker(tiebreaker_);
  peer_state->remote_fingerprint.reset(
      talk_base::SSLFingerprint::CreateFromRfc4572(talk_base::DIGEST_SHA_1,
                                                   peer_state->fingerprint));
  peer_state->local_description.reset(new cricket::TransportDescription(
      cricket::NS_JINGLE_ICE_UDP, std::vector<std::string>(), kIceUfrag,
      kIcePwd, cricket::ICEMODE_FULL, local_fingerprint_.get(), 
      peer_state->candidates));
  peer_state->remote_description.reset(new cricket::TransportDescription(
      cricket::NS_JINGLE_ICE_UDP, std::vector<std::string>(), kIceUfrag, 
      kIcePwd, cricket::ICEMODE_FULL, peer_state->remote_fingerprint.get(), 
      peer_state->candidates));

  if (peer_state->uid.compare(svpn_id_) < 0) {
    peer_state->transport->SetRole(cricket::ROLE_CONTROLLING);
    peer_state->transport->SetLocalTransportDescription(
        *peer_state->local_description, cricket::CA_OFFER);
    peer_state->transport->SetRemoteTransportDescription(
        *peer_state->remote_description, cricket::CA_ANSWER);
  }
  else {
    peer_state->transport->SetRole(cricket::ROLE_CONTROLLED);
    peer_state->transport->SetRemoteTransportDescription(
        *peer_state->remote_description, cricket::CA_OFFER);
    peer_state->transport->SetLocalTransportDescription(
        *peer_state->local_description, cricket::CA_ANSWER);
  }
}

bool SvpnConnectionManager::CreateTransport(
    const std::string& uid, const std::string& fingerprint, int nid,
    const std::string& stun_server, const std::string& turn_server,
    const bool sec_enabled) {
  if (uid_map_.find(uid) != uid_map_.end()) {
    LOG_F(INFO) << "EXISTS " << uid;
    return false;
  }

  LOG_F(INFO) << uid << " " << talk_base::Time();
  talk_base::SocketAddress stun_addr;
  stun_addr.FromString(stun_server);
  PeerStatePtr peer_state(new talk_base::RefCountedObject<PeerState>);
  peer_state->uid = uid;
  peer_state->fingerprint = fingerprint;
  peer_state->nid = nid;
  peer_state->port_allocator.reset(new cricket::BasicPortAllocator(
      &network_manager_, &packet_factory_, stun_addr));
  peer_state->port_allocator->set_flags(kFlags);
  peer_state->port_allocator->set_allow_tcp_listen(kAllowTcpListen);

  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  cricket::TransportChannelImpl* channel;
  if (sec_enabled) {
    peer_state->transport.reset(new DtlsP2PTransport(
        signaling_thread_, worker_thread_, content_name_, 
        peer_state->port_allocator.get(), identity_.get()));
    channel = static_cast<cricket::DtlsTransportChannelWrapper*>(
        peer_state->transport->CreateChannel(component));
  }
  else {
    peer_state->transport.reset(new cricket::P2PTransport(
        signaling_thread_, worker_thread_, content_name_, 
        peer_state->port_allocator.get()));
    channel = peer_state->transport->CreateChannel(component);
  }

  channel->SignalReadPacket.connect(
    this, &SvpnConnectionManager::OnReadPacket);
  peer_state->transport->SignalRequestSignaling.connect(
      this, &SvpnConnectionManager::OnRequestSignaling);
  peer_state->transport->SignalCandidatesReady.connect(
      this, &SvpnConnectionManager::OnCandidatesReady);
  peer_state->transport->SignalCandidatesAllocationDone.connect(
      this, &SvpnConnectionManager::OnCandidatesAllocationDone);
  peer_state->transport->SignalReadableState.connect(
      this, &SvpnConnectionManager::OnRWChangeState);
  peer_state->transport->SignalWritableState.connect(
      this, &SvpnConnectionManager::OnRWChangeState);

  SetupTransport(peer_state.get());
  peer_state->transport->ConnectChannels();
  uid_map_[uid] = peer_state;
  transport_map_[peer_state->transport.get()] = uid;
  return true;
}

bool SvpnConnectionManager::AddIP(
    const std::string& uid, const std::string& ip4, const std::string& ip6) {
  if (ip_map_.find(uid) != ip_map_.end())  return false;
  override_base_ipv4_addr_p(ip4.c_str());
  peerlist_add_p(uid.c_str(), ip4.c_str(), ip6.c_str(), 0);
  ip_map_[uid] = ip4;
  return true;
}

bool SvpnConnectionManager::CreateConnections(
    const std::string& uid, const std::string& candidates_string) {
  if (uid_map_.find(uid) == uid_map_.end()) return false;
  cricket::Candidates& candidates = uid_map_[uid]->candidates;
  if (candidates.size() > 0) return false;
  std::istringstream iss(candidates_string);
  do {
    std::string candidate_string;
    iss >> candidate_string;
    std::vector<std::string> fields;
    size_t len = talk_base::split(candidate_string, ':', &fields);
    if (len >= 12) {
      cricket::Candidate candidate(
          fields[0], atoi(fields[1].c_str()), fields[2],
          talk_base::SocketAddress(fields[3], atoi(fields[4].c_str())), 
          atoi(fields[5].c_str()), fields[6], fields[7], fields[8],
          fields[9], atoi(fields[10].c_str()), fields[11]);
      candidates.push_back(candidate);
    }
  } while (iss);
  uid_map_[uid]->transport->OnRemoteCandidates(candidates);
  return true;
}

bool SvpnConnectionManager::DestroyTransport(const std::string& uid) {
  if (uid_map_.find(uid) == uid_map_.end()) return false;
  if (uid_map_[uid]->transport->was_writable()) {
    uid_map_[uid]->port_allocator.release();
  }
  uid_map_.erase(uid);
  LOG_F(INFO) << "DESTROYED " << uid;
  return true;
}

void SvpnConnectionManager::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_QUEUESIGNAL: {
        HandleQueueSignal_w(0);
      }
      break;
  }
}

void SvpnConnectionManager::HandlePeer(const std::string& uid,
                                       const std::string& data) {
  // TODO - For now, nid = 0 is the controller
  int nid = 0;
  if (data.size() == fingerprint().size()) {
    social_sender_->SendToPeer(nid, uid, data);
  }
  else if (data.size() > fingerprint().size()) {
    social_sender_->SendToPeer(
        nid, uid, data.substr(0, fingerprint().size()));
    CreateConnections(uid, data.substr(fingerprint().size()));
  }
  LOG_F(INFO) << uid << " " << data;
}

void SvpnConnectionManager::HandleQueueSignal(struct threadqueue *queue) {
  if (g_manager != 0) {
    g_manager->worker_thread()->Post(g_manager, MSG_QUEUESIGNAL, 0);
  }
}

void SvpnConnectionManager::HandleQueueSignal_w(struct threadqueue *queue) {
  char buf[kBufferSize];
  int len = thread_queue_bget(send_queue_, buf, sizeof(buf));
  if (len > 0) {
    HandlePacket(0, buf, len, talk_base::SocketAddress());
  }
}

std::string SvpnConnectionManager::GetState() {
  Json::Value state(Json::objectValue);
  Json::Value peers(Json::objectValue);
  for (std::map<std::string, std::string>::const_iterator it =
       ip_map_.begin(); it != ip_map_.end(); ++it) {
    std::string uid = it->first;
    Json::Value peer(Json::objectValue);
    peer["uid"] = it->first;
    peer["ip4"] = it->second;
    peer["status"] = "offline";
    if (uid_map_.find(uid) != uid_map_.end()) {
      peer["fpr"] = uid_map_[uid]->fingerprint;
      if (uid_map_[uid]->transport->all_channels_readable() &&
          uid_map_[uid]->transport->all_channels_writable()) {
        peer["status"] = "online";
      }
    }
    peers[it->first] = peer;
  }
  state["_uid"] = svpn_id_;
  state["_fpr"] = fingerprint_;
  state["_ip4"] = svpn_ip4_;
  state["peers"] = peers;
  return state.toStyledString();
}

}  // namespace sjingle

