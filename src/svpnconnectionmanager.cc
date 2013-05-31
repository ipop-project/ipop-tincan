
#include <sstream>
#include <iostream>

#include "talk/base/logging.h"
#include "talk/base/timeutils.h"

#include "svpnconnectionmanager.h"

namespace sjingle {

static const char kStunServer[] = "stun.l.google.com";
static const int kStunPort = 19302;
static const char kContentName[] = "svpn-jingle";
static const bool kAllowTcpListen = false;
static const char kIceUfrag[] = "ufrag";
static const char kIcePwd[] = "pwd";
static const int kBufferSize = 1500;
static const int kIdSize = 20;
static const int kCheckInterval = 30000;
static const char kIpNetwork[] = "172.31.0.";
static const char kIpv6[] = "fd50:0dbc:41f2:4a3c:b683:19a7:63b4:f736";
static const int kIpBase = 101;
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
    struct threadqueue* rcv_queue,
    const std::string& uid)
    : content_name_(kContentName),
      social_sender_(social_sender),
      packet_factory_(worker_thread),
      uid_map_(),
      transport_map_(),
      signaling_thread_(signaling_thread),
      worker_thread_(worker_thread),
      stun_server_(kStunServer, kStunPort),
      network_manager_(),
      identity_(talk_base::OpenSSLIdentity::Generate(uid)),
      local_fingerprint_(talk_base::SSLFingerprint::Create(
           talk_base::DIGEST_SHA_1, identity_)),
      fingerprint_(local_fingerprint_->GetRfc4572Fingerprint()),
      send_queue_(send_queue),
      rcv_queue_(rcv_queue),
      tiebreaker_(talk_base::CreateRandomId64()),
      last_connect_time_(talk_base::Time()) {
  signaling_thread->PostDelayed(kCheckInterval, this, MSG_CHECK, 0);
  worker_thread->PostDelayed(kCheckInterval + 15000, this, MSG_PING, 0);
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
    if (candidates[i].network_name().compare("svpn0") == 0) return;
    std::ostringstream oss;
    std::string ip_string =
        talk_base::SocketAddress::IPToString(candidates[i].address().ip());
    oss << candidates[i].id() << ":" << candidates[i].component()
        << ":" << candidates[i].protocol() << ":" << ip_string
        << ":"<< candidates[i].address().port() 
        << ":" << candidates[i].priority() 
        << ":" << candidates[i].username() 
        << ":" << candidates[i].password() 
        << ":" << candidates[i].type() 
        << ":" << candidates[i].network_name() 
        << ":" << candidates[i].generation() 
        << ":" << candidates[i].foundation(); 
    candidate_list.insert(oss.str());
    LOG(INFO) << __FUNCTION__ << " " << oss.str();
  }
}

void SvpnConnectionManager::OnCandidatesAllocationDone(
    cricket::Transport* transport) {
  std::string uid_key = get_key(transport_map_[transport]);
  std::set<std::string>& candidates = uid_map_[uid_key]->candidate_list;
  std::string data(fingerprint());
  for (std::set<std::string>::iterator it = candidates.begin();
       it != candidates.end(); ++it) {
    data += " ";
    data += *it;
  }
  if (transport_map_.find(transport) != transport_map_.end()) {
    social_sender_->SendToPeer(transport_map_[transport], data);
  }
}

void SvpnConnectionManager::UpdateTime(const char* data, size_t len) {
  std::string uid(data, len);
  std::string uid_key = get_key(uid);
  if (uid_map_.find(uid_key) != uid_map_.end()) {
    uid_map_[uid_key]->last_ping_time = talk_base::Time();
    LOG(INFO) << __FUNCTION__ << " PING FROM " << uid << " "
              << uid_map_[uid_key]->last_ping_time;
  }
}

void SvpnConnectionManager::OnReadPacket(cricket::TransportChannel* channel, 
    const char* data, size_t len, int flags) {
  if (len < (kIdSize * 2)) {
    return UpdateTime(data, len);
  }
  const char* dest_id = data + kIdSize;
  std::string source(data, kResourceSize);
  std::string dest(dest_id, kResourceSize);
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  if (uid_map_[source]->transport.get()->GetChannel(component) == channel) {
    int count = thread_queue_bput(rcv_queue_, data, len);
    LOG(INFO) << __FUNCTION__ << " " << len << " " << source
              << " " << dest << " " << count;
  }
}

void SvpnConnectionManager::HandlePacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  if (len < (kIdSize * 2)) return;
  const char* dest_id = data + kIdSize;
  std::string source(data, kResourceSize);
  std::string dest(dest_id, kResourceSize);
  LOG(INFO) << __FUNCTION__ << " " << source << " " << dest;
  if (uid_map_.find(dest) != uid_map_.end()) {
    int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
    cricket::DtlsTransportChannelWrapper* channel =
        static_cast<cricket::DtlsTransportChannelWrapper*>(
            uid_map_[dest]->transport.get()->GetChannel(component));
#ifndef NO_DTLS
    int count = channel->SendPacket(data, len, 0);
    LOG(INFO) << __FUNCTION__ << " SENT DTLS " << count;
#else
    int count = channel->channel()->SendPacket(data, len, 0);
    LOG(INFO) << __FUNCTION__ << " SENT NODTLS " << count;
#endif
  }
}

void SvpnConnectionManager::AddIP(const std::string& uid) {
  // TODO - Cleanup this function
  int ip_idx = kIpBase + ip_map_.size();
  std::string uid_key = get_key(uid);
  if (ip_map_.find(uid) != ip_map_.end()) {
    ip_idx = ip_map_[uid];
  }
  ip_map_[uid] = ip_idx;
  std::string ip(kIpNetwork);
  char ip_rem[4] = { '\0' };
  snprintf(ip_rem, sizeof(ip_rem), "%d", ip_idx);
  ip += ip_rem;
  // TODO - Generate real IPv6 addresses
  peerlist_add_p(uid_key.c_str(), ip.c_str(), kIpv6, 0);
  std::cout << "\nAdding " << uid << " " << ip << std::endl;
}

