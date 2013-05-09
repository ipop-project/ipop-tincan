
#include <iostream>
#include <sstream>

#include "talk/base/ssladapter.h"
#include "talk/base/logging.h"
#include "talk/xmpp/xmppsocket.h"
#include "talk/xmpp/xmpppump.h"
#include "talk/base/physicalsocketserver.h"

#include "svpnconnectionmanager.h"

namespace sjingle {

static const char kXmppHost[] = "jabber.org";
static const int kXmppPort = 5222;
static const char kStunServer[] = "stun.l.google.com";
static const int kStunPort = 19302;
static const char kContentName[] = "svpn-jingle";
static const bool kAllowTcpListen = false;
static const char kCandidateProtocolUdp[] = "udp";
static const uint32 kCandidatePriority = 2130706432U;
static const uint32 kCandidateGeneration = 0;
static const char kCandidateFoundation[] = "a0+B/1";
static const char kIceUfrag[] = "SVPNICEUFRAG0001";
static const char kIcePwd[] = "SVPNICEPWD00000000000001";
static const int kSvpnPort = 5800;
static const int kNetworkPort = 5801;
static const int kMinPort = 5802;
static const int kMaxPort = 5820;
static const char kLocalHost[] = "127.0.0.1";
static const int kBufferSize = 1500;
static const int kIdSize = 20;
static const char AES_CM_128_HMAC_SHA1_80[] = "AES_CM_128_HMAC_SHA1_80";
static const char AES_CM_128_HMAC_SHA1_32[] = "AES_CM_128_HMAC_SHA1_32";
static const char kAddrPrefix[] = "cas";
static const char kFprPrefix[] = "fpr";

const uint32 kFlags = cricket::PORTALLOCATOR_ENABLE_SHARED_UFRAG |
                      cricket::PORTALLOCATOR_DISABLE_RELAY |
                      cricket::PORTALLOCATOR_DISABLE_TCP |
                      cricket::PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                      cricket::PORTALLOCATOR_ENABLE_BUNDLE;

enum {
  MSG_SETSOCKET = 1,
  MSG_HANDLEPACKET = 2
};

struct HandlePacketParams : public talk_base::MessageData {
  HandlePacketParams(const char* param_data, size_t param_len, 
                     const talk_base::SocketAddress& param_addr)
      : data(), len(param_len), addr(param_addr) {
    if (len <= kBufferSize) {
      memcpy(data, param_data, param_len);
    }
    else {
      LOG(LERROR) << __FUNCTION__ << " len greater than kBuffersize " << len;
    }
  }
  char data[kBufferSize];
  const size_t len;
  const talk_base::SocketAddress addr;
};


SvpnConnectionManager::SvpnConnectionManager(
    SocialNetworkSenderInterface* social_sender,
    talk_base::Thread* signaling_thread,
    talk_base::Thread* worker_thread,
    talk_base::Thread* network_thread,
    const std::string& uid)
    : content_name_(kContentName),
      social_sender_(social_sender),
      socket_(0),
      packet_factory_(network_thread),
      uid_map_(),
      channel_map_(),
      addresses_(),
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
           talk_base::DIGEST_SHA_1, identity_)) {
  port_allocator_.set_flags(kFlags);
  //port_allocator_.set_allow_tcp_listen(kAllowTcpListen);
  port_allocator_.SetPortRange(kMinPort, kMaxPort);
  network_thread->Post(this, MSG_SETSOCKET, 0);
}

void SvpnConnectionManager::OnRequestSignaling(
    cricket::TransportChannelImpl* channel) {
  std::string& uid = channel_map_[channel].uid;
  if (uid.compare(social_sender_->uid()) < 0) {
    channel->OnSignalingReady();
    LOG(INFO) << __FUNCTION__ << " SIGNALING " << uid << " "
              << social_sender_->uid();
  }
}

void SvpnConnectionManager::OnCandidateReady(
    cricket::TransportChannelImpl* channel, 
    const cricket::Candidate& candidate) {
  LOG(INFO) << __FUNCTION__ << " " << candidate.ToString();
  addresses_.insert(candidate.address().ToString());
}

void SvpnConnectionManager::OnCandidatesAllocationDone(
    cricket::TransportChannelImpl* channel) {
  std::string data(kAddrPrefix);
  for (std::set<std::string>::iterator it = addresses_.begin();
       it != addresses_.end(); ++it) {
    data += " ";
    data += *it;
  }

  if (channel_map_.find(channel) != channel_map_.end()) {
    social_sender_->SendToPeer(channel_map_[channel].uid, data);
  }
  LOG(INFO) << __FUNCTION__ << " " << data;
}

void SvpnConnectionManager::OnRoleConflict(
    cricket::TransportChannelImpl* channel) {
  LOG(INFO) << __FUNCTION__ << " " << "CONFLICT";
}

void SvpnConnectionManager::DestroyChannel(
    cricket::TransportChannel* channel) {
  LOG(INFO) << __FUNCTION__ << " " << "DESTROYING";
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  channel_map_[channel].transport->DestroyChannel(component);
}

