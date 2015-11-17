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

#ifndef TINCAN_CONNECTIONMANAGER_H_
#define TINCAN_CONNECTIONMANAGER_H_
#pragma once

#include <string>
#include <map>
#include <set>

#include "talk/base/sigslot.h"
#include "talk/p2p/base/p2ptransport.h"
#include "talk/p2p/client/basicportallocator.h"
#include "talk/p2p/base/transportdescription.h"
#include "talk/p2p/base/transportchannelimpl.h"
#include "talk/p2p/base/p2ptransportchannel.h"
#include "talk/p2p/base/dtlstransportchannel.h"
#include "talk/p2p/base/dtlstransport.h"
#include "talk/base/base64.h"
#include "talk/p2p/base/basicpacketsocketfactory.h"
#include "talk/base/asyncpacketsocket.h"
#include "talk/base/scoped_ref_ptr.h"
#include "talk/base/refcount.h"
#include "talk/base/json.h"
#include "talk/base/sslidentity.h"

#include "talk/ipop-project/ipop-tap/src/ipop_tap.h"
#include "talk/ipop-project/ipop-tap/src/tap.h"
#include "talk/ipop-project/ipop-tap/src/peerlist.h"
#include "talk/ipop-project/ipop-tap/src/packetio.h"

#include "peersignalsender.h"
#include "wqueue.h"

namespace tincan {

static const char kTapName[] = "ipop";
static const char kTapDesc[] = "TAP";

class PeerSignalSender : public PeerSignalSenderInterface {
 public:
  // Inherited from PeerSignalSenderInterface
  virtual void SendToPeer(int overlay_id, const std::string& uid,
                          const std::string& data, const std::string& type) {
    return service_map_[overlay_id]->SendToPeer(overlay_id, uid, data, type);
  }

  virtual void add_service(int overlay_id, 
                           PeerSignalSenderInterface* sender) {
    service_map_[overlay_id] = sender;
  }