void SvpnConnectionManager::SetupTransport(PeerState* peer_state) {
  peer_state->transport.get()->SetTiebreaker(tiebreaker_);
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
    peer_state->transport.get()->SetRole(cricket::ROLE_CONTROLLING);
    peer_state->transport.get()->SetLocalTransportDescription(
        *peer_state->local_description, cricket::CA_OFFER);
    peer_state->transport.get()->SetRemoteTransportDescription(
        *peer_state->remote_description, cricket::CA_ANSWER);
  }
  else {
    peer_state->transport.get()->SetRole(cricket::ROLE_CONTROLLED);
    peer_state->transport.get()->SetRemoteTransportDescription(
        *peer_state->remote_description, cricket::CA_OFFER);
    peer_state->transport.get()->SetLocalTransportDescription(
        *peer_state->local_description, cricket::CA_ANSWER);
  }
}

void SvpnConnectionManager::CreateTransport(
    const std::string& uid, const std::string& fingerprint) {
  std::string uid_key = get_key(uid);
  LOG(INFO) << __FUNCTION__ << " " << uid_key;
  if (uid_map_.find(uid_key) != uid_map_.end()) {
    LOG(INFO) << __FUNCTION__ << " EXISTING TRANSPORT " << uid_key;
    return;
  }

  PeerStatePtr peer_state(new talk_base::RefCountedObject<PeerState>);
  peer_state->uid = uid;
  peer_state->fingerprint = fingerprint;
  peer_state->last_ping_time = talk_base::Time();
  peer_state->port_allocator.reset(new cricket::BasicPortAllocator(
      &network_manager_, &packet_factory_, stun_server_));
  peer_state->port_allocator->set_flags(kFlags);
  peer_state->port_allocator->set_allow_tcp_listen(kAllowTcpListen);
  peer_state->transport.reset(new DtlsP2PTransport(
      signaling_thread_, worker_thread_, content_name_, 
      peer_state->port_allocator.get(), identity_));

  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  cricket::DtlsTransportChannelWrapper* channel =
      static_cast<cricket::DtlsTransportChannelWrapper*>(
          peer_state->transport.get()->CreateChannel(component));

#ifndef NO_DTLS
  channel->SignalReadPacket.connect(
    this, &SvpnConnectionManager::OnReadPacket);
#else
  channel->channel()->SignalReadPacket.connect(
    this, &SvpnConnectionManager::OnReadPacket);
#endif

  peer_state->transport.get()->SignalRequestSignaling.connect(
      this, &SvpnConnectionManager::OnRequestSignaling);
  peer_state->transport.get()->SignalCandidatesReady.connect(
      this, &SvpnConnectionManager::OnCandidatesReady);
  peer_state->transport.get()->SignalCandidatesAllocationDone.connect(
      this, &SvpnConnectionManager::OnCandidatesAllocationDone);
  peer_state->transport.get()->SignalReadableState.connect(
      this, &SvpnConnectionManager::OnRWChangeState);
  peer_state->transport.get()->SignalWritableState.connect(
      this, &SvpnConnectionManager::OnRWChangeState);

  SetupTransport(peer_state.get());
  peer_state->transport.get()->ConnectChannels();
  uid_map_[uid_key] = peer_state;
  transport_map_[peer_state->transport.get()] = uid;
  AddIP(uid);
}

void SvpnConnectionManager::CreateConnections(
    const std::string& uid, const std::string& candidates_string) {
  std::string uid_key = get_key(uid);
  cricket::Candidates& candidates = uid_map_[uid_key]->candidates;
  if (candidates.size() > 0) return;
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
  uid_map_[uid_key]->transport.get()->OnRemoteCandidates(candidates);
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
  for (std::map<std::string, int>::iterator it = ip_map_.begin();
       it != ip_map_.end(); ++it) {
    social_sender_->SendToPeer(it->first, fingerprint());
  }

  std::vector<std::string> dead_transports;
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  for (std::map<std::string, PeerStatePtr>::iterator it = uid_map_.begin();
       it != uid_map_.end(); ++it) {
    cricket::DtlsTransportChannelWrapper* channel =
        static_cast<cricket::DtlsTransportChannelWrapper*>(
            it->second->transport.get()->GetChannel(component));
    uint32 time_diff = talk_base::Time() - it->second->last_ping_time;
    if (time_diff > 2 * kCheckInterval) {
      dead_transports.push_back(it->first);
    }
    std::cout << "\nNode status " << it->first << " "
              << channel->ToString() << std::endl;
  }
  for (std::vector<std::string>::iterator it = dead_transports.begin();
       it != dead_transports.end(); ++it) {
    uid_map_.erase(*it);
    LOG(INFO) << __FUNCTION__ << " DEAD TRANSPORT " << *it;
  }
  signaling_thread_->PostDelayed(kCheckInterval, this, MSG_CHECK, 0);
}

void SvpnConnectionManager::HandlePing_w() {
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  for (std::map<std::string, PeerStatePtr>::iterator it = uid_map_.begin();
       it != uid_map_.end(); ++it) {
    cricket::TransportChannelImpl* channel = 
        it->second->transport->GetChannel(component);
    int count = channel->SendPacket(social_sender_->uid().c_str(), 
                                    social_sender_->uid().size(), 0);
  }
  worker_thread_->PostDelayed(kCheckInterval, this, MSG_PING, 0);
}

}  // namespace sjingle

