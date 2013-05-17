
#include <sstream>
#include <iostream>

#include "talk/base/logging.h"
#include "talk/base/stringencode.h"
#include "talk/base/timeutils.h"

#include "svpnconnectionmanager.h"

namespace sjingle {

static const char kStunServer[] = "stun.l.google.com";
static const int kStunPort = 19302;
static const char kContentName[] = "svpn-jingle";
static const bool kAllowTcpListen = false;
static const char kIceUfrag[] = "SVPNICEUFRAG0001";
static const char kIcePwd[] = "SVPNICEPWD00000000000001";
static const int kSvpnPort = 5800;
static const int kNetworkPort = 5801;
static const int kMinPort = 5802;
static const int kMaxPort = 5820;
static const char kLocalHost[] = "127.0.0.1";
static const int kBufferSize = 1500;
static const int kIdSize = 20;
static const int kCheckInterval = 30000;
static const int kIpBase = 101;

static SvpnConnectionManager* g_manager = 0;

const uint32 kFlags = cricket::PORTALLOCATOR_DISABLE_RELAY |
                      cricket::PORTALLOCATOR_DISABLE_TCP |
                      cricket::PORTALLOCATOR_ENABLE_SHARED_UFRAG | 
                      cricket::PORTALLOCATOR_ENABLE_BUNDLE |
                      cricket::PORTALLOCATOR_ENABLE_SHARED_SOCKET;

enum {
  MSG_SETSOCKET = 1,
  MSG_QUEUESIGNAL = 2,
  MSG_CHECK = 3
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
      socket_(0),
      packet_factory_(worker_thread),
      uid_map_(),
      channel_map_(),
      candidates_(),
      signaling_thread_(signaling_thread),
      worker_thread_(worker_thread),
      stun_server_(kStunServer, kStunPort),
      network_manager_(),
      port_allocator_(&network_manager_, stun_server_,
                      talk_base::SocketAddress(),
                      talk_base::SocketAddress(),
                      talk_base::SocketAddress()),
      identity_(talk_base::OpenSSLIdentity::Generate(uid)),
      local_fingerprint_(talk_base::SSLFingerprint::Create(
           talk_base::DIGEST_SHA_1, identity_)),
      send_queue_(send_queue), rcv_queue_(rcv_queue) {
  port_allocator_.set_flags(kFlags);
  port_allocator_.set_allow_tcp_listen(kAllowTcpListen);
  port_allocator_.SetPortRange(kMinPort, kMaxPort);
#ifndef EN_SOCK
  g_manager = this;
#else
  worker_thread->Post(this, MSG_SETSOCKET, 0);
#endif
  signaling_thread->PostDelayed(kCheckInterval, this, MSG_CHECK, 0);
  fingerprint_ = local_fingerprint_->GetRfc4752Fingerprint();
}

void SvpnConnectionManager::OnRequestSignaling(
    cricket::TransportChannelImpl* channel) {
  channel->OnSignalingReady();
  LOG(INFO) << __FUNCTION__ << " SIGNALING";
}

void SvpnConnectionManager::OnCandidateReady(
    cricket::TransportChannelImpl* channel, 
    const cricket::Candidate& candidate) {
  if (candidate.network_name().compare("svpn0") == 0) return;
  std::ostringstream oss;
  std::string ip_string = talk_base::SocketAddress::IPToString(
      candidate.address().ip());
  oss << candidate.id() << ":" << candidate.component() << ":"
      << candidate.protocol() << ":" << ip_string << ":"
      << candidate.address().port() << ":" << candidate.priority() << ":"
      << candidate.username() << ":" << candidate.password() << ":"
      << candidate.type() << ":" << candidate.network_name() << ":"
      << candidate.generation() << ":" << candidate.foundation(); 
  candidates_.insert(oss.str());
  LOG(INFO) << __FUNCTION__ << " " << oss.str();
}

void SvpnConnectionManager::OnCandidatesAllocationDone(
    cricket::TransportChannelImpl* channel) {
  std::string data(fingerprint());
  for (std::set<std::string>::iterator it = candidates_.begin();
       it != candidates_.end(); ++it) {
    data += " ";
    data += *it;
  }
  if (channel_map_.find(channel) != channel_map_.end()) {
    social_sender_->SendToPeer(channel_map_[channel].uid, data);
    LOG(INFO) << __FUNCTION__ << " " << data;
  }
}

void SvpnConnectionManager::OnRWChangeState(
    cricket::TransportChannel* channel) {
  LOG(INFO) << __FUNCTION__ << " " << "R " << channel->readable()
            << " W " << channel->writable();
  if (channel->readable() && channel->writable()) {
    std::string uid = channel_map_[channel].uid;
    std::cout << "Node " << uid << " online" << std::endl;
  }
}

void SvpnConnectionManager::OnReadPacket(cricket::TransportChannel* channel, 
    const char* data, size_t len, int flags) {
  const char* dest_id = data + kIdSize;
  std::string source(data, kResourceSize);
  std::string dest(dest_id, kResourceSize);
#ifndef EN_SOCK
  int count = thread_queue_bput(rcv_queue_, data, len);
#else
  talk_base::SocketAddress addr(kLocalHost, kSvpnPort);
  int count = socket_->SendTo(data, len, addr);
#endif
  LOG(INFO) << __FUNCTION__ << " " << len << " " << source << " " 
            << dest << " " << count;
}

void SvpnConnectionManager::HandlePacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  const char* dest_id = data + kIdSize;
  std::string source(data, kResourceSize);
  std::string dest(dest_id, kResourceSize);
  LOG(INFO) << __FUNCTION__ << " " << source << " " << dest;
  if (uid_map_.find(dest) != uid_map_.end()) {
    int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
#ifndef NO_DTLS
    cricket::TransportChannel* channel = 
        uid_map_[dest].transport->GetChannel(component);
    int count = channel->SendPacket(data, len, 0);
#else
    cricket::DtlsTransportChannelWrapper* channel =
        static_cast<cricket::DtlsTransportChannelWrapper*>(
            uid_map_[dest].transport->GetChannel(component));
    int count = channel->channel()->SendPacket(data, len, 0);
#endif
    LOG(INFO) << __FUNCTION__ << " SENT " << count;
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
  std::string ip("172.31.0.");
  char ip_rem[3];
  sprintf(ip_rem, "%d", ip_idx);
  ip += ip_rem;
  // TODO - Generate real IPv6 addresses
  char ipv6[] = "fd50:0dbc:41f2:4a3c:b683:19a7:63b4:f736";
  peerlist_add_p(uid_key.c_str(), ip.c_str(), ipv6, 5800);
  std::cout << "\nadding " << uid << " " << ip << "\n" << std::endl;
}

void SvpnConnectionManager::SetupTransport(
    cricket::P2PTransport* transport, const std::string& uid, 
    const std::string& fingerprint) {
  talk_base::SSLFingerprint* remote_fingerprint =
      talk_base::SSLFingerprint::CreateFromRfc4572(talk_base::DIGEST_SHA_1,
                                                   fingerprint);
  cricket::TransportDescription* local_description =
      new cricket::TransportDescription(
      cricket::NS_GINGLE_P2P, std::vector<std::string>(), kIceUfrag,
      kIcePwd, cricket::ICEMODE_FULL, local_fingerprint_, 
      cricket::Candidates());
  cricket::TransportDescription* remote_description =
      new cricket::TransportDescription(
      cricket::NS_GINGLE_P2P, std::vector<std::string>(), kIceUfrag, 
      kIcePwd, cricket::ICEMODE_FULL, remote_fingerprint, 
      cricket::Candidates());

  if (uid.compare(social_sender_->uid()) < 0) {
    transport->SetRole(cricket::ROLE_CONTROLLING);
    transport->SetTiebreaker(1);
    transport->SetLocalTransportDescription(*local_description,
                                            cricket::CA_OFFER);
    transport->SetRemoteTransportDescription(*remote_description,
                                             cricket::CA_ANSWER);
  }
  else {
    transport->SetRole(cricket::ROLE_CONTROLLED);
    transport->SetTiebreaker(2);
    transport->SetRemoteTransportDescription(*remote_description,
                                             cricket::CA_OFFER);
    transport->SetLocalTransportDescription(*local_description,
                                            cricket::CA_ANSWER);
  }
  LOG(INFO) << __FUNCTION__ << " DIGEST SET " << fingerprint
            << " " << fingerprint.size();
}

void SvpnConnectionManager::CreateTransport(
    const std::string& uid, const std::string& fingerprint) {
  std::string uid_key = get_key(uid);
  LOG(INFO) << __FUNCTION__ << " " << uid_key;
  if (uid_map_.find(uid_key) != uid_map_.end()) {
    LOG(INFO) << __FUNCTION__ << " EXISTING TRANSPORT " << uid_key;
    return;
  }

  PeerState peer_state;
  peer_state.uid = uid;
  peer_state.fingerprint = fingerprint;
  peer_state.creation_time = talk_base::Time();
  peer_state.transport = new DtlsP2PTransport(
      signaling_thread_, worker_thread_, content_name_, 
      &port_allocator_, identity_);

  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  cricket::DtlsTransportChannelWrapper* channel =
      static_cast<cricket::DtlsTransportChannelWrapper*>(
          peer_state.transport->CreateChannel(component));
  channel->SignalRequestSignaling.connect(
      this, &SvpnConnectionManager::OnRequestSignaling);
  channel->SignalCandidateReady.connect(
      this, &SvpnConnectionManager::OnCandidateReady);
  channel->SignalCandidatesAllocationDone.connect(
      this, &SvpnConnectionManager::OnCandidatesAllocationDone);
  channel->SignalReadableState.connect(
      this, &SvpnConnectionManager::OnRWChangeState);
  channel->SignalWritableState.connect(
      this, &SvpnConnectionManager::OnRWChangeState);
#ifndef NO_DTLS
  channel->SignalReadPacket.connect(
    this, &SvpnConnectionManager::OnReadPacket);
#else
  channel->channel()->SignalReadPacket.connect(
    this, &SvpnConnectionManager::OnReadPacket);
#endif

  uid_map_[uid_key] = peer_state;
  channel_map_[channel] = peer_state;
  SetupTransport(peer_state.transport, uid, fingerprint);
  peer_state.transport->ConnectChannels();
  AddIP(uid);
}

void SvpnConnectionManager::CreateConnections(
    const std::string& uid, const std::string& candidates_string) {
  cricket::Candidates candidates;
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
  uid_map_[get_key(uid)].transport->OnRemoteCandidates(candidates);
}

void SvpnConnectionManager::SetSocket_w() {
  talk_base::SocketAddress local_address(kLocalHost, 0);
  socket_ = packet_factory_.CreateUdpSocket(local_address, kNetworkPort,
                                               kNetworkPort);
  socket_->SignalReadPacket.connect(
      this, &sjingle::SvpnConnectionManager::HandlePacket);
}

void SvpnConnectionManager::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_SETSOCKET: {
        SetSocket_w();
      }
      break;
    case MSG_QUEUESIGNAL: {
        HandleQueueSignal_w(0);
      }
      break;
    case MSG_CHECK: {
        HandleCheck_s();
      }
      break;
  }
}

