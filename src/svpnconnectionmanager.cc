
#include <string>
#include <iostream>
#include <unistd.h>

#include "talk/base/ssladapter.h"
#include "talk/base/logging.h"
#include "talk/xmpp/xmppsocket.h"
#include "talk/xmpp/xmpppump.h"
#include "talk/base/json.h"

#include "svpnconnectionmanager.h"

namespace sjingle {

const char kStunServerUri[] = "stun:stun.l.google.com:19302";
const char kAudioLabel[] = "audio_label";
const char kStreamLabel[] = "stream_label";
const char kDataLabel[] = "data_label";

SvpnConnectionManager::SvpnConnectionManager(
    SocialNetworkSenderInterface* social_sender)
    : social_sender_(social_sender) {
  server_.uri = kStunServerUri;
  servers_.push_back(server_);
  peer_connection_factory_ = webrtc::CreatePeerConnectionFactory();
  constraints_.SetMandatoryReceiveAudio(true);
  constraints_.SetMandatoryReceiveVideo(false);
  constraints_.SetAllowRtpDataChannels();
  audio_track_ = peer_connection_factory_->CreateAudioTrack(kAudioLabel,
                                                            NULL);
  stream_ = peer_connection_factory_->CreateLocalMediaStream(kStreamLabel);
  stream_->AddTrack(audio_track_);
}

void SvpnConnectionManager::CreateConnection(const std::string& uid,
      webrtc::SessionDescriptionInterface* desc) {
  std::cout << "CREATE CONNECTION " << uid << std::endl;
  talk_base::scoped_refptr<RefSvpnConnectionObserver>
      observer(new RefSvpnConnectionObserver(uid, this));
  talk_base::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection =
      peer_connection_factory_->CreatePeerConnection(servers_, &constraints_,
                                                     observer.get());
  talk_base::scoped_refptr<RefSvpnSetSdpObserver>
      set_observer(new RefSvpnSetSdpObserver);
  talk_base::scoped_refptr<RefSvpnCreateSdpObserver>
      sdp_observer(new RefSvpnCreateSdpObserver(
                   peer_connection.get(), set_observer.get()));
  talk_base::scoped_refptr<RefSvpnDataChannelObserver> 
      data_observer(new RefSvpnDataChannelObserver);
  peer_connection->AddStream(stream_, &constraints_);

  if (desc == NULL) {
    peer_connection->CreateOffer(sdp_observer.get(), NULL);
  }
  else {
    peer_connection->SetRemoteDescription(set_observer, desc);
    peer_connection->CreateAnswer(sdp_observer.get(), NULL);
  }

  webrtc::DataChannelInit config;
  config.reliable = false;

  talk_base::scoped_refptr<RefConnectionState> state(new RefConnectionState);
  state->connection = peer_connection;
  state->channel = peer_connection->CreateDataChannel(kDataLabel, &config);
  state->channel->RegisterObserver(data_observer.get());
  state->data_observer = data_observer;
  state->observer = observer;
  state->sdp_observer = sdp_observer;
  state->set_observer = set_observer;
  connections_[uid] = state;
}

void SvpnConnectionManager::DeleteConnection(const std::string& uid) {
  connections_[uid]->connection->Close();
  //connections_.erase(uid);
}

void SvpnConnectionManager::Notify(const std::string& uid) {
  std::cout << "ICE COMPLETE" << std::endl;
  Json::FastWriter writer;
  Json::Value jdesc;
  const webrtc::SessionDescriptionInterface* desc =
      connections_[uid]->connection->local_description();
  jdesc["type"] = desc->type();
  std::string sdp;
  desc->ToString(&sdp);
  jdesc["sdp"] = sdp;
  connections_[uid]->sdp = writer.write(jdesc);
  social_sender_->SendToPeer(uid, connections_[uid]->sdp);
  std::cout << "SDP COMPLETE" << std::endl;
}

void SvpnConnectionManager::HandlePeer(const std::string& uid,
                                       const std::string& data) {
  std::cout << "HANDLE PEER\n" << data << std::endl;
  Json::Reader reader;
  Json::Value jdesc;
  if (connections_.find(uid) == connections_.end() && 
      data.compare("connect") == 0 &&
      social_sender_->uid().compare(uid) < 0) {
      CreateConnection(uid, NULL);
  }
  else if (reader.parse(data, jdesc)) {
    std::string type = jdesc["type"].asString();
    std::string sdp = jdesc["sdp"].asString();
    webrtc::SessionDescriptionInterface* desc = 
        webrtc::CreateSessionDescription(type, sdp);

    if (type.compare("offer") == 0 &&
        connections_.find(uid) == connections_.end()) {
      CreateConnection(uid, desc);
    }
    else if (type.compare("offer") == 0 &&
             connections_.find(uid) != connections_.end() &&
             social_sender_->uid().compare(uid) > 0) {
      //DeleteConnection(uid);
      CreateConnection(uid, desc);
    }
    else if (type.compare("answer") == 0) {
      connections_[uid]->connection->SetRemoteDescription(
          connections_[uid]->set_observer, desc);
      sleep(5);
      std::cout << "===================SENDING " << std::endl;
      webrtc::DataBuffer buffer("========HHHHHHHEEEEEEEEELLLLLLLLLL");
      connections_[uid]->channel->Send(buffer);
    }
    else {
      delete desc;
    }
  }
}

}  // namespace sjingle

int main(int argc, char **argcv) {
  talk_base::LogMessage::LogToDebug(talk_base::LS_INFO);
  talk_base::InitializeSSL();

  std::cout << "User Name: ";
  std::string username;
  std::getline(std::cin, username);

  std::cout << "Password: ";
  std::string password;
  std::getline(std::cin, password);

  talk_base::InsecureCryptStringImpl pass;
  pass.password() = password;

  std::string resource(sjingle::kXmppPrefix);
  buzz::Jid jid(username);
  buzz::XmppClientSettings xcs;
  xcs.set_user(jid.node());
  xcs.set_host(jid.domain());
  xcs.set_resource(resource + talk_base::CreateRandomString(10));
  xcs.set_use_tls(buzz::TLS_REQUIRED);
  xcs.set_pass(talk_base::CryptString(pass));
  xcs.set_server(talk_base::SocketAddress("talk.google.com", 5222));

  buzz::XmppPump pump;
  pump.DoLogin(xcs, new buzz::XmppSocket(buzz::TLS_REQUIRED), 0);
               //new buzz::XmppAuth());
  sjingle::XmppNetwork network(pump.client());
  sjingle::SvpnConnectionManager manager(network.sender());
  network.sender()->HandlePeer.connect(&manager,
      &sjingle::SvpnConnectionManager::HandlePeer);

  talk_base::Thread::Current()->Run();
  std::string input;
  std::cin >> input;
  return 0;
}

