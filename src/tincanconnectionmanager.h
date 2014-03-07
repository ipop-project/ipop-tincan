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
                          talk_base::Thread* packet_handling_thread);

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

  // Signal handlers for BasicNetworkManager
  virtual void OnNetworksChanged();

  // Signal handlers for TransportChannelImpl
  virtual void OnRequestSignaling(cricket::Transport* transport);
  virtual void OnRWChangeState(cricket::Transport* transport);
  virtual void OnCandidatesReady(cricket::Transport* transport,
                                 const cricket::Candidates& candidates);
  virtual void OnCandidatesAllocationDone(cricket::Transport* transport);
  virtual void OnReadPacket(cricket::TransportChannel* channel, 
                            const char* data, size_t len, int flags);

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
      const std::string& ip6, int ip6_mask, int subnet_mask);

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
    cricket::Candidates candidates;
    std::set<std::string> candidate_list;
  };

  struct PeerIPs {
    std::string ip4;
    std::string ip6;
  };

  typedef talk_base::scoped_refptr<
      talk_base::RefCountedObject<PeerState> > PeerStatePtr;

 private:
  void SetupTransport(PeerState* peer_state);
  void HandleQueueSignal_w();
  void HandleControllerSignal_w();
  Json::Value StateToJson(const std::string& uid, uint32 xmpp_time,
                          bool get_stats);
  bool SetRelay(PeerState* peer_state, const std::string& turn_server,
                const std::string& username, const std::string& password);

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
};

}  // namespace tincan

#endif  // TINCAN_CONNECTIONMANAGER_H_

