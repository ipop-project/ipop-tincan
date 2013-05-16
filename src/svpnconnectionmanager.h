
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

#include "talk/examples/svpn-core/lib/threadqueue/threadqueue.h"
#include "talk/examples/svpn-core/src/svpn.h"
#include "talk/examples/svpn-core/src/tap.h"
#include "talk/examples/svpn-core/src/peerlist.h"
#include "talk/examples/svpn-core/src/packetio.h"

#include "xmppnetwork.h"

namespace sjingle {

static const int kDigestSize = 64;

class SvpnConnectionManager : public talk_base::MessageHandler,
                              public sigslot::has_slots<> {

 public:
  SvpnConnectionManager(SocialNetworkSenderInterface* social_sender,
                        talk_base::Thread* signaling_thread,
                        talk_base::Thread* worker_thread,
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
  virtual void OnRequestSignaling(cricket::TransportChannelImpl* channel);
  virtual void OnCandidateReady(cricket::TransportChannelImpl* channel,
                                const cricket::Candidate& candidate);
  virtual void OnCandidatesAllocationDone(
      cricket::TransportChannelImpl* channel);
  virtual void OnRWChangeState(cricket::TransportChannel* channel);
  virtual void OnReadPacket(cricket::TransportChannel* channel, 
                            const char* data, size_t len, int flags);

  // Signal fired when packet inserted in recv_queue
  static void HandleQueueSignal(struct threadqueue* queue);

  typedef cricket::DtlsTransport<cricket::P2PTransport> DtlsP2PTransport;

  struct PeerState {
    std::string uid;
    DtlsP2PTransport* transport;
    uint32 creation_time;
    std::string fingerprint;
  };

 private:
  void AddIP(const std::string& uid_key);
  void SetupTransport(cricket::P2PTransport* transport, 
                      const std::string& uid, const std::string& fingerprint);
  void CreateTransport(const std::string& uid, 
                        const std::string& fingerprint);
  void CreateConnections(const std::string& uid, 
                        const std::string& candidates_string);
  void DestroyTransport_s(std::string& uid);
  void SetSocket_w();
  void HandleQueueSignal_w(struct threadqueue* queue);
  void HandleCheck_w();

  std::string get_key(const std::string& uid) {
    return uid.substr(uid.size() - kResourceSize);
  }

  const std::string content_name_;
  SocialNetworkSenderInterface* social_sender_;
  talk_base::AsyncPacketSocket* socket_;
  talk_base::BasicPacketSocketFactory packet_factory_;
  std::map<std::string, PeerState> uid_map_;
  std::map<cricket::TransportChannel*, PeerState> channel_map_;
  std::set<std::string> candidates_;
  talk_base::Thread* signaling_thread_;
  talk_base::Thread* worker_thread_;
  talk_base::SocketAddress stun_server_;
  talk_base::BasicNetworkManager network_manager_;
  cricket::BasicPortAllocator port_allocator_;
  talk_base::OpenSSLIdentity* identity_;
  talk_base::SSLFingerprint* local_fingerprint_;
  std::string fingerprint_;
  struct threadqueue* send_queue_;
  struct threadqueue* rcv_queue_;
  std::map<std::string, int> ip_map_;
};

}  // namespace sjingle

#endif  // SJINGLE_SVPNCONNECTIONMANAGER_H_

