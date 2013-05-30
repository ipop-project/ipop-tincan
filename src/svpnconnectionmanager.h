
#ifndef SJINGLE_SVPNCONNECTIONMANAGER_H_
#define SJINGLE_SVPNCONNECTIONMANAGER_H_
#pragma once

#include <string>
#include <map>
#include <set>

#include "talk/base/sigslot.h"
#include "talk/p2p/base/p2ptransport.h"
#include "talk/p2p/client/basicportallocator.h"
#include "talk/p2p/base/transportdescription.h"
#include "talk/p2p/base/transportchannelimpl.h"
#include "talk/base/opensslidentity.h"
#include "talk/p2p/base/dtlstransportchannel.h"
#include "talk/p2p/base/dtlstransport.h"
#include "talk/base/base64.h"
#include "talk/p2p/base/basicpacketsocketfactory.h"
#include "talk/base/asyncpacketsocket.h"
#include "talk/base/fakenetwork.h"

#include "talk/examples/svpn-core/lib/threadqueue/threadqueue.h"
#include "talk/examples/svpn-core/src/svpn.h"
#include "talk/examples/svpn-core/src/tap.h"
#include "talk/examples/svpn-core/src/peerlist.h"
#include "talk/examples/svpn-core/src/packetio.h"

#include "xmppnetwork.h"

namespace sjingle {

class SvpnConnectionManager : public talk_base::MessageHandler,
                              public sigslot::has_slots<> {

 public:
  SvpnConnectionManager(SocialNetworkSenderInterface* social_sender,
                        talk_base::Thread* signaling_thread,
                        talk_base::Thread* worker_thread,
                        talk_base::FakeNetworkManager* network_manager,
                        struct threadqueue* send_queue,
                        struct threadqueue* rcv_queue,
                        const std::string& uid);

  const std::string fingerprint() const {
    return fingerprint_;
  }

  talk_base::Thread* worker_thread() { return worker_thread_; }

  // Inherited from MessageHandler
  virtual void OnMessage(talk_base::Message* msg);

  // Signal handler for SocialNetworkSenderInterface
  virtual void HandlePeer(const std::string& uid, const std::string& data);

  // Signal handler for PacketSenderInterface
  virtual void HandlePacket(talk_base::AsyncPacketSocket* socket,
      const char* data, size_t len, const talk_base::SocketAddress& addr);

  // Signal handlers for TransportChannelImpl
  virtual void OnRequestSignaling(cricket::Transport* transport);
  virtual void OnRoleConflict(cricket::TransportChannelImpl* channel);
  virtual void OnCandidatesReady(cricket::Transport* transport,
                                const cricket::Candidates& candidates);
  virtual void OnCandidatesAllocationDone(cricket::Transport* transport);
  virtual void OnRWChangeState(cricket::Transport* transport);
  virtual void OnReadPacket(cricket::TransportChannel* channel, 
                            const char* data, size_t len, int flags);

  // Signal fired when packet inserted in recv_queue
  static void HandleQueueSignal(struct threadqueue* queue);

  typedef cricket::DtlsTransport<cricket::P2PTransport> DtlsP2PTransport;

  struct PeerState {
    std::string uid;
    DtlsP2PTransport* transport;
    cricket::BasicPortAllocator* port_allocator;
    cricket::Candidates candidates;
    uint32 creation_time;
    std::string fingerprint;
    int count;
  };

 private:
  void AddIP(const std::string& uid_key);
  void SetupTransport(cricket::P2PTransport* transport, 
                      const std::string& uid, const std::string& fingerprint);
  void CreateTransport(const std::string& uid, 
                        const std::string& fingerprint);
  void CreateConnections(const std::string& uid, 
                        const std::string& candidates_string);
  void DestroyTransport(const std::string& uid);
  void SetSocket_w();
  void HandleQueueSignal_w(struct threadqueue* queue);
  void HandleCheck_s();
  void HandlePing_w();
  void ProcessInput(const char* data, size_t len);

  std::string get_key(const std::string& uid) {
    return uid.substr(uid.size() - kResourceSize);
  }

  const std::string content_name_;
  SocialNetworkSenderInterface* social_sender_;
  talk_base::AsyncPacketSocket* socket_;
  talk_base::BasicPacketSocketFactory packet_factory_;
  std::map<std::string, PeerState> uid_map_;
  std::map<cricket::Transport*, PeerState> transport_map_;
  std::set<std::string> candidates_;
  talk_base::Thread* signaling_thread_;
  talk_base::Thread* worker_thread_;
  talk_base::SocketAddress stun_server_;
  talk_base::FakeNetworkManager* network_manager_;
  talk_base::OpenSSLIdentity* identity_;
  talk_base::SSLFingerprint* local_fingerprint_;
  std::string fingerprint_;
  struct threadqueue* send_queue_;
  struct threadqueue* rcv_queue_;
  std::map<std::string, int> ip_map_;
  uint64 tiebreaker_;
  uint32 last_connect_time_;
};

}  // namespace sjingle

#endif  // SJINGLE_SVPNCONNECTIONMANAGER_H_