 private:
  std::map<int, PeerSignalSenderInterface*> service_map_;

};

class TinCanConnectionManager : public talk_base::MessageHandler,
                                public sigslot::has_slots<> {

 public:
  TinCanConnectionManager(PeerSignalSenderInterface* signal_sender,
                          talk_base::Thread* link_setup_thread,
                          talk_base::Thread* packet_handling_thread,
                          thread_opts_t* opts);

  // Accessors
  const std::string fingerprint() const { return fingerprint_; }

  const std::string uid() const { return tincan_id_; }

  const std::string ipv4() const { return tincan_ip4_; }

  const std::string ipv6() const { return tincan_ip6_; }

  const std::string tap_name() const { return tap_name_; }
  
  talk_base::Thread* packet_handling_thread() const { return packet_handling_thread_; }

  void set_ip(const char* ip) { tincan_ip4_ = ip; }

  void set_forward_addr(const talk_base::SocketAddress addr) {
    forward_addr_ = addr;
  }

  void set_forward_socket(talk_base::AsyncPacketSocket* socket) {
    forward_socket_ = socket;
  }

  void set_trim_connection(bool trim) {
    trim_enabled_ = trim;
  }

  void set_network_ignore_list(
      const std::vector<std::string>& network_ignore_list) {
    network_manager_.set_network_ignore_list(network_ignore_list);
  }

  // Signal handlers for BasicNetworkManager
  virtual void OnNetworksChanged();

  // Signal handlers for TransportChannelImpl
  virtual void OnRequestSignaling(cricket::Transport* transport);
  virtual void OnRWChangeState(cricket::Transport* transport);
  virtual void OnCandidatesReady(cricket::Transport* transport,
                                 const cricket::Candidates& candidates);
  virtual void OnCandidatesAllocationDone(cricket::Transport* transport);
  virtual void OnReadPacket(cricket::TransportChannel* channel, 
                            const char* data, size_t len,
                            const talk_base::PacketTime& ptime, int flags);

  // Inherited from MessageHandler
  virtual void OnMessage(talk_base::Message* msg);

  // Signal handler for PeerSignalSenderInterface
  virtual void HandlePeer(const std::string& uid, const std::string& data,
                          const std::string& type);

  // Signal handler for PacketSenderInterface
  virtual void HandlePacket(talk_base::AsyncPacketSocket* socket,
      const char* data, size_t len, const talk_base::SocketAddress& addr);

  // Other public functions
  virtual void Setup(
      const std::string& uid, const std::string& ip4, int ip4_mask,
      const std::string& ip6, int ip6_mask, int subnet_mask, int switchmode);

  virtual bool CreateTransport(
      const std::string& uid, const std::string& fingerprint, int overlay_id,
      const std::string& stun_server, const std::string& turn_server,
      const std::string& turn_user, const std::string& turn_pass,
      bool sec_enabled);

  bool CreateConnections(const std::string& uid, 
                         const std::string& candidates_string);

  virtual bool AddIPMapping(const std::string& uid_key,
                            const std::string& ip4,
                            const std::string& ip6);

  virtual bool DestroyTransport(const std::string& uid);

  virtual Json::Value GetState(const std::map<std::string, uint32>& friends,
                               bool get_stats);

  static int DoPacketSend(const char* buf, size_t len);

  static int DoPacketRecv(char* buf, size_t len);

  static int SendToTap(const char* buf, size_t len);

  typedef cricket::DtlsTransport<cricket::P2PTransport> DtlsP2PTransport;

  struct PeerState {
    int overlay_id;
    uint32 last_time;
    std::string uid;
    std::string fingerprint;
    std::string connection_security;
    talk_base::scoped_ptr<cricket::P2PTransport> transport;
    talk_base::scoped_ptr<cricket::BasicPortAllocator> port_allocator;
    talk_base::scoped_ptr<talk_base::SSLFingerprint> remote_fingerprint;
    talk_base::scoped_ptr<cricket::TransportDescription> local_description;
    talk_base::scoped_ptr<cricket::TransportDescription> remote_description;
    cricket::P2PTransportChannel* channel;
    cricket::Candidates candidates;
    std::set<std::string> candidate_list;
    ~PeerState() {
      transport.reset();
      port_allocator.reset();
    }
  };

  struct PeerIPs {
    std::string ip4;
    std::string ip6;
  };

  typedef talk_base::scoped_refptr<
      talk_base::RefCountedObject<PeerState> > PeerStatePtr;

 private:
  void HandleConnectionSignal(cricket::Port* port,
                              cricket::Connection* connection);
  void SetupTransport(PeerState* peer_state);
  void HandleQueueSignal_w();
  void HandleControllerSignal_w();
  void InsertTransportMap_w(const std::string sub_uid,
                            cricket::Transport* transport);
  void DeleteTransportMap_w(const std::string sub_uid);
  Json::Value StateToJson(const std::string& uid, uint32 xmpp_time,
                          bool get_stats);
  bool SetRelay(PeerState* peer_state, const std::string& turn_server,
                const std::string& username, const std::string& password);
  void GetChannelStats_w(const std::string &uid,
                         cricket::ConnectionInfos *infos);
  bool is_icc(const unsigned char * buf);

  const std::string content_name_;
  PeerSignalSenderInterface* signal_sender_;
  talk_base::BasicPacketSocketFactory packet_factory_;
  std::map<std::string, PeerStatePtr> uid_map_;
  std::map<std::string, cricket::Transport*> short_uid_map_;
  std::map<cricket::Transport*, std::string> transport_map_;
  std::map<std::string, PeerIPs> ip_map_;
  talk_base::Thread* link_setup_thread_;
  talk_base::Thread* packet_handling_thread_;
  talk_base::BasicNetworkManager network_manager_;
  std::string tincan_id_;
  talk_base::scoped_ptr<talk_base::SSLIdentity> identity_;
  talk_base::scoped_ptr<talk_base::SSLFingerprint> local_fingerprint_;
  std::string fingerprint_;
  const uint64 tiebreaker_;
  std::string tincan_ip4_;
  std::string tincan_ip6_;
  std::string tap_name_;
  talk_base::AsyncPacketSocket* forward_socket_;
  talk_base::SocketAddress forward_addr_;
  talk_base::PacketOptions packet_options_;
  bool trim_enabled_;
  thread_opts_t* opts_;
};

}  // namespace tincan

#endif  // TINCAN_CONNECTIONMANAGER_H_

