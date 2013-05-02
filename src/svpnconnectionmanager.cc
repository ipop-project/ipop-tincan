
#include <iostream>
#include <sstream>

#include "talk/base/ssladapter.h"
#include "talk/base/logging.h"
#include "talk/xmpp/xmppsocket.h"
#include "talk/xmpp/xmpppump.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/basicpacketsocketfactory.h"
#include "talk/base/asyncpacketsocket.h"

#include "svpnconnectionmanager.h"

namespace sjingle {

static const char kXmppHost[] = "talk.google.com";
static const int kXmppPort = 5222;
static const char kStunServer[] = "stun.l.google.com";
static const int kStunPort = 19302;
static const char kContentName[] = "svpn-jingle";
static const bool kAllowTcpListen = false;
static const uint64 kTieBreaker = 111111;
static const char kCandidateProtocolUdp[] = "udp";
static const uint32 kCandidatePriority = 2130706432U;
static const uint32 kCandidateGeneration = 0;
static const char kCandidateFoundation[] = "a0+B/1";
static const char kIceUfrag[] = "SVPNICEUFRAG0001";
static const char kIcePwd[] = "SVPNICEPWD00000000000001";
static const int kSvpnPort = 5800;
static const int kNetworkPort = 5801;
static const int kMinPort = 5802; // need to revise, may impose limit of 10
static const int kMaxPort = 5820;
static const char kLocalHost[] = "127.0.0.1";
static const int kBufferSize = 1500;
static const int kIdSize = 20;

const uint32 kFlags = cricket::PORTALLOCATOR_ENABLE_SHARED_UFRAG |
                      cricket::PORTALLOCATOR_DISABLE_RELAY |
                      cricket::PORTALLOCATOR_DISABLE_TCP |
                      cricket::PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                      cricket::PORTALLOCATOR_ENABLE_BUNDLE;

enum {
  MSG_HANDLEPEER = 1,
  MSG_HANDLEPACKET = 2
};

struct HandlePeerParams : public talk_base::MessageData {
  HandlePeerParams(const std::string& param_uid,
                   const std::string& param_data)
      : uid(param_uid), data(param_data) {}
  const std::string uid;
  const std::string data;
};

struct HandlePacketParams : public talk_base::MessageData {
  HandlePacketParams(const char* param_data, size_t param_len, 
                     const talk_base::SocketAddress& param_addr)
      : data(), len(param_len), addr(param_addr) {
    if (len <= kBufferSize) {
      memcpy(data, param_data, param_len);
    }
    else {
      // error
    }
  }
  char data[kBufferSize];
  const size_t len;
  const talk_base::SocketAddress addr;
};

SvpnConnectionManager::SvpnConnectionManager(
    SocialNetworkSenderInterface* social_sender,
    talk_base::AsyncPacketSocket* socket,
    talk_base::Thread* signaling_thread,
    talk_base::Thread* worker_thread) 
    : peer_idx_(0),
      content_name_(kContentName),
      social_sender_(social_sender),
      socket_(socket),
      uid_map_(),
      channel_map_(),
      addresses_(),
      stun_server_(kStunServer, kStunPort),
      network_manager_(),
      port_allocator_(&network_manager_, stun_server_,
                      talk_base::SocketAddress(),
                      talk_base::SocketAddress(),
                      talk_base::SocketAddress()),
      transport_(signaling_thread, worker_thread, content_name_, 
                 &port_allocator_),
      transport_description_(cricket::NS_JINGLE_ICE_UDP,
                             std::vector<std::string>(),
                             kIceUfrag, kIcePwd, 
                             cricket::ICEMODE_FULL,
                             0, cricket::Candidates()) {
  port_allocator_.set_flags(kFlags);
  port_allocator_.set_allow_tcp_listen(kAllowTcpListen);
  port_allocator_.SetPortRange(kMinPort, kMaxPort);
  transport_.SetLocalTransportDescription(transport_description_,
                                          cricket::CA_OFFER);
  transport_.SetRemoteTransportDescription(transport_description_,
                                           cricket::CA_ANSWER);
  transport_.SetTiebreaker(kTieBreaker);
  transport_.SetRole(cricket::ROLE_CONTROLLING);
  socket_->SignalReadPacket.connect(
      this, &sjingle::SvpnConnectionManager::HandlePacket);

}

void SvpnConnectionManager::OnRequestSignaling(
    cricket::TransportChannelImpl* channel) {
  channel->OnSignalingReady();
  std::cout << "REQUEST SIGNALING" << std::endl;
}

void SvpnConnectionManager::OnCandidateReady(
    cricket::TransportChannelImpl* channel, 
    const cricket::Candidate& candidate) {
  std::cout << "CANDIDATE READY" << std::endl;
  std::cout << candidate.ToString() << std::endl;
  addresses_.insert(candidate.address().ToString());
}

void SvpnConnectionManager::OnCandidatesAllocationDone(
    cricket::TransportChannelImpl* channel) {
  std::cout << "ALLOCATION DONE" << std::endl;
  std::string data(kContentName);
  for (std::set<std::string>::iterator it = addresses_.begin();
       it != addresses_.end(); ++it) {
    data += " ";
    data += *it;
  }

  if (channel_map_.find(channel) != channel_map_.end()) {
    social_sender_->SendToPeer(channel_map_[channel].uid, data);
  }
}

void SvpnConnectionManager::OnRoleConflict(
    cricket::TransportChannelImpl* channel) {
  std::cout << "ROLE CONFLICT" << std::endl;
}

void SvpnConnectionManager::OnReadableState(
    cricket::TransportChannel* channel) {
  std::cout << "READABLE STATE" << std::endl;
}

void SvpnConnectionManager::OnWritableState(
    cricket::TransportChannel* channel) {
  std::cout << "WRITABLE STATE" << std::endl;
}

void SvpnConnectionManager::OnReadPacket(
    cricket::TransportChannel* channel, 
    const char* data, size_t len, int flags) {
  std::cout << "READ PACKET SIZE " << len << std::endl;

  // TODO - set the real remote endpoint
  talk_base::SocketAddress addr(kLocalHost, kSvpnPort);
  socket_->SendTo(data, len, addr);
}

void SvpnConnectionManager::OnRouteChange(
    cricket::TransportChannel* channel, 
    const cricket::Candidate& candidate) {
  std::cout << "ROUTE CHANGE" << std::endl;
}

void SvpnConnectionManager::OnDestroyed(
    cricket::TransportChannel* channel) {
  std::cout << "DESTROYED" << std::endl;
}

void SvpnConnectionManager::CreateConnection(const std::string& uid) {
  std::string uid_key = uid.substr(uid.size() - kResourceSize);
  std::cout << "UID KEY " << uid_key << std::endl;
  cricket::TransportChannelImpl* channel;
  if (uid_map_.find(uid_key) != uid_map_.end()) {
    cricket::ConnectionInfos infos;
    channel = uid_map_[uid_key].channel;
    channel->GetStats(&infos);
    std::cout << "CONNECTIONS NUMBER " << infos.size() << std::endl;
    return;
  }

  channel = transport_.CreateChannel(peer_idx_);
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

  channel->Connect();
  PeerState peer_state;
  peer_state.peer_idx = peer_idx_++;
  peer_state.uid = uid;
  peer_state.channel = channel;
  uid_map_[uid_key] = peer_state;
  channel_map_[channel] = peer_state;
}

void SvpnConnectionManager::DeleteConnection(const std::string& uid) {
}

void SvpnConnectionManager::AddPeerAddress(const std::string& uid,
                                           const std::string& addr_string) {
  std::string uid_key = uid.substr(uid.size() - kResourceSize);
  if (uid_map_.find(uid_key) != uid_map_.end()) {
    std::set<std::string>& addresses = uid_map_[uid_key].addresses;
    if (addresses.find(addr_string) != addresses.end()) {
      // TODO - add logging statement 
      return;
    }

    talk_base::SocketAddress address;
    address.FromString(addr_string);
    const cricket::Candidate candidate(
        uid, uid_map_[uid_key].peer_idx, kCandidateProtocolUdp, address,
        kCandidatePriority, kIceUfrag, kIcePwd, cricket::LOCAL_PORT_TYPE,
        "", kCandidateGeneration, kCandidateFoundation);
    uid_map_[uid_key].channel->OnCandidate(candidate);
    uid_map_[uid_key].addresses.insert(addr_string);
  }
}

void SvpnConnectionManager::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_HANDLEPEER: {
        HandlePeerParams* params = static_cast<HandlePeerParams*>(msg->pdata);
        HandlePeer_w(params->uid, params->data);
        delete params;
      }
      break;
    case MSG_HANDLEPACKET: {
        HandlePacketParams* params = static_cast<HandlePacketParams*>(msg->pdata);
        HandlePacket_w(params->data, params->len, params->addr);
        delete params;
      }
      break;
  }
}

