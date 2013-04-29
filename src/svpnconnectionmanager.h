
#ifndef SJINGLE_SVPNCONNECTIONMANAGER_H_
#define SJINGLE_SVPNCONNECTIONMANAGER_H_
#pragma once

#include <string>
#include <map>

#include "talk/base/sigslot.h"
#include "talk/p2p/base/p2ptransport.h"
#include "talk/p2p/client/basicportallocator.h"
#include "talk/p2p/base/transportdescription.h"
#include "talk/p2p/base/transportchannelimpl.h"

#include "xmppnetwork.h"

namespace sjingle {


class SvpnConnectionManager : public talk_base::MessageHandler,
                              public sigslot::has_slots<> {

 public:
  SvpnConnectionManager(SocialNetworkSenderInterface* social_sender,
                        talk_base::AsyncPacketSocket* socket,
                        talk_base::Thread* signaling_thread,
                        talk_base::Thread* worker_thread);

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
  virtual void OnRoleConflict(cricket::TransportChannelImpl* channel);
  virtual void OnReadableState(cricket::TransportChannel* channel);
  virtual void OnWritableState(cricket::TransportChannel* channel);
  virtual void OnReadPacket(cricket::TransportChannel* channel, 
                            const char* data, size_t len, int flags);
  virtual void OnRouteChange(cricket::TransportChannel* channel,
                             const cricket::Candidate& candidate);
  virtual void OnDestroyed(cricket::TransportChannel* channel);

  struct PeerState {
    int peer_idx;
    std::string uid;
    cricket::TransportChannelImpl* channel;
    cricket::Candidates candidates;
  };

 private:
  int peer_idx_;
  const std::string content_name_;
  SocialNetworkSenderInterface* social_sender_;
  talk_base::AsyncPacketSocket* socket_;
  std::map<std::string, PeerState> uid_map_;
  std::map<cricket::TransportChannel*, PeerState> channel_map_;
  cricket::Candidates candidates_;
  talk_base::SocketAddress stun_server_;
  talk_base::BasicNetworkManager network_manager_;
  cricket::BasicPortAllocator port_allocator_;
  cricket::P2PTransport transport_;
  cricket::TransportDescription transport_description_;

  void HandlePeer_w(const std::string& uid, const std::string& data);
  void HandlePacket_w(const char* data, size_t len,
                      const talk_base::SocketAddress& addr);
  void CreateConnection(const std::string& uid);
  void DeleteConnection(const std::string& uid);
  void AddPeerAddress(const std::string& uid, const std::string& addr_string);
};

}  // namespace sjingle

#endif  // SJINGLE_SVPNCONNECTIONMANAGER_H_

