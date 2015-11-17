/*
 * ipop-tincan
 * Copyright 2015, University of Florida
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

#include <iostream>
#include <sstream>

#include "talk/base/logging.h"
#include "talk/base/bind.h"
#include "talk/base/stringencode.h"
#include "tincan_utils.h"
#include "tincanconnectionmanager.h"

namespace tincan {
using talk_base::Bind;

static const char kIpv4[] = "172.31.0.100";
static const char kIpv6[] = "fd50:0dbc:41f2:4a3c:0000:0000:0000:0000";
static const char kFprNull[] =
    "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00";
static const char kContentName[] = "ipop-tincan";
static const char kIceUfrag[] = "ufrag";
static const char kIcePwd[] = "pwd";
static const int kBufferSize = 1500;
static const size_t kIdBytesLen = 20;
static const uint32 kFlags = 0;
static const uint32 kLocalControllerId = 0;

// this is an optimization for decode 20-byte hearders, we only
// decode 8 bytes instead of 20-bytes because hex_decode is
// costly operation and 8 bytes is enough entropy for small
// networks to avoid collisions (birthday problem)
static const size_t kShortLen = 8;
// this is a hack because we are depending on a global variable
// to set the pointer for instance of this same class
// sadly this is necessary for communication with ipop-tap
// TODO - Replace this ugly hack with something smarter
static TinCanConnectionManager* g_manager = 0;

// these blocking queues used to communicate with ipop-tap,
// these are required to be static global variables because
// ipop-tap is written in C and uses function pointers to access
// this portion of the code, this can probably be done in a smarter way
static wqueue<talk_base::Buffer*> g_recv_queue;
static wqueue<talk_base::Buffer*> g_send_queue;

// when the destination uid of a packet is set to this constant it means
// that a P2P connection does not exist and this packet is sent to
// a forwarder (i.e. the controller)
static const char kNullPeerId[] = "00000000000000000000";

// delimiter for candidate string parameters
static const char kCandidateDelim[] = ":";

// constants sent to controller to indicate different types of connection
// notifications
static const char kConStat[] = "con_stat";
static const char kConReq[] = "con_req";
static const char kConResp[] = "con_resp";

// enumeration used by OnMessage function
enum {
  MSG_QUEUESIGNAL = 0,
};

TinCanConnectionManager::TinCanConnectionManager(
    PeerSignalSenderInterface* signal_sender,
    talk_base::Thread* link_setup_thread,
    talk_base::Thread* packet_handling_thread,
    thread_opts_t* opts)
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
      fingerprint_(kFprNull),
      tiebreaker_(talk_base::CreateRandomId64()),
      tincan_ip4_(kIpv4),
      tincan_ip6_(kIpv6),
      tap_name_(kTapName),
      packet_options_(talk_base::DSCP_DEFAULT),
      trim_enabled_(false),
      opts_(opts) {
  // we have to set the global point for ipop-tap communication
  g_manager = this;

  // we set event handler for network change in order to disable
  // ipop VNIC from list of devices uses by libjingle
  network_manager_.SignalNetworksChanged.connect(
      this, &TinCanConnectionManager::OnNetworksChanged);
}

void TinCanConnectionManager::Setup(
    const std::string& uid, const std::string& ip4, int ip4_mask,
    const std::string& ip6, int ip6_mask, int subnet_mask, int switchmode) {

  // input verification before proceeding
  if (!tincan_id_.empty() || uid.size() != kIdSize) return;

  // tincan id is uid
  tincan_id_ = uid;

  // we create X509 identity for secure connections
  identity_.reset(talk_base::SSLIdentity::Generate(tincan_id_));
  local_fingerprint_.reset(talk_base::SSLFingerprint::Create(
      talk_base::DIGEST_SHA_1, identity_.get()));

  // verifies that local_fingerprint is not null before calling function
  if (local_fingerprint_.get()) {
    fingerprint_ = local_fingerprint_->GetRfc4572Fingerprint();
  }

  // translate string based uid to byte based uid
  char uid_str[kIdBytesLen];
  talk_base::hex_decode(uid_str, kIdBytesLen, uid);

  int error = 0;
#if defined(LINUX) || defined(ANDROID)
  // Configure ipop tap VNIC through Linux sys calls
  error |= tap_set_ipv4_addr(ip4.c_str(), ip4_mask, opts_->my_ip4);
  error |= tap_set_ipv6_addr(ip6.c_str(), ip6_mask);
  error |= tap_set_mtu(MTU) | tap_set_base_flags() | tap_set_up();
  if (switchmode) { error |= tap_unset_noarp_flags(); }
#endif
  // set up ipop-tap parameters
  error |= peerlist_set_local_p(uid_str, ip4.c_str(), ip6.c_str());
  error |= set_subnet_mask(ip4_mask, subnet_mask);
  ASSERT(error == 0);
  tincan_ip4_ = ip4;
  tincan_ip6_ = ip6;
}

void TinCanConnectionManager::OnNetworksChanged() {
  ASSERT(packet_handling_thread_->IsCurrent());
  talk_base::NetworkManager::NetworkList networks;
  talk_base::SocketAddress ip6_addr(tincan_ip6_, 0);
  network_manager_.GetNetworks(&networks);

  // We loop through each network interface and we disable ipop tap
  // interface because we don't want libjingle to try to connect
  // over ipop network that can create weird conditions and break things
  for (size_t i = 0; i < networks.size(); ++i) {
	  if (networks[i]->name().compare(kTapName) == 0 ||
		  networks[i]->description().compare(0, 3, kTapDesc) == 0) {
		  networks[i]->ClearIPs();
      // Set to a random ipv6 address in order to disable
      networks[i]->AddIP(ip6_addr.ipaddr());
	  LOG_TS(INFO) << "IPOP TAP Device to be ignored " << networks[i]->name() << ":" << networks[i]->description();
    }
  }
}

void TinCanConnectionManager::OnRequestSignaling(
    cricket::Transport* transport) {
  ASSERT(link_setup_thread_->IsCurrent());
  // boiler plate libjingle code
  transport->OnSignalingReady();
}

void TinCanConnectionManager::HandleConnectionSignal(
    cricket::Port* port, cricket::Connection* connection) {
  ASSERT(packet_handling_thread_->IsCurrent());

  // This function is called after a connection is already online and
  // therefore it prunes every additional relay connection because
  // they are not necessary
  if (trim_enabled_ && port->Type() == cricket::RELAY_PORT_TYPE) {
    connection->Prune();
    LOG_TS(LS_VERBOSE) << "TRIMMING " << connection->ToString();
  }
}

void TinCanConnectionManager::OnRWChangeState(
    cricket::Transport* transport) {
  ASSERT(link_setup_thread_->IsCurrent());
  std::string uid = transport_map_[transport];
  std::string status = "unknown";
  if (transport->readable() && transport->writable()) {
    status = "online";
    LOG_TS(INFO) << "ONLINE " << uid << " " << talk_base::Time();
  }
  else if (transport->was_writable()) {
    status = "offline";
    LOG_TS(INFO) << "OFFLINE " << uid << " " << talk_base::Time();
  }
  // callback message sent to local controller for connection status
  signal_sender_->SendToPeer(kLocalControllerId, uid, status, kConStat);

  if (status == "online") {
    // Go through all of the ports and bind to each new connection
    // creation this will be useful for trimming and other things
    const std::vector<cricket::PortInterface*>& ports =
        uid_map_[uid]->channel->ports();
    for (size_t i = 0; i < ports.size(); i++) {
      cricket::Port* port = static_cast<cricket::Port*>(ports[i]);
      port->SignalConnectionCreated.connect(
            this, &TinCanConnectionManager::HandleConnectionSignal);
    }
  }
}

void TinCanConnectionManager::OnCandidatesReady(
    cricket::Transport* transport, const cricket::Candidates& candidates) {
  ASSERT(link_setup_thread_->IsCurrent());
  std::string uid = transport_map_[transport];
  std::set<std::string>& candidate_list = uid_map_[uid]->candidate_list;
  for (size_t i = 0; i < candidates.size(); i++) {
    if (candidates[i].network_name().compare(kTapName) == 0) continue;
    size_t idx = candidates[i].network_name().find(' ');
    std::string interface = candidates[i].network_name().substr(0, idx);
    std::string ip_string =
        talk_base::SocketAddress::IPToString(candidates[i].address().ip());

    // here we built a colon delimited set of parameters required by
    // libjingle/ICE protocol to create P2P connections
    std::ostringstream oss;
    oss << candidates[i].id() << kCandidateDelim << candidates[i].component()
        << kCandidateDelim << candidates[i].protocol()
        << kCandidateDelim << ip_string
        << kCandidateDelim << candidates[i].address().port() 
        << kCandidateDelim << candidates[i].priority() 
        << kCandidateDelim << candidates[i].username() 
        << kCandidateDelim << candidates[i].password() 
        << kCandidateDelim << candidates[i].type() 
        << kCandidateDelim << interface
        << kCandidateDelim << candidates[i].generation() 
        << kCandidateDelim << candidates[i].foundation(); 
    candidate_list.insert(oss.str());
  }
}

void TinCanConnectionManager::OnCandidatesAllocationDone(
    cricket::Transport* transport) {
  ASSERT(link_setup_thread_->IsCurrent());
  std::string uid = transport_map_[transport];
  std::set<std::string>& candidates = uid_map_[uid]->candidate_list;
  int overlay_id = uid_map_[uid]->overlay_id;
  std::string data(fingerprint());

  // we create a space delimited list of candidate information and
  // then send that information over XMPP to other peer to create
  // P2P connections
  for (std::set<std::string>::const_iterator it = candidates.begin();
       it != candidates.end(); ++it) {
    data += " ";
    data += *it;
  }
  if (transport_map_.find(transport) != transport_map_.end()) {
    // for now overlay_id is typically 1 meaning send over XMPP if it
    // is 0 that means send through the controller
    signal_sender_->SendToPeer(overlay_id, transport_map_[transport],
                               data, kConResp);
  }
}

void TinCanConnectionManager::OnReadPacket(cricket::TransportChannel* channel, 
    const char* data, size_t len, const talk_base::PacketTime& ptime,
    int flags) {
  ASSERT(packet_handling_thread_->IsCurrent());
  if (len < kHeaderSize) return;

  // we are processing incoming code from the P2P network, first we convert
  // 20-byte uid from bytes to string, this is a costly operation especially
  // if we want high bandwidth throughput. The kShortLen parameter is used
  // to limit the encode to only 8 bytes instead of 20 bytes. This is
  // sufficient for a small network because 8 bytes (64-bits of entropy)
  // which is enough (birthday problem). We are required to use strings
  // for lookup tables because many lookup tables depend on string as key.
  std::string source = talk_base::hex_encode(data, kShortLen);
  std::string dest = talk_base::hex_encode(data + kIdBytesLen, kShortLen);
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  if (short_uid_map_.find(source) != short_uid_map_.end() && 
      short_uid_map_[source]->GetChannel(component) == channel) {
    // add to receive for processing by ipop-tap
    g_recv_queue.add(new talk_base::Buffer(data, len));
  }
}

void TinCanConnectionManager::HandlePacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  ASSERT(packet_handling_thread_->IsCurrent());
  if (len < (kHeaderSize)) return;
  std::string source = talk_base::hex_encode(data, kShortLen);
  std::string dest = talk_base::hex_encode(data + kIdBytesLen, kShortLen);

  // forward packet to controller if we do not have a P2P connection for it
  if (dest.compare(0, 3, kNullPeerId) == 0 ||
      short_uid_map_.find(dest) == short_uid_map_.end()) {
    // forward_addr_ is the address of the forwarder/controller
    talk_base::scoped_ptr<char[]> msg(new char[len + kTincanHeaderSize]);

    // Put IPOP version field in the header (offset 0 field)
    *(msg.get() + kTincanVerOffset) = kIpopVer;

    /* This block intended for the message that is passed through TinCan link
       the destination uid field is NULL. NULLing is done one in recv thread in
       Tap. Based on MAC address, we tag it as ICC control or ICC packet. (To
       distinguish MAC address, we use mac address 00-69-70-6f-70-03 for ICC
       control 00-69-70-6f-70-04 for ICC packet. Ascii code of ipop is 69706f70
    */
    if (len > (kHeaderSize + 6) && is_icc((unsigned char *) data)) {
        if (data[kHeaderSize+kICCMacOffset] == kICCPacket) {
            *(msg.get() + kTincanMsgTypeOffset) = kICCPacket;
        } else if (data[kHeaderSize+kICCMacOffset] == kICCControl) {
            *(msg.get() + kTincanMsgTypeOffset) = kICCControl;
        }
    } else {
        *(msg.get() + kTincanMsgTypeOffset) = kTincanPacket;
    }

    memcpy(msg.get() + kTincanHeaderSize, data, len);
    forward_socket_->SendTo(msg.get(), len + kTincanHeaderSize,
        forward_addr_, packet_options_);
  } 
  else if (short_uid_map_[dest]->writable()) {
    int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
    cricket::TransportChannelImpl* channel = 
        short_uid_map_[dest]->GetChannel(component);
    if (channel != NULL) {
      // Send packet over Tincan P2P connection
      int count = channel->SendPacket(data, len, packet_options_, 0);
    }
  }
}

