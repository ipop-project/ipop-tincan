/*
 * ipop-tincan
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

#include "tincanconnectionmanager.h"

namespace tincan {

static const char kIpv4[] = "172.31.0.100";
static const char kIpv6[] = "fd50:0dbc:41f2:4a3c:0000:0000:0000:0000";
static const char kContentName[] = "ipop-tincan";
static const char kIceUfrag[] = "ufrag";
static const char kIcePwd[] = "pwd";
static const int kBufferSize = 1500;
static const char kTapName[] = "ipop";
static const size_t kIdBytesLen = 20;
static const size_t kShortLen = 8;
static const uint32 kFlags = 0;
static const uint32 kLocalControllerId = 0;
static TinCanConnectionManager* g_manager = 0;

enum {
  MSG_QUEUESIGNAL = 0,
  MSG_CONTROLLERSIGNAL = 1
};

TinCanConnectionManager::TinCanConnectionManager(
    PeerSignalSenderInterface* signal_sender,
    talk_base::Thread* link_setup_thread,
    talk_base::Thread* packet_handling_thread,
    struct threadqueue* send_queue,
    struct threadqueue* rcv_queue,
    struct threadqueue* controller_queue)
    : content_name_(kContentName),
      signal_sender_(signal_sender),
      packet_factory_(packet_handling_thread),
      uid_map_(),
      short_uid_map_(),
      transport_map_(),
      link_setup_thread_(link_setup_thread),
      packet_handling_thread_(packet_handling_thread),
      network_manager_(),
      tincan_id_(),
      identity_(),
      local_fingerprint_(),
      fingerprint_(),
      send_queue_(send_queue),
      rcv_queue_(rcv_queue),
      controller_queue_(controller_queue),
      tiebreaker_(talk_base::CreateRandomId64()),
      tincan_ip4_(kIpv4),
      tincan_ip6_(kIpv6),
      tap_name_(kTapName) {
  g_manager = this;
  network_manager_.SignalNetworksChanged.connect(
      this, &TinCanConnectionManager::OnNetworksChanged);
}

void TinCanConnectionManager::Setup(
    const std::string& uid, const std::string& ip4, int ip4_mask,
    const std::string& ip6, int ip6_mask) {
  if (!tincan_id_.empty() || uid.size() != kIdSize) return;
  tincan_id_ = uid;
  char uid_str[kIdBytesLen];
  talk_base::hex_decode(uid_str, kIdBytesLen, uid);
  identity_.reset(talk_base::OpenSSLIdentity::Generate(tincan_id_));
  local_fingerprint_.reset(talk_base::SSLFingerprint::Create(
      talk_base::DIGEST_SHA_1, identity_.get()));
  fingerprint_ = local_fingerprint_->GetRfc4572Fingerprint();
  int error = tap_set_ipv4_addr(ip4.c_str(), ip4_mask);
  error |= tap_set_ipv6_addr(ip6.c_str(), ip6_mask);
  error |= tap_set_mtu(MTU) | tap_set_base_flags() | tap_set_up();
  error |= peerlist_set_local_p(uid_str, ip4.c_str(), ip6.c_str());
  ASSERT(error == 0);
  tincan_ip4_ = ip4;
  tincan_ip6_ = ip6;
}

void TinCanConnectionManager::OnNetworksChanged() {
  talk_base::NetworkManager::NetworkList networks;
  talk_base::SocketAddress ip6_addr(tincan_ip6_, 0);
  network_manager_.GetNetworks(&networks);
  for (uint32 i = 0; i < networks.size(); ++i) {
    if (networks[i]->name().compare(kTapName) == 0) {
      networks[i]->ClearIPs();
      // Set to a random ipv6 address in order to disable
      networks[i]->AddIP(ip6_addr.ipaddr());
    }
  }
}

void TinCanConnectionManager::OnRequestSignaling(
    cricket::Transport* transport) {
  transport->OnSignalingReady();
}

void TinCanConnectionManager::OnRWChangeState(
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
  // callback message sent to local controller for connection status
  signal_sender_->SendToPeer(kLocalControllerId, uid, status, "con_stat");
}

void TinCanConnectionManager::OnCandidatesReady(
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

void TinCanConnectionManager::OnCandidatesAllocationDone(
    cricket::Transport* transport) {
  std::string uid = transport_map_[transport];
  std::set<std::string>& candidates = uid_map_[uid]->candidate_list;
  int overlay_id = uid_map_[uid]->overlay_id;
  std::string data(fingerprint());
  for (std::set<std::string>::const_iterator it = candidates.begin();
       it != candidates.end(); ++it) {
    data += " ";
    data += *it;
  }
  if (transport_map_.find(transport) != transport_map_.end()) {
    signal_sender_->SendToPeer(overlay_id, transport_map_[transport],
                               data, "con_resp");
  }
}

void TinCanConnectionManager::OnReadPacket(cricket::TransportChannel* channel, 
    const char* data, size_t len, int flags) {
  if (len < kHeaderSize) return;
  std::string source = talk_base::hex_encode(data, kShortLen);
  std::string dest = talk_base::hex_encode(data + kIdBytesLen, kShortLen);
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  if (short_uid_map_.find(source) != short_uid_map_.end() && 
      short_uid_map_[source]->GetChannel(component) == channel) {
    int count = thread_queue_bput(rcv_queue_, data, len);
  }
}

void TinCanConnectionManager::HandlePacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  if (len < (kHeaderSize)) return;
  std::string source = talk_base::hex_encode(data, kShortLen);
  std::string dest = talk_base::hex_encode(data + kIdBytesLen, kShortLen);
  if (dest.compare(0, 3, "000") == 0) {
    forward_socket_->SendTo(data, len, forward_addr_);
  } 
  else if (short_uid_map_.find(dest) != short_uid_map_.end() &&
           short_uid_map_[dest]->writable()) {
    int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
    cricket::TransportChannelImpl* channel = 
        short_uid_map_[dest]->GetChannel(component);
    int count = channel->SendPacket(data, len, 0);
  }
}

bool TinCanConnectionManager::SetRelay(PeerState* peer_state,
                                     const std::string& turn_server,
                                     const std::string& username, 
                                     const std::string& password) {
  if (turn_server.empty() || username.empty()) return false;
  talk_base::SocketAddress turn_addr;
  turn_addr.FromString(turn_server);
  cricket::RelayServerConfig relay_config_udp(cricket::RELAY_TURN);
  cricket::RelayServerConfig relay_config_tcp(cricket::RELAY_TURN);
  relay_config_udp.ports.push_back(cricket::ProtocolAddress(
      turn_addr, cricket::PROTO_UDP));
  relay_config_udp.credentials.username = username;
  relay_config_udp.credentials.password = password;
  relay_config_tcp.ports.push_back(cricket::ProtocolAddress(
      turn_addr, cricket::PROTO_TCP));
  relay_config_tcp.credentials.username = username;
  relay_config_tcp.credentials.password = password;
  if (!relay_config_udp.credentials.username.empty()) {
    peer_state->port_allocator->AddRelay(relay_config_udp);
    // TODO - TCP relaying needs more testing
    //peer_state->port_allocator->AddRelay(relay_config_tcp);
  }
  LOG_F(INFO) << "TURN " << turn_addr.ToString();
  return true;
}

void TinCanConnectionManager::SetupTransport(PeerState* peer_state) {
  peer_state->transport->SetIceTiebreaker(tiebreaker_);
  peer_state->remote_fingerprint.reset(
      talk_base::SSLFingerprint::CreateFromRfc4572(talk_base::DIGEST_SHA_1,
                                                   peer_state->fingerprint));

  cricket::ConnectionRole conn_role_local = cricket::CONNECTIONROLE_ACTPASS;
  if (peer_state->uid.compare(tincan_id_) > 0) {
    conn_role_local = cricket::CONNECTIONROLE_ACTIVE;
  }
  peer_state->local_description.reset(new cricket::TransportDescription(
      cricket::NS_JINGLE_ICE_UDP, std::vector<std::string>(), kIceUfrag,
      kIcePwd, cricket::ICEMODE_FULL, conn_role_local,
      local_fingerprint_.get(), peer_state->candidates));
  peer_state->remote_description.reset(new cricket::TransportDescription(
      cricket::NS_JINGLE_ICE_UDP, std::vector<std::string>(), kIceUfrag, 
      kIcePwd, cricket::ICEMODE_FULL, cricket::CONNECTIONROLE_NONE,
      peer_state->remote_fingerprint.get(), peer_state->candidates));

  if (peer_state->uid.compare(tincan_id_) < 0) {
    peer_state->transport->SetIceRole(cricket::ICEROLE_CONTROLLING);
    peer_state->transport->SetLocalTransportDescription(
        *peer_state->local_description, cricket::CA_OFFER);
    peer_state->transport->SetRemoteTransportDescription(
        *peer_state->remote_description, cricket::CA_ANSWER);
  }
  else {
    peer_state->transport->SetIceRole(cricket::ICEROLE_CONTROLLED);
    peer_state->transport->SetRemoteTransportDescription(
        *peer_state->remote_description, cricket::CA_OFFER);
    peer_state->transport->SetLocalTransportDescription(
        *peer_state->local_description, cricket::CA_ANSWER);
  }
}

bool TinCanConnectionManager::CreateTransport(
    const std::string& uid, const std::string& fingerprint, int overlay_id,
    const std::string& stun_server, const std::string& turn_server,
    const std::string& turn_user, const std::string& turn_pass,
    const bool sec_enabled) {
  if (uid_map_.find(uid) != uid_map_.end() || tincan_id_ == uid) {
    LOG_F(INFO) << "EXISTS " << uid;
    return false;
  }

  LOG_F(INFO) << uid << " " << talk_base::Time();
  talk_base::SocketAddress stun_addr;
  stun_addr.FromString(stun_server);
  PeerStatePtr peer_state(new talk_base::RefCountedObject<PeerState>);
  peer_state->uid = uid;
  peer_state->fingerprint = fingerprint;
  peer_state->overlay_id = overlay_id;
  peer_state->last_time = talk_base::Time();
  peer_state->port_allocator.reset(new cricket::BasicPortAllocator(
      &network_manager_, &packet_factory_, stun_addr));
  peer_state->port_allocator->set_flags(kFlags);
  SetRelay(peer_state.get(), turn_server, turn_user, turn_pass);

  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  cricket::TransportChannelImpl* channel;
  if (sec_enabled) {
    peer_state->transport.reset(new DtlsP2PTransport(
        link_setup_thread_, packet_handling_thread_, content_name_, 
        peer_state->port_allocator.get(), identity_.get()));
    channel = static_cast<cricket::DtlsTransportChannelWrapper*>(
        peer_state->transport->CreateChannel(component));
  }
  else {
    peer_state->transport.reset(new cricket::P2PTransport(
        link_setup_thread_, packet_handling_thread_, content_name_, 
        peer_state->port_allocator.get()));
    channel = peer_state->transport->CreateChannel(component);
  }

  channel->SignalReadPacket.connect(
    this, &TinCanConnectionManager::OnReadPacket);
  peer_state->transport->SignalRequestSignaling.connect(
      this, &TinCanConnectionManager::OnRequestSignaling);
  peer_state->transport->SignalCandidatesReady.connect(
      this, &TinCanConnectionManager::OnCandidatesReady);
  peer_state->transport->SignalCandidatesAllocationDone.connect(
      this, &TinCanConnectionManager::OnCandidatesAllocationDone);
  peer_state->transport->SignalReadableState.connect(
      this, &TinCanConnectionManager::OnRWChangeState);
  peer_state->transport->SignalWritableState.connect(
      this, &TinCanConnectionManager::OnRWChangeState);

  SetupTransport(peer_state.get());
  peer_state->transport->ConnectChannels();
  uid_map_[uid] = peer_state;
  transport_map_[peer_state->transport.get()] = uid;
  // TODO: This is speed hack
  short_uid_map_[uid.substr(0, kShortLen * 2)] = peer_state->transport.get();
  return true;
}

bool TinCanConnectionManager::AddIPMapping(
    const std::string& uid, const std::string& ip4, const std::string& ip6) {
  if (ip4.empty() || ip6.empty() || uid.size() != kIdSize || 
      ip_map_.find(uid) != ip_map_.end()) return false;
  char uid_str[kIdBytesLen];
  talk_base::hex_decode(uid_str, kIdBytesLen, uid);
  // TODO - this override call should go away, only there for compatibility
  override_base_ipv4_addr_p(ip4.c_str());
  peerlist_add_p(uid_str, ip4.c_str(), ip6.c_str(), 0);
  IPs ips;
  ips.ip4 = ip4;
  ips.ip6 = ip6;
  ip_map_[uid] = ips;
  return true;
}

bool TinCanConnectionManager::CreateConnections(
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

bool TinCanConnectionManager::DestroyTransport(const std::string& uid) {
  if (uid_map_.find(uid) == uid_map_.end()) return false;
  if (uid_map_[uid]->transport->was_writable()) {
    uid_map_[uid]->port_allocator.release();
  }
  uid_map_.erase(uid);
  short_uid_map_.erase(uid.substr(0, kShortLen * 2));
  LOG_F(INFO) << "DESTROYED " << uid;
  return true;
}

void TinCanConnectionManager::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_QUEUESIGNAL: {
        HandleQueueSignal_w(0);
      }
      break;
    case MSG_CONTROLLERSIGNAL: {
        HandleControllerSignal_w(0);
      }
      break;
  }
}

void TinCanConnectionManager::HandlePeer(const std::string& uid,
                                       const std::string& data) {
  // This is a callback message to the controller indicating a new
  // connection request sent over XMPP
  signal_sender_->SendToPeer(kLocalControllerId, uid, data, "con_req");
  LOG_F(INFO) << uid << " " << data;
}

void TinCanConnectionManager::HandleQueueSignal(struct threadqueue *queue) {
  if (g_manager != 0) {
    if (queue != 0) {
      g_manager->packet_handling_thread()->Post(g_manager, MSG_QUEUESIGNAL, 0);
    }
    else {
      g_manager->packet_handling_thread()->Post(g_manager, MSG_CONTROLLERSIGNAL, 0);
    }
  }
}

void TinCanConnectionManager::HandleQueueSignal_w(
    struct threadqueue *queue) {
  char buf[kBufferSize];
  int len = thread_queue_bget(send_queue_, buf, sizeof(buf));
  if (len > 0) {
    HandlePacket(0, buf, len, talk_base::SocketAddress());
  }
}

void TinCanConnectionManager::HandleControllerSignal_w(
    struct threadqueue *queue) {
  char buf[kBufferSize];
  int len = thread_queue_bget(controller_queue_, buf, sizeof(buf));
  if (len > 0) {
    int count = thread_queue_bput(rcv_queue_, buf, len);
  }
}

Json::Value TinCanConnectionManager::GetState() {
  Json::Value peers(Json::objectValue);
  for (std::map<std::string, IPs>::const_iterator it =
       ip_map_.begin(); it != ip_map_.end(); ++it) {
    std::string uid = it->first;
    Json::Value peer(Json::objectValue);
    peer["uid"] = it->first;
    peer["ip4"] = it->second.ip4;
    peer["ip6"] = it->second.ip6;
    peer["status"] = "offline";
    if (uid_map_.find(uid) != uid_map_.end()) {
      peer["fpr"] = uid_map_[uid]->fingerprint;
      uint32 time_diff = talk_base::Time() - uid_map_[uid]->last_time;
      peer["last_time"] = time_diff/1000;
      if (uid_map_[uid]->transport->readable() && 
          uid_map_[uid]->transport->writable()) {
        peer["status"] = "online";
        cricket::ConnectionInfos infos;
        int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
        uid_map_[uid]->transport->GetChannel(component)->GetStats(&infos);
        std::ostringstream oss;
        for (int i = 0; i < infos.size(); i++) {
          oss << infos[i].best_connection << ":"
              << infos[i].writable << ":"
              << infos[i].readable << ":"
              << infos[i].timeout << ":"
              << infos[i].new_connection << ":"
              << infos[i].rtt << ":" 
              << infos[i].sent_total_bytes << ":"
              << infos[i].sent_bytes_second<< ":"
              << infos[i].recv_total_bytes << ":"
              << infos[i].recv_bytes_second << " ";
         }
         peer["stats"] = oss.str();
         std::ostringstream oss2;
         for (int i = 0; i < infos.size(); i++) {
           oss2 << infos[i].local_candidate.address().ToString() << " "
             << infos[i].remote_candidate.address().ToString() << " ";
         }
         peer["stats_cons"] = oss2.str();
      }
    }
    peers[it->first] = peer;
  }
  return peers;
}

}  // namespace tincan

