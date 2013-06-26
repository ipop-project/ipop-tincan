
#include <iostream>
#include <sstream>

#include "talk/base/logging.h"
#include "talk/base/timeutils.h"
#include "talk/base/json.h"

#include "svpnconnectionmanager.h"

namespace sjingle {

//static const char kStunServer[] = "stun.l.google.com";
static const char kStunServer[] = "0.0.0.0";
static const int kStunPort = 19302;
static const char kContentName[] = "svpn-jingle";
static const bool kAllowTcpListen = false;
static const char kIceUfrag[] = "ufrag";
static const char kIcePwd[] = "pwd";
static const int kBufferSize = 1500;
static const int kCheckInterval = 30000;
static const char kIpv4[] = "172.31.0.100";
static const char kIpv6[] = "fd50:0dbc:41f2:4a3c:0000:0000:0000:0000";
static const int kIpBase = 101;
static const char kTapName[] = "svpn";

static SvpnConnectionManager* g_manager = 0;
static const uint32 kFlags = cricket::PORTALLOCATOR_DISABLE_RELAY |
                             cricket::PORTALLOCATOR_DISABLE_TCP;

enum {
  MSG_QUEUESIGNAL = 1,
  MSG_CHECK = 2,
  MSG_PING = 3
};

SvpnConnectionManager::SvpnConnectionManager(
    SocialNetworkSenderInterface* social_sender,
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
      stun_server_(kStunServer, kStunPort),
      network_manager_(),
      svpn_id_(talk_base::hex_encode(
          talk_base::CreateRandomString(kIdSize/2).c_str(), kIdSize/2)),
      identity_(talk_base::OpenSSLIdentity::Generate(svpn_id_)),
      local_fingerprint_(talk_base::SSLFingerprint::Create(
           talk_base::DIGEST_SHA_1, identity_)),
      fingerprint_(local_fingerprint_->GetRfc4572Fingerprint()),
      send_queue_(send_queue),
      rcv_queue_(rcv_queue),
      tiebreaker_(talk_base::CreateRandomId64()),
      check_counter_(0),
      svpn_ip4_(kIpv4),
      svpn_ip6_(kIpv6),
      tap_name_(kTapName),
      sec_enabled_(true) {
  signaling_thread->PostDelayed(kCheckInterval, this, MSG_CHECK, 0);
  worker_thread->PostDelayed(kCheckInterval + 15000, this, MSG_PING, 0);
  svpn_ip6_ = gen_ip6(svpn_id_);
  g_manager = this;
}

void SvpnConnectionManager::OnRequestSignaling(
    cricket::Transport* transport) {
  transport->OnSignalingReady();
}

void SvpnConnectionManager::OnRWChangeState(
    cricket::Transport* transport) {
  if (transport->readable() && transport->writable()) {
    std::string uid = transport_map_[transport];
    LOG(INFO) << __FUNCTION__ << "ONLINE " << uid;
  }
}

void SvpnConnectionManager::OnCandidatesReady(
    cricket::Transport* transport, const cricket::Candidates& candidates) {
  std::string uid_key = get_key(transport_map_[transport]);
  std::set<std::string>& candidate_list = uid_map_[uid_key]->candidate_list;
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
  std::string uid_key = get_key(transport_map_[transport]);
  std::set<std::string>& candidates = uid_map_[uid_key]->candidate_list;
  std::string data(fingerprint());
  for (std::set<std::string>::const_iterator it = candidates.begin();
       it != candidates.end(); ++it) {
    data += " ";
    data += *it;
  }
  if (transport_map_.find(transport) != transport_map_.end()) {
    social_sender_->SendToPeer(transport_map_[transport], data);
  }
}

void SvpnConnectionManager::UpdateTime(const char* data, size_t len) {
  std::string uid_key(data, len);
  if (uid_map_.find(uid_key) != uid_map_.end()) {
    uid_map_[uid_key]->last_ping_time = talk_base::Time();
    LOG(INFO) << __FUNCTION__ << " PING FROM " << uid_map_[uid_key]->uid 
              << " " << uid_map_[uid_key]->last_ping_time;
  }
}

void SvpnConnectionManager::OnReadPacket(cricket::TransportChannel* channel, 
    const char* data, size_t len, int flags) {
  if (len < (kHeaderSize)) {
    return UpdateTime(data, len);
  }
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

bool SvpnConnectionManager::AddIP(const std::string& uid) {
  if (ip_map_.find(uid) != ip_map_.end())  return false;
  int ip_idx = kIpBase + ip_map_.size();
  std::string uid_key = get_key(uid);
  ip_map_[uid] = ip_idx;
  char ip4[sizeof(kIpv4)] = { '0' };
  svpn_ip4_.copy(ip4, sizeof(kIpv4));
  snprintf(ip4 + 9, 4, "%d", ip_idx);
  std::string ip6 = gen_ip6(uid_key);
  peerlist_add_p(uid_key.c_str(), ip4, ip6.c_str(), 0);
  return true;
}

void SvpnConnectionManager::SetupTransport(PeerState* peer_state) {
  peer_state->transport->SetTiebreaker(tiebreaker_);
  peer_state->remote_fingerprint.reset(
      talk_base::SSLFingerprint::CreateFromRfc4572(talk_base::DIGEST_SHA_1,
                                                   peer_state->fingerprint));
  peer_state->local_description.reset(new cricket::TransportDescription(
      cricket::NS_JINGLE_ICE_UDP, std::vector<std::string>(), kIceUfrag,
      kIcePwd, cricket::ICEMODE_FULL, local_fingerprint_, 
      peer_state->candidates));
  peer_state->remote_description.reset(new cricket::TransportDescription(
      cricket::NS_JINGLE_ICE_UDP, std::vector<std::string>(), kIceUfrag, 
      kIcePwd, cricket::ICEMODE_FULL, peer_state->remote_fingerprint.get(), 
      peer_state->candidates));

  if (peer_state->uid.compare(social_sender_->uid()) < 0) {
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
    const std::string& uid, const std::string& fingerprint) {
  std::string uid_key = get_key(uid);
  LOG(INFO) << __FUNCTION__ << " " << uid_key;
  if (uid_map_.find(uid_key) != uid_map_.end()) {
    LOG(INFO) << __FUNCTION__ << " EXISTING TRANSPORT " << uid_key;
    return false;
  }

  LOG(INFO) << __FUNCTION__ << " STUN " << stun_server_.ToString();

  PeerStatePtr peer_state(new talk_base::RefCountedObject<PeerState>);
  peer_state->uid = uid;
  peer_state->fingerprint = fingerprint;
  peer_state->last_ping_time = talk_base::Time();
  peer_state->port_allocator.reset(new cricket::BasicPortAllocator(
      &network_manager_, &packet_factory_, stun_server_));
  peer_state->port_allocator->set_flags(kFlags);
  peer_state->port_allocator->set_allow_tcp_listen(kAllowTcpListen);

  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  cricket::TransportChannelImpl* channel;
  if (sec_enabled_) {
    peer_state->transport.reset(new DtlsP2PTransport(
        signaling_thread_, worker_thread_, content_name_, 
        peer_state->port_allocator.get(), identity_));
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
  uid_map_[uid_key] = peer_state;
  transport_map_[peer_state->transport.get()] = uid;
  return AddIP(uid);
}

bool SvpnConnectionManager::CreateConnections(
    const std::string& uid, const std::string& candidates_string) {
  std::string uid_key = get_key(uid);
  cricket::Candidates& candidates = uid_map_[uid_key]->candidates;
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
  uid_map_[uid_key]->transport->OnRemoteCandidates(candidates);
  return true;
}

void SvpnConnectionManager::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_QUEUESIGNAL: {
        HandleQueueSignal_w(0);
      }
      break;
    case MSG_CHECK: {
        HandleCheck_s();
      }
      break;
    case MSG_PING: {
        HandlePing_w();
      }
      break;
  }
}

void SvpnConnectionManager::HandlePeer(const std::string& uid,
                                       const std::string& data) {
  std::string uid_key = get_key(uid);
  if (data.size() == fingerprint().size()) {
    CreateTransport(uid, data);
  }
  else if (data.size() > fingerprint().size()) {
    CreateTransport(uid, data.substr(0, fingerprint().size()));
    CreateConnections(uid, data.substr(fingerprint().size()));
  }
  LOG(INFO) << __FUNCTION__ << " " << uid << " " << data;
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

void SvpnConnectionManager::HandleCheck_s() {
  if (++check_counter_ % 8 == 0) {
    for (std::map<std::string, int>::const_iterator it = ip_map_.begin();
         it != ip_map_.end(); ++it) {
        social_sender_->SendToPeer(it->first, fingerprint());
        LOG(INFO) << __FUNCTION__ << " SOCIAL REQUEST TO " << it->first;
    }
  }

  std::vector<std::string> dead_transports;
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  for (std::map<std::string, PeerStatePtr>::const_iterator it = 
       uid_map_.begin(); it != uid_map_.end(); ++it) {
    cricket::TransportChannelImpl* channel =
        it->second->transport->GetChannel(component);
    uint32 time_diff = talk_base::Time() - it->second->last_ping_time;
    if (time_diff > 2 * kCheckInterval) {
      if (!it->second->transport->was_writable()) {
        it->second->port_allocator.release();
      }
      dead_transports.push_back(it->first);
      LOG(INFO) << __FUNCTION__ << " DEAD TRANSPORT " << it->second->uid;
    }
  } 
  for (std::vector<std::string>::const_iterator it = dead_transports.begin();
       it != dead_transports.end(); ++it) {
    uid_map_.erase(*it);
  }
  signaling_thread_->PostDelayed(kCheckInterval/2, this, MSG_CHECK, 0);
}

void SvpnConnectionManager::HandlePing_w() {
  std::string uid = social_sender_->uid();
  if (uid.size() < kIdSize) return;
  std::string uid_key = get_key(uid);
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  for (std::map<std::string, PeerStatePtr>::const_iterator it =
       uid_map_.begin(); it != uid_map_.end(); ++it) {
    uint32 time_diff = talk_base::Time() - it->second->last_ping_time;
    if (time_diff < 2 * kCheckInterval) {
      cricket::TransportChannelImpl* channel = 
          it->second->transport->GetChannel(component);
      int count = channel->SendPacket(uid_key.c_str(), uid_key.size(), 0);
      LOG(INFO) << __FUNCTION__ << " PINGING " << " with " << uid_key;
    }
  }
  worker_thread_->PostDelayed(kCheckInterval, this, MSG_PING, 0);
}

std::string SvpnConnectionManager::GetState() {
  Json::Value state(Json::objectValue);
  Json::Value peers(Json::arrayValue);
  for (std::map<std::string, int>::const_iterator it = ip_map_.begin();
       it != ip_map_.end(); ++it) {
    std::string uid_key = get_key(it->first);
    std::ostringstream oss;
    oss << svpn_ip4_.substr(0 ,svpn_ip4_.size() - 3) << it->second;
    Json::Value peer(Json::objectValue);
    peer["uid"] = it->first;
    peer["ipv4"] = oss.str();
    peer["ipv6"] = gen_ip6(uid_key);
    peer["status"] = "offline";
    if (uid_map_.find(uid_key) != uid_map_.end()) {
      peer["fpr"] = uid_map_[uid_key]->fingerprint;
      peer["ping"] = (talk_base::Time() - 
                      uid_map_[uid_key]->last_ping_time)/1000;
      if (uid_map_[uid_key]->transport->all_channels_readable() &&
          uid_map_[uid_key]->transport->all_channels_writable()) {
        peer["status"] = "online";
      }
    }
    peers.append(peer);
  }
  state["_uid"] = social_sender_->uid();
  state["_fpr"] = fingerprint_;
  state["_ipv4"] = svpn_ip4_;
  state["_ipv6"] = svpn_ip6_;
  state["_peers"] = peers;
  return state.toStyledString();
}

}  // namespace sjingle

