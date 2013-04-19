
#ifndef SJINGLE_SVPNCONNECTIONMANAGER_H_
#define SJINGLE_SVPNCONNECTIONMANAGER_H_
#pragma once

#include <string>
#include <map>

#include "talk/app/webrtc/jsep.h"
#include "talk/base/refcount.h"
#include "talk/base/scoped_ref_ptr.h"
#include "talk/app/webrtc/test/fakeconstraints.h"
#include "talk/base/sigslot.h"
#include "talk/app/webrtc/datachannelinterface.h"

#include "svpnconnectionobserver.h"
#include "xmppnetwork.h"

namespace sjingle {

class SvpnConnectionManager
    : public IceNotifierInterface,
      public sigslot::has_slots<> {

 public:
  explicit SvpnConnectionManager(SocialNetworkSenderInterface* social_sender);
  virtual void CreateConnection(const std::string& uid,
                                webrtc::SessionDescriptionInterface* desc);
  virtual void DeleteConnection(const std::string& uid);

  // will be used as callback for xmppnetwork
  virtual void HandlePeer(const std::string& uid, const std::string& data);

  // inherited from IceNotifierInterface
  virtual void Notify(const std::string& uid);

 private:
  typedef talk_base::RefCountedObject<SvpnDataChannelObserver>
            RefSvpnDataChannelObserver;
  typedef talk_base::RefCountedObject<SvpnConnectionObserver>
            RefSvpnConnectionObserver;
  typedef talk_base::RefCountedObject<SvpnSetSessionDescriptionObserver>
            RefSvpnSetSdpObserver;
  typedef talk_base::RefCountedObject<SvpnCreateSessionDescriptionObserver>
            RefSvpnCreateSdpObserver;

  typedef struct PeerConnectionState {
    talk_base::scoped_refptr<webrtc::PeerConnectionInterface> connection;
    talk_base::scoped_refptr<webrtc::DataChannelInterface> channel;
    talk_base::scoped_refptr<RefSvpnDataChannelObserver> data_observer;
    talk_base::scoped_refptr<RefSvpnConnectionObserver> observer;
    talk_base::scoped_refptr<RefSvpnSetSdpObserver> set_observer;
    talk_base::scoped_refptr<RefSvpnCreateSdpObserver> sdp_observer;
    std::string sdp;
  } PeerConnectionState;

  typedef talk_base::RefCountedObject<PeerConnectionState> RefConnectionState;
  typedef std::map<const std::string, 
                   talk_base::scoped_refptr<RefConnectionState> >
                   PeerConnectionMap;

  SocialNetworkSenderInterface* social_sender_;
  webrtc::FakeConstraints constraints_;
  webrtc::PeerConnectionInterface::IceServer server_;
  webrtc::PeerConnectionInterface::IceServers servers_;
  talk_base::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  talk_base::scoped_refptr<webrtc::MediaStreamInterface> stream_;
  talk_base::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
      peer_connection_factory_;
  PeerConnectionMap connections_;
};

}  // namespace sjingle

#endif  // SJINGLE_SVPNCONNECTIONOBSERVER_H_