void SvpnConnectionManager::OnReadableState(
    cricket::TransportChannel* channel) {
  LOG(INFO) << __FUNCTION__ << " " << "R " << channel->readable()
            << " W " << channel->writable();
  if (!channel->readable() && !channel->writable()) {
    DestroyChannel(channel);
  }
}

void SvpnConnectionManager::OnWritableState(
    cricket::TransportChannel* channel) {
  LOG(INFO) << __FUNCTION__ << " " << "R " << channel->readable()
            << " W " << channel->writable();
  if (!channel->readable() && !channel->writable()) {
    DestroyChannel(channel);
  }
}

void SvpnConnectionManager::OnReadPacket(
    cricket::TransportChannel* channel, 
    const char* data, size_t len, int flags) {

  const char* dest_id = data + kIdSize;
  std::string source(data, kResourceSize);
  std::string dest(dest_id, kResourceSize);
  LOG(INFO) << __FUNCTION__ << " " << len << " " << source << " " << dest;
  // TODO - make this configurable
  talk_base::SocketAddress addr(kLocalHost, kSvpnPort);
  int count = socket_->SendTo(data, len, addr);
}

void SvpnConnectionManager::HandlePacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  LOG(INFO) << __FUNCTION__ << " " << len;
  HandlePacketParams* params = new HandlePacketParams(data, len, addr);
  worker_thread_->Post(this, MSG_HANDLEPACKET, params);
}

void SvpnConnectionManager::HandlePacket_w(const char* data, size_t len, 
    const talk_base::SocketAddress& addr) {
  const char* dest_id = data + kIdSize;
  std::string source(data, kResourceSize);
  std::string dest(dest_id, kResourceSize);
  LOG(INFO) << __FUNCTION__ << " " << source << " " << dest;
  if (uid_map_.find(dest) != uid_map_.end()) {
    int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
    int count = uid_map_[dest].transport->GetChannel(
        component)->SendPacket(data, len, 0);
    LOG(INFO) << __FUNCTION__ << " SENT " << count;
  }
}

void SvpnConnectionManager::OnRouteChange(
    cricket::TransportChannel* channel, 
    const cricket::Candidate& candidate) {
  LOG(INFO) << __FUNCTION__ << " " << "ROUTE";
}

void SvpnConnectionManager::OnDestroyed(
    cricket::TransportChannel* channel) {
  LOG(INFO) << __FUNCTION__ << " " << "DESTROYED";
}

void SvpnConnectionManager::SetupTransport(
    cricket::P2PTransport* transport, const std::string& uid, 
    const std::string& fingerprint) {

  talk_base::SSLFingerprint* remote_fingerprint =
      talk_base::SSLFingerprint::CreateFromRfc4572(talk_base::DIGEST_SHA_1,
                                                   fingerprint);

  // TODO - Fix memory leaks as this is a prime example
  cricket::TransportDescription* local_description =
      new cricket::TransportDescription(
      cricket::NS_JINGLE_ICE_UDP, std::vector<std::string>(), kIceUfrag,
      kIcePwd, cricket::ICEMODE_FULL, local_fingerprint_, 
      cricket::Candidates());
  cricket::TransportDescription* remote_description =
      new cricket::TransportDescription(
      cricket::NS_JINGLE_ICE_UDP, std::vector<std::string>(), kIceUfrag, 
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

void SvpnConnectionManager::CreateConnection(
    const std::string& uid, const std::string& fingerprint) {
  std::string uid_key = get_key(uid);
  LOG(INFO) << __FUNCTION__ << " " << uid_key;
  PeerState peer_state;
  if (uid_map_.find(uid_key) != uid_map_.end()) {
    peer_state = uid_map_[uid_key];
  }
  else {
    peer_state.uid = uid;
    peer_state.transport = new DtlsP2PTransport(
        signaling_thread_, worker_thread_, content_name_, 
        &port_allocator_, identity_);
  }

  if (peer_state.transport->HasChannels()) return;

  cricket::DtlsTransportChannelWrapper* channel =
      static_cast<cricket::DtlsTransportChannelWrapper*>(
          peer_state.transport->CreateChannel(
              cricket::ICE_CANDIDATE_COMPONENT_DEFAULT));
  channel->SignalRequestSignaling.connect(
      this, &SvpnConnectionManager::OnRequestSignaling);
  channel->SignalCandidateReady.connect(
      this, &SvpnConnectionManager::OnCandidateReady);
  channel->SignalCandidatesAllocationDone.connect(
      this, &SvpnConnectionManager::OnCandidatesAllocationDone);
  channel->SignalRoleConflict.connect(
      this, &SvpnConnectionManager::OnRoleConflict);
  channel->SignalReadableState.connect(
      this, &SvpnConnectionManager::OnReadableState);
  channel->SignalWritableState.connect(
      this, &SvpnConnectionManager::OnWritableState);
  channel->SignalReadPacket.connect(
      this, &SvpnConnectionManager::OnReadPacket);
  channel->SignalRouteChange.connect(
      this, &SvpnConnectionManager::OnRouteChange);
  channel->SignalDestroyed.connect(
      this, &SvpnConnectionManager::OnDestroyed);

  std::vector<std::string> ciphers;
  ciphers.push_back(AES_CM_128_HMAC_SHA1_32);
  channel->SetSrtpCiphers(ciphers);

  uid_map_[uid_key] = peer_state;
  channel_map_[channel] = peer_state;
  SetupTransport(peer_state.transport, uid, fingerprint);
  peer_state.transport->ConnectChannels();
}

void SvpnConnectionManager::DeleteConnection(const std::string& uid) {
}

void SvpnConnectionManager::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_SETSOCKET: {
        SetSocket_w();
      }
      break;
    case MSG_HANDLEPACKET: {
        HandlePacketParams* params = 
            static_cast<HandlePacketParams*>(msg->pdata);
        HandlePacket_w(params->data, params->len, params->addr);
        delete params;
      }
      break;
  }
}