void SvpnConnectionManager::HandlePacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  std::cout << "PACKET RCV SIZE " << len << std::endl;
  HandlePacketParams* params = new HandlePacketParams(data, len, addr);
  transport_.worker_thread()->Post(this, MSG_HANDLEPACKET, params);
}

void SvpnConnectionManager::HandlePacket_w(const char* data, size_t len, 
    const talk_base::SocketAddress& addr) {
  const char* dest_id = data + kIdSize;
  std::string uid_key(dest_id, kResourceSize);
  std::cout << "PACKET UID KEY " << uid_key << std::endl;
  if (uid_map_.find(uid_key) != uid_map_.end()) {
    uid_map_[uid_key].channel->SendPacket(data, len, 0);
  }
}

void SvpnConnectionManager::HandlePeer(const std::string& uid,
                                       const std::string& data) {
  HandlePeerParams* params = new HandlePeerParams(uid, data);
  transport_.worker_thread()->Post(this, MSG_HANDLEPEER, params);
}

void SvpnConnectionManager::HandlePeer_w(const std::string& uid,
                                         const std::string& data) {
  CreateConnection(uid);
  if (data.compare(0, sizeof(kContentName) - 1, kContentName) == 0) {
    std::istringstream iss(data.substr(sizeof(kContentName)));
    do {
      std::string addr_string;
      iss >> addr_string;
      if (addr_string.size() > 8) {
        AddPeerAddress(uid, addr_string);
      }
    } while (iss);
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
  signaling_thread.WrapCurrent();
  worker_thread.Start();

  // TODO - create a third seperate thread for packet handling
  talk_base::SocketAddress local_address(sjingle::kLocalHost, 0);
  talk_base::BasicPacketSocketFactory packet_factory(&signaling_thread);
  talk_base::AsyncPacketSocket* udp_socket = packet_factory.CreateUdpSocket(
      local_address, sjingle::kNetworkPort, sjingle::kNetworkPort);

  buzz::XmppPump pump;
  pump.DoLogin(xcs, new buzz::XmppSocket(buzz::TLS_REQUIRED), 0);
  sjingle::XmppNetwork network(pump.client());
  sjingle::SvpnConnectionManager manager(network.sender(), udp_socket,
                                         &signaling_thread, &worker_thread);
  network.sender()->HandlePeer.connect(&manager,
      &sjingle::SvpnConnectionManager::HandlePeer);
  signaling_thread.Run();
  return 0;
}

