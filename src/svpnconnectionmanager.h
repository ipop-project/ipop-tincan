
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
#include "talk/base/scoped_ref_ptr.h"
#include "talk/base/refcount.h"

#include "talk/socialvpn/svpn-core/lib/threadqueue/threadqueue.h"
#include "talk/socialvpn/svpn-core/src/svpn.h"
#include "talk/socialvpn/svpn-core/src/tap.h"
#include "talk/socialvpn/svpn-core/src/peerlist.h"
#include "talk/socialvpn/svpn-core/src/packetio.h"

#include "xmppnetwork.h"

namespace sjingle {

class SvpnConnectionManager : public talk_base::MessageHandler,
                              public sigslot::has_slots<> {

 public:
  SvpnConnectionManager(SocialNetworkSenderInterface* social_sender,
                        talk_base::Thread* signaling_thread,
                        talk_base::Thread* worker_thread,
                        struct threadqueue* send_queue,
                        struct threadqueue* rcv_queue);

  const std::string fingerprint() const { return fingerprint_; }

  const std::string uid() const { return svpn_id_; }

  const std::string ipv4() const { return svpn_ip_; }

  const std::string ipv6() const { return svpn_ip6_; }

  const std::string tap_name() const { return tap_name_; }

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
  virtual void OnRWChangeState(cricket::Transport* transport);
  virtual void OnCandidatesReady(cricket::Transport* transport,
                                const cricket::Candidates& candidates);
  virtual void OnCandidatesAllocationDone(cricket::Transport* transport);
  virtual void OnReadPacket(cricket::TransportChannel* channel, 
                            const char* data, size_t len, int flags);

  virtual std::string GetState();

  // Signal fired when packet inserted in recv_queue
  static void HandleQueueSignal(struct threadqueue* queue);

  typedef cricket::DtlsTransport<cricket::P2PTransport> DtlsP2PTransport;

  struct PeerState {
    std::string uid;
    std::string fingerprint;
    talk_base::scoped_ptr<DtlsP2PTransport> transport;
    talk_base::scoped_ptr<cricket::BasicPortAllocator> port_allocator;
    talk_base::scoped_ptr<talk_base::SSLFingerprint> remote_fingerprint;
    talk_base::scoped_ptr<cricket::TransportDescription> local_description;
    talk_base::scoped_ptr<cricket::TransportDescription> remote_description;
    cricket::Candidates candidates;
    std::set<std::string> candidate_list;
    uint32 last_ping_time;
  };

  typedef talk_base::scoped_refptr<
      talk_base::RefCountedObject<PeerState> > PeerStatePtr;

 private:
  void AddIP(const std::string& uid_key);
  void SetupTransport(PeerState* peer_state);
  void CreateTransport(const std::string& uid, 
                       const std::string& fingerprint);
  void CreateConnections(const std::string& uid, 
                         const std::string& candidates_string);
  void DestroyTransport(const std::string& uid);
  void HandleQueueSignal_w(struct threadqueue* queue);
  void HandleCheck_s();
  void HandlePing_w();
  void UpdateTime(const char* data, size_t len);

  std::string get_key(const std::string& uid) {
    int idx = uid.find('/') + sizeof(kXmppPrefix);
    if ((idx + kIdSize) <= uid.size()) {
      return uid.substr(idx, kIdSize);
    }
    return uid;
  }

  const std::string content_name_;
  SocialNetworkSenderInterface* social_sender_;
  talk_base::BasicPacketSocketFactory packet_factory_;
  std::map<std::string, PeerStatePtr> uid_map_;
  std::map<cricket::Transport*, std::string> transport_map_;
  std::map<std::string, int> ip_map_;
  talk_base::Thread* signaling_thread_;
  talk_base::Thread* worker_thread_;
  talk_base::SocketAddress stun_server_;
  talk_base::BasicNetworkManager network_manager_;
  std::string svpn_id_;
  talk_base::OpenSSLIdentity* identity_;
  talk_base::SSLFingerprint* local_fingerprint_;
  std::string fingerprint_;
  struct threadqueue* send_queue_;
  struct threadqueue* rcv_queue_;
  uint64 tiebreaker_;
  uint32 check_counter_;
  std::string svpn_ip_;
  std::string svpn_ip6_;
  std::string tap_name_;
};

}  // namespace sjingle

#endif  // SJINGLE_SVPNCONNECTIONMANAGER_H_

