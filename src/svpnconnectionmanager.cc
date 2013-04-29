
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
static const int kMinPort = 5810; // need to revise, may impose limit of 10
static const int kMaxPort = 5820;
static const uint64 kTieBreaker = 111111;
static const char kCandidateProtocolUdp[] = "udp";
static const uint32 kCandidatePriority = 2130706432U;
static const uint32 kCandidateGeneration = 0;
static const char kCandidateFoundation[] = "a0+B/1";
static const char kIceUfrag[] = "SVPNICEUFRAG0001";
static const char kIcePwd[] = "SVPNICEPWD00000000000001";
static const int kBufferSize = 1500;

const uint32 kFlags = cricket::PORTALLOCATOR_ENABLE_SHARED_UFRAG |
                      cricket::PORTALLOCATOR_DISABLE_RELAY |
                      cricket::PORTALLOCATOR_DISABLE_TCP |
                      cricket::PORTALLOCATOR_ENABLE_SHARED_SOCKET;

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
      candidates_(),
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
  candidates_.push_back(candidate);
}

void SvpnConnectionManager::OnCandidatesAllocationDone(
    cricket::TransportChannelImpl* channel) {
  std::cout << "ALLOCATION DONE" << std::endl;
  std::string data(kContentName);
  for (cricket::Candidates::iterator it = candidates_.begin();
       it != candidates_.end(); ++it) {
    data += " ";
    data += it->address().ToString();
  }

  for (std::map<std::string, PeerState>::iterator it = uid_map_.begin();
       it != uid_map_.end(); ++it) {
    social_sender_->SendToPeer(it->first, data);
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
  talk_base::SocketAddress addr("127.0.0.1", 5800);
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
  if (uid_map_.find(uid) != uid_map_.end()) {
    return;
  }

  cricket::TransportChannelImpl* channel = 
      transport_.CreateChannel(peer_idx_);
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
  uid_map_[uid] = peer_state;
  channel_map_[channel] = peer_state;
}

void SvpnConnectionManager::DeleteConnection(const std::string& uid) {
}

void SvpnConnectionManager::AddPeerAddress(const std::string& uid,
                                           const std::string& addr_string) {
  if (uid_map_.find(uid) != uid_map_.end()) {
    PeerState peer_state = uid_map_[uid];
    for (cricket::Candidates::iterator it = peer_state.candidates.begin();
         it != peer_state.candidates.end(); ++it) {
      if (addr_string.compare(it->address().ToString()) == 0) { 
        return;
      }
    }

    int peer_idx = uid_map_[uid].peer_idx;
    talk_base::SocketAddress address;
    address.FromString(addr_string);
    cricket::Candidate candidate(
        uid, peer_idx, kCandidateProtocolUdp, address, kCandidatePriority,
        kIceUfrag, kIcePwd, cricket::LOCAL_PORT_TYPE, "",
        kCandidateGeneration, kCandidateFoundation);
    uid_map_[uid].channel->OnCandidate(candidate);
    uid_map_[uid].candidates.push_back(candidate);
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
  // Temporary, sends to first connection
  for (std::map<std::string, PeerState>::iterator it = uid_map_.begin();
       it != uid_map_.end(); ++it) {
    it->second.channel->SendPacket(data, len, 0);
    break;
  }

  /*
  std::string uid(data, 20);
  if (uid_map_.find(uid) != uid_map_.end()) {
    uid_map_[uid].channel->SendPacket(data, len, flags);
  }
  */
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

  talk_base::InsecureCryptStringImpl pass;
  pass.password() = password;

  std::string resource(sjingle::kXmppPrefix);
  buzz::Jid jid(username);
  buzz::XmppClientSettings xcs;
  xcs.set_user(jid.node());
  xcs.set_host(jid.domain());
  xcs.set_resource(resource + talk_base::CreateRandomString(10));
  xcs.set_use_tls(buzz::TLS_REQUIRED);
  xcs.set_pass(talk_base::CryptString(pass));
  xcs.set_server(talk_base::SocketAddress(sjingle::kXmppHost, 
                                          sjingle::kXmppPort));
  talk_base::AutoThread signaling_thread;
  talk_base::Thread worker_thread;
  worker_thread.Start();

  // TODO - create a third seperate thread for packet handling
  talk_base::SocketAddress local_address("127.0.0.1", 0);
  talk_base::BasicPacketSocketFactory packet_factory(
      talk_base::Thread::Current());
  talk_base::AsyncPacketSocket* udp_socket = packet_factory.CreateUdpSocket(
      local_address, 5700, 5710);

  buzz::XmppPump pump;
  pump.DoLogin(xcs, new buzz::XmppSocket(buzz::TLS_REQUIRED), 0);
  sjingle::XmppNetwork network(pump.client());
  sjingle::SvpnConnectionManager manager(network.sender(), udp_socket,
                                         &signaling_thread, &worker_thread);
  network.sender()->HandlePeer.connect(&manager,
      &sjingle::SvpnConnectionManager::HandlePeer);

  talk_base::Thread::Current()->Run();
  return 0;
}