void SvpnConnectionManager::HandlePeer(const std::string& uid,
                                       const std::string& data) {
  LOG(INFO) << __FUNCTION__ << " " << uid << " " << data;
  std::string uid_key = get_key(uid);

  if (data.size() == fingerprint().size()) {
    if (uid_key.compare(social_sender_->uid()) < 0) {
      CreateTransport(uid, data);
    }
  }
  else if (data.size() > fingerprint().size()) {
    if (uid_key.compare(social_sender_->uid()) < 0) {
      CreateConnections(uid, data.substr(fingerprint().size()));
    }
    else {
      CreateTransport(uid, data.substr(0, fingerprint().size()));
      CreateConnections(uid, data.substr(fingerprint().size()));
    }
  }
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
  // TODO - Need a better way to detect back channels
  // Put ping function in worker thread, if return -1 then set flag
  int count = 0;
  for (std::map<std::string, PeerState>::iterator it = uid_map_.begin();
       it != uid_map_.end(); ++it) {
    uint32 time_interval = talk_base::Time() - it->second.creation_time;
    DtlsP2PTransport* transport = it->second.transport;
    int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
    cricket::DtlsTransportChannelWrapper* channel =
        static_cast<cricket::DtlsTransportChannelWrapper*>(
            transport->GetChannel(component));
    std::cout << channel->ToString() << " ROLE " << channel->GetRole() << std::endl;
    if (!channel->IsDtlsActive()) {
      count++;
    }
  }
  LOG(INFO) << __FUNCTION__ << " DEAD TRANSPORTS " << count;
  worker_thread_->PostDelayed(kCheckInterval, this, MSG_CHECK, 0);
}

}  // namespace sjingle

