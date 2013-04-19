
#ifndef SJINGLE_SVPNCONNECTIONOBSERVER_H_
#define SJINGLE_SVPNCONNECTIONOBSERVER_H_
#pragma once

#include <iostream>

#include "talk/app/webrtc/peerconnectioninterface.h"
#include "talk/app/webrtc/datachannelinterface.h"
#include "talk/app/webrtc/jsep.h"

namespace sjingle {

class IceNotifierInterface {
 public:
  virtual void Notify(const std::string& uid) = 0;
};

class SvpnDataChannelObserver : public webrtc::DataChannelObserver {
 public:
  virtual void OnStateChange() {};
  virtual void OnMessage(const webrtc::DataBuffer& buffer) {
    std::cout << "=============DEBUG==============" << std::endl;
    std::string msg(buffer.data.data());
    std::cout << "=====================RECV " << msg << std::endl;
  };
};

class SvpnConnectionObserver : public webrtc::PeerConnectionObserver {
 public:
  SvpnConnectionObserver(const std::string& uid, 
                         IceNotifierInterface* notifier);

  // inherited from PeerConnectionObserver
  virtual void OnError();
  virtual void OnAddStream(webrtc::MediaStreamInterface* stream);
  virtual void OnRemoveStream(webrtc::MediaStreamInterface* stream);
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate);
  virtual void OnIceComplete();

 protected:
  const std::string uid_;
  IceNotifierInterface* notifier_;
};

class SvpnSetSessionDescriptionObserver
  : public webrtc::SetSessionDescriptionObserver {
 public:
  SvpnSetSessionDescriptionObserver();

  // inherited from SetSessionDescriptionObserver
  virtual void OnSuccess();
  virtual void OnFailure(const std::string &error);
};

class SvpnCreateSessionDescriptionObserver
  : public webrtc::CreateSessionDescriptionObserver {
 public:
  SvpnCreateSessionDescriptionObserver(
      webrtc::PeerConnectionInterface* peer_connection,
      SvpnSetSessionDescriptionObserver* set_observer);

  // inherited from CreateSessionDescriptionObserver
  virtual void OnSuccess(webrtc::SessionDescriptionInterface* desc);
  virtual void OnFailure(const std::string &error);

 protected:
  webrtc::PeerConnectionInterface* peer_connection_;
  SvpnSetSessionDescriptionObserver* set_observer_;
};

}  // namespace sjingle

#endif  // SJINGLE_SVPNCONNECTIONOBSERVER_H_

