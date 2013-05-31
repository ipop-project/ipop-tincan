
#ifndef SJINGLE_XMPPNETWORK_H_
#define SJINGLE_XMPPNETWORK_H_
#pragma once

#include <string>

#include "talk/xmpp/xmpptask.h"
#include "talk/xmpp/xmppengine.h"
#include "talk/xmpp/presencestatus.h"
#include "talk/xmpp/presencereceivetask.h"
#include "talk/xmpp/presenceouttask.h"
#include "talk/xmpp/xmppclient.h"
#include "talk/xmpp/xmppsocket.h"
#include "talk/xmpp/xmpppump.h"
#include "talk/base/sigslot.h"

namespace sjingle {

const char kXmppPrefix[] = "svpn";
const int kResourceSize = 10;

class SocialNetworkSenderInterface {
 public:
  // Slot for message callbacks
  sigslot::signal2<const std::string&, const std::string&> HandlePeer;

  virtual const std::string uid() = 0;
  virtual void SendToPeer(const std::string& uid, const std::string& sdp) = 0;

 protected:
  virtual ~SocialNetworkSenderInterface() {}
};

class SvpnTask
    : public SocialNetworkSenderInterface,
      public buzz::XmppTask {
 public:
  explicit SvpnTask(buzz::XmppClient* client)
      : XmppTask(client, buzz::XmppEngine::HL_SINGLE) {}

  // inherited from SocialSenderInterface
  virtual const std::string uid() { return GetClient()->jid().Str(); }

  virtual void SendToPeer(const std::string& uid, const std::string& data);

 protected:
  virtual int ProcessStart();
  virtual bool HandleStanza(const buzz::XmlElement* stanza);
};

class XmppNetwork 
    : public SocialNetworkSenderInterface,
      public sigslot::has_slots<> {
 public:
  explicit XmppNetwork(buzz::XmppClientSettings& xcs)
      : xcs_(xcs) { Init(); }

  // Slot for message callbacks
  sigslot::signal2<const std::string&, const std::string&> HandlePeer;

  // inherited from SocialSenderInterface
  virtual const std::string uid() { return svpn_task_->uid(); }

  virtual void SendToPeer(const std::string& uid, const std::string& data) {
    svpn_task_->SendToPeer(uid, data);
  }

  virtual void set_status(const std::string& status) {
    my_status_->set_status(status);
  }

 private:
  buzz::XmppClientSettings& xcs_;
  buzz::XmppPump* pump_;
  buzz::XmppSocket* xmpp_socket_;
  buzz::PresenceStatus* my_status_;
  buzz::PresenceReceiveTask* presence_receive_;
  buzz::PresenceOutTask* presence_out_;
  SvpnTask* svpn_task_;

  void Init();
  void OnSignOn();
  void OnStateChange(buzz::XmppEngine::State state);
  void OnPresenceMessage(const buzz::PresenceStatus &status);
  void OnCloseEvent(int error);

};

}  // namespace sjingle

#endif  // SJINGLE_XMPPNETWORK_H_
