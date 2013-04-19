
#include <string>

#include "svpnconnectionobserver.h"

namespace sjingle {

SvpnConnectionObserver::SvpnConnectionObserver(const std::string& uid,
                                               IceNotifierInterface* notifier)
                                               : uid_(uid), 
                                                 notifier_(notifier) {
}

void SvpnConnectionObserver::OnError() {
}

void SvpnConnectionObserver::OnAddStream(
    webrtc::MediaStreamInterface* stream) {
}

void SvpnConnectionObserver::OnRemoveStream(
    webrtc::MediaStreamInterface* stream) {
}

void SvpnConnectionObserver::OnIceCandidate(
    const webrtc::IceCandidateInterface* candidate) {
}

void SvpnConnectionObserver::OnIceComplete() {
  notifier_->Notify(uid_);
}

//////////////////////////////////////////////////////////////////////////////
SvpnSetSessionDescriptionObserver::SvpnSetSessionDescriptionObserver() {
}

void SvpnSetSessionDescriptionObserver::OnSuccess() {
}

void SvpnSetSessionDescriptionObserver::OnFailure(
    const std::string &error) {
}

//////////////////////////////////////////////////////////////////////////////
SvpnCreateSessionDescriptionObserver::SvpnCreateSessionDescriptionObserver(
    webrtc::PeerConnectionInterface* peer_connection,
    SvpnSetSessionDescriptionObserver* set_observer)
    : peer_connection_(peer_connection),
      set_observer_(set_observer) {
}

void SvpnCreateSessionDescriptionObserver::OnSuccess(
    webrtc::SessionDescriptionInterface* desc) {
  peer_connection_->SetLocalDescription(set_observer_, desc);
}

void SvpnCreateSessionDescriptionObserver::OnFailure(
    const std::string &error) {
}

}  // namespace sjingle