void SvpnConnectionManager::SetSocket_w() {
  talk_base::SocketAddress local_address(kLocalHost, 0);
  socket_ = packet_factory_.CreateUdpSocket(local_address, kNetworkPort,
                                               kNetworkPort);
  socket_->SignalReadPacket.connect(
      this, &sjingle::SvpnConnectionManager::HandlePacket);
}

cricket::Candidate SvpnConnectionManager::MakeCandidate(
    const std::string& uid, const std::string& addr_string) {
  talk_base::SocketAddress address;
  address.FromString(addr_string);
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  const cricket::Candidate candidate(
      uid, component, kCandidateProtocolUdp, address,
      kCandidatePriority, kIceUfrag, kIcePwd, cricket::LOCAL_PORT_TYPE,
      "", kCandidateGeneration, kCandidateFoundation);
  return candidate;
}

void SvpnConnectionManager::HandlePeer(const std::string& uid,
                                       const std::string& data) {
  LOG(INFO) << __FUNCTION__ << " " << uid << " " << data;

  if (data.compare(0, sizeof(kFprPrefix) - 1, kFprPrefix) == 0) {
    CreateConnection(uid, data.substr(sizeof(kFprPrefix)));
  }
  else if (data.compare(0, sizeof(kAddrPrefix) - 1, kAddrPrefix) == 0) {
    std::istringstream iss(data.substr(sizeof(kAddrPrefix)));
    do {
      std::string addr_string;
      iss >> addr_string;
      if (addr_string.size() > 8 && addr_string.compare(0, 3, "172") != 0) {
        cricket::Candidates candidates;
        candidates.push_back(MakeCandidate(uid, addr_string));
        uid_map_[get_key(uid)].transport->OnRemoteCandidates(candidates);
      }
    } while (iss);
    if (uid.compare(social_sender_->uid()) > 0) {
      uid_map_[get_key(uid)].transport->OnSignalingReady();
      LOG(INFO) << __FUNCTION__ << " SIGNALING " << uid << " "
                << social_sender_->uid() ;
    }
  }
}

}  // namespace sjingle

int main(int argc, char **argcv) {
  talk_base::LogMessage::LogToDebug(talk_base::LS_INFO);
  talk_base::InitializeSSL();

  std::cout << "User Name: ";
  std::string username;
  std::cin >> username;

  std::cout << "Password: ";
  std::string password;
  std::cin >> password;

  std::cout << "Uid: ";
  std::string uid;
  std::cin >> uid;

  talk_base::InsecureCryptStringImpl pass;
  pass.password() = password;

  std::string resource(sjingle::kXmppPrefix);
  buzz::Jid jid(username);
  buzz::XmppClientSettings xcs;
  xcs.set_user(jid.node());
  xcs.set_host(jid.domain());
  xcs.set_resource(resource + uid);
  xcs.set_use_tls(buzz::TLS_REQUIRED);
  xcs.set_pass(talk_base::CryptString(pass));
  xcs.set_server(talk_base::SocketAddress(sjingle::kXmppHost, 
                                          sjingle::kXmppPort));
  talk_base::AutoThread signaling_thread;
  talk_base::Thread worker_thread;
  talk_base::Thread network_thread;
  signaling_thread.WrapCurrent();
  worker_thread.Start();
  network_thread.Start();

  buzz::XmppPump pump;
  pump.DoLogin(xcs, new buzz::XmppSocket(buzz::TLS_REQUIRED), 0);
  sjingle::XmppNetwork network(pump.client());
  sjingle::SvpnConnectionManager manager(network.sender(), &signaling_thread,
                                         &worker_thread, &network_thread,
                                         uid);
  std::string status(sjingle::kFprPrefix);
  status += " ";
  status += manager.fingerprint();
  network.set_status(status);

  network.sender()->HandlePeer.connect(&manager,
      &sjingle::SvpnConnectionManager::HandlePeer);
  signaling_thread.Run();
  return 0;
}