bool TinCanConnectionManager::SetRelay(
    PeerState* peer_state, const std::string& turn_server,
    const std::string& username, const std::string& password) {
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
  LOG_TS(INFO) << "TURN " << turn_addr.ToString();
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
        *peer_state->local_description, cricket::CA_OFFER, NULL);
    peer_state->transport->SetRemoteTransportDescription(
        *peer_state->remote_description, cricket::CA_ANSWER, NULL);
  }
  else {
    peer_state->transport->SetIceRole(cricket::ICEROLE_CONTROLLED);
    peer_state->transport->SetRemoteTransportDescription(
        *peer_state->remote_description, cricket::CA_OFFER, NULL);
    peer_state->transport->SetLocalTransportDescription(
        *peer_state->local_description, cricket::CA_ANSWER, NULL);
  }
}

bool TinCanConnectionManager::CreateTransport(
    const std::string& uid, const std::string& fingerprint, int overlay_id,
    const std::string& stun_server, const std::string& turn_server,
    const std::string& turn_user, const std::string& turn_pass,
    const bool sec_enabled) {
  if (uid_map_.find(uid) != uid_map_.end() || tincan_id_ == uid) {
    LOG_TS(INFO) << "EXISTS " << uid;
    return false;
  }

  LOG_TS(INFO) << "peer_uid:" << uid << " time:" << talk_base::Time();
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
  if (sec_enabled && local_fingerprint_.get() &&
      fingerprint.compare(kFprNull) != 0) {
    DtlsP2PTransport* dtls_transport = new DtlsP2PTransport(
        link_setup_thread_, packet_handling_thread_, content_name_, 
        peer_state->port_allocator.get(), identity_.get());
    peer_state->transport.reset(dtls_transport);
    cricket::DtlsTransportChannelWrapper* dtls_channel =
        static_cast<cricket::DtlsTransportChannelWrapper*>(
            dtls_transport->CreateChannel(component));
    channel = dtls_channel;
    peer_state->channel = static_cast<cricket::P2PTransportChannel*>(
                              dtls_channel->channel());
    peer_state->connection_security = "dtls";
  }
  else {
    peer_state->transport.reset(new cricket::P2PTransport(
        link_setup_thread_, packet_handling_thread_, content_name_, 
        peer_state->port_allocator.get()));
    channel = peer_state->transport->CreateChannel(component);
    peer_state->channel = static_cast<cricket::P2PTransportChannel*>(
                              channel);
    peer_state->connection_security = "none";
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
  packet_handling_thread_->Invoke<void>(
    Bind(&TinCanConnectionManager::InsertTransportMap_w, this,
         uid.substr(0, kShortLen * 2), peer_state->transport.get()));
  LOG_TS(INFO) << "CREATED " << uid;
  return true;
}

bool TinCanConnectionManager::AddIPMapping(
    const std::string& uid, const std::string& ip4, const std::string& ip6) {
  ASSERT(link_setup_thread_->IsCurrent());
  if (ip4.empty() || ip6.empty() || uid.size() != kIdSize || 
      ip_map_.find(uid) != ip_map_.end()) return false;
  char uid_str[kIdBytesLen];
  talk_base::hex_decode(uid_str, kIdBytesLen, uid);
  // TODO - this override call should go away, only there for compatibility
  override_base_ipv4_addr_p(ip4.c_str());

  // this create a UID to IP mapping in the ipop-tap peerlist
  if (ip4 == "127.0.0.1" ) { //TODO should be changed with switchmode flag
    peerlist_add_by_uid(uid_str);
  } else {
    peerlist_add_p(uid_str, ip4.c_str(), ip6.c_str(), 0);
  }
  PeerIPs ips;
  ips.ip4 = ip4;
  ips.ip6 = ip6;

  // we also store the assigned ip addresses to table for reuse
  ip_map_[uid] = ips;
  return true;
}

bool TinCanConnectionManager::CreateConnections(
    const std::string& uid, const std::string& candidates_string) {
  ASSERT(link_setup_thread_->IsCurrent());
  if (uid_map_.find(uid) == uid_map_.end()) return false;
  cricket::Candidates& candidates = uid_map_[uid]->candidates;
  if (candidates.size() > 0) return false;

  // this parses the string delimited list of candidates and adds
  // them to the P2P transport and therefore creating connections
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
  ASSERT(link_setup_thread_->IsCurrent());
  if (uid_map_.find(uid) == uid_map_.end()) return false;

  // This call destroys the P2P connection and deletes connection
  // because this calls destructor of PeerState which in turn calls the
  // destructors of all internal objects

  // We can't use async message posting, or there may be a window that
  // transport has been deleted, but short_uid_map_ still contains the
  // pointer. For the same reason, we must call short_uid_map_.erase
  // before destroy the transport
  // We can't use lock either, because destroying transport need invoke
  // worker thread to do the real work.
  packet_handling_thread_->Invoke<void>(
    Bind(&TinCanConnectionManager::DeleteTransportMap_w, this,
         uid.substr(0, kShortLen * 2)));
  PeerStatePtr peer = uid_map_[uid];
  transport_map_.erase(peer->transport.get());
  uid_map_.erase(uid);
  LOG_TS(INFO) << "DESTROYED " << uid;
  return true;
}

void TinCanConnectionManager::OnMessage(talk_base::Message* msg) {
  ASSERT(packet_handling_thread_->IsCurrent());
  switch (msg->message_id) {
    case MSG_QUEUESIGNAL: {
        HandleQueueSignal_w();
      }
      break;
  }
}

void TinCanConnectionManager::HandlePeer(const std::string& uid, 
    const std::string& data, const std::string& type) {
  ASSERT(link_setup_thread_->IsCurrent());
  signal_sender_->SendToPeer(kLocalControllerId, uid, data, type);
  LOG_TS(INFO) << "uid:" << uid << " data:" << data << " type:" << type;
}

int TinCanConnectionManager::DoPacketSend(const char* buf, size_t len) {
  g_send_queue.add(new talk_base::Buffer(buf, len));
  if (g_manager != 0) {
    // This is called when main_thread has to process outgoing packet
    g_manager->packet_handling_thread()->Post(g_manager, MSG_QUEUESIGNAL, 0);
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;
  }
  return len;
}

int TinCanConnectionManager::DoPacketRecv(char* buf, size_t len) {
  talk_base::scoped_ptr<talk_base::Buffer> packet(g_recv_queue.remove());
  if (packet->length() > len) {
    return -1;
  }
  memcpy(buf, packet->data(), packet->length());
  return packet->length();
}

int TinCanConnectionManager::SendToTap(const char* buf, size_t len) {
  g_recv_queue.add(new talk_base::Buffer(buf, len));
  return len;
}

void TinCanConnectionManager::HandleQueueSignal_w() {
  ASSERT(packet_handling_thread_->IsCurrent());
  talk_base::scoped_ptr<talk_base::Buffer> packet(g_send_queue.remove());
  HandlePacket(0, packet->data(), packet->length(), forward_addr_);
}

void TinCanConnectionManager::GetChannelStats_w(const std::string &uid,
                                                cricket::ConnectionInfos *infos)
{
  cricket::TransportChannelImpl* channel;
  int component = cricket::ICE_CANDIDATE_COMPONENT_DEFAULT;

  // Invoke by link_setup_thread_, so we are safe to access uid_map_ here
  channel = uid_map_[uid]->transport->GetChannel(component);
  channel->GetStats(infos);
}

void TinCanConnectionManager::InsertTransportMap_w(const std::string sub_uid,
                                                   cricket::Transport* transport)
{
  if (short_uid_map_.find(sub_uid) != short_uid_map_.end()) {
    LOG_TS(LERROR) << "uid: " << sub_uid << " already exists" << endl;
  }
  short_uid_map_[sub_uid] = transport;
}

void TinCanConnectionManager::DeleteTransportMap_w(const std::string sub_uid)
{
  if (short_uid_map_.find(sub_uid) == short_uid_map_.end()) {
    // There is some bug here. So log it.
    LOG_TS(LERROR) << "Can't find uid: " << sub_uid;
  } else {
    short_uid_map_.erase(sub_uid);
  }
}

Json::Value TinCanConnectionManager::StateToJson(const std::string& uid,
                                                 uint32 xmpp_time,
                                                 bool get_stats) {
  Json::Value peer(Json::objectValue);
  peer["uid"] = uid;
  peer["status"] = "offline";

  // time_diff gives the amount of time since last xmpp presense message
  uint32 time_diff = talk_base::Time() - xmpp_time;
  peer["xmpp_time"] = time_diff/1000;

  if (ip_map_.find(uid) != ip_map_.end()) {
    peer["ip4"] = ip_map_[uid].ip4;
    peer["ip6"] = ip_map_[uid].ip6;
  }

  if (uid_map_.find(uid) != uid_map_.end()) {
    peer["fpr"] = uid_map_[uid]->fingerprint;

    // time_diff gives the amount of time since connection was created
    time_diff = talk_base::Time() - uid_map_[uid]->last_time;
    peer["last_time"] = time_diff/1000;

    // if transport is readable and writable that means P2P connection 
    // is online and ready to send packets
    if (uid_map_[uid]->transport->readable() && 
        uid_map_[uid]->transport->writable()) {
      peer["status"] = "online";
      peer["security"] = uid_map_[uid]->connection_security;
#if !defined(WIN32)
        // For some odd reason, GetStats fails on WIN32
      if (get_stats) {
        cricket::ConnectionInfos infos;
        packet_handling_thread_->Invoke<void>(
          Bind(&TinCanConnectionManager::GetChannelStats_w, this,
               uid,&infos));

        Json::Value stats(Json::arrayValue);
        for (int i = 0; i < infos.size(); i++) {
          Json::Value stat(Json::objectValue);
          stat["local_addr"] = infos[i].local_candidate.address().ToString();
          stat["rem_addr"] = infos[i].remote_candidate.address().ToString();
          stat["local_type"] = infos[i].local_candidate.type();
          stat["rem_type"] = infos[i].remote_candidate.type();
          stat["best_conn"] = infos[i].best_connection;
          stat["writable"] = infos[i].writable;
          stat["readable"] = infos[i].readable;
          stat["timeout"] = infos[i].timeout;
          stat["new_conn"] = infos[i].new_connection;
          stat["rtt"] = (uint) infos[i].rtt;
          stat["sent_total_bytes"] = (uint) infos[i].sent_total_bytes;
          stat["sent_bytes_second"] = (uint) infos[i].sent_bytes_second;
          stat["recv_total_bytes"] = (uint) infos[i].recv_total_bytes;
          stat["recv_bytes_second"] = (uint) infos[i].recv_bytes_second;
          stats.append(stat);
        }
        peer["stats"] = stats;
      }
#endif
    }
  }
  return peer;
}

Json::Value TinCanConnectionManager::GetState(
    const std::map<std::string, uint32>& friends, bool get_stats) {
  ASSERT(link_setup_thread_->IsCurrent());
  Json::Value peers(Json::objectValue);
  for (std::map<std::string, uint32>::const_iterator it =
       friends.begin(); it != friends.end(); ++it) {
    peers[it->first] = StateToJson(it->first, it->second, get_stats);
  }
  return peers;
}

bool TinCanConnectionManager::is_icc(const unsigned char * buf) {
  int offset = kIdBytesLen*2;
  return buf[offset] == 0x00 && buf[offset+1] == 0x69 &&
         buf[offset+2] == 0x70 && buf[offset+3] == 0x6f &&
         buf[offset+4] == 0x70;
}

}  // namespace tincan

