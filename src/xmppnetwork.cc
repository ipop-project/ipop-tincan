
#include <string>
#include <iostream>

#include "talk/base/thread.h"
#include "talk/xmpp/constants.h"
#include "talk/xmpp/xmppclientsettings.h"
#include "talk/xmpp/xmppthread.h"
#include "talk/xmpp/xmpppump.h"
#include "talk/xmpp/jid.h"
#include "talk/base/logging.h"

#include "xmppnetwork.h"

namespace sjingle {

static const buzz::StaticQName QN_SVPN = { "svpn:webrtc", "data" };

void SvpnTask::SendToPeer(const std::string &uid, const std::string &data) {
  const buzz::Jid to(uid);
  talk_base::scoped_ptr<buzz::XmlElement> get(
      MakeIq(buzz::STR_GET, to, task_id()));
  buzz::XmlElement* element = new buzz::XmlElement(QN_SVPN);
  element->SetBodyText(data);
  get->AddElement(element);
  SendStanza(get.get());
}

int SvpnTask::ProcessStart() {
  const buzz::XmlElement* stanza = NextStanza();
  if (stanza == NULL) {
    return STATE_BLOCKED;
  }

  buzz::Jid from(stanza->Attr(buzz::QN_FROM));
  if (from.resource().compare(0, 4, kXmppPrefix) == 0 &&
      from != GetClient()->jid()) {
    HandlePeer(stanza->Attr(buzz::QN_FROM),
               stanza->FirstNamed(QN_SVPN)->BodyText());
  }
  return STATE_START;
}

bool SvpnTask::HandleStanza(const buzz::XmlElement* stanza) {
  if (!MatchRequestIq(stanza, buzz::STR_GET, QN_SVPN)) {
    return false;
  }
  QueueStanza(stanza);
  return true;
}

void XmppNetwork::Init() {
  xmpp_socket_ = new buzz::XmppSocket(buzz::TLS_REQUIRED);
  xmpp_socket_->SignalCloseEvent.connect(this, 
      &XmppNetwork::OnCloseEvent);

  pump_ = new buzz::XmppPump();
  pump_->DoLogin(xcs_, xmpp_socket_, 0);
  pump_->client()->SignalStateChange.connect(this, 
      &XmppNetwork::OnStateChange);

  my_status_ = new buzz::PresenceStatus();
  my_status_->set_jid(pump_->client()->jid());
  my_status_->set_available(true);
  my_status_->set_show(buzz::PresenceStatus::SHOW_ONLINE);
  my_status_->set_priority(0);

  presence_receive_ = new buzz::PresenceReceiveTask(pump_->client());
  presence_receive_->PresenceUpdate.connect(this,
      &XmppNetwork::OnPresenceMessage);

  presence_out_ = new buzz::PresenceOutTask(pump_->client());
  svpn_task_ = new SvpnTask(pump_->client());
  LOG(INFO) << __FUNCTION__ << " XMPP CONNECTING ";
}

void XmppNetwork::OnSignOn() {
  presence_receive_->Start();
  presence_out_->Send(*my_status_);
  presence_out_->Start();
  svpn_task_->HandlePeer = HandlePeer;
  svpn_task_->Start();
}

void XmppNetwork::OnStateChange(buzz::XmppEngine::State state) {
  switch (state) {
    case buzz::XmppEngine::STATE_START:
      LOG(INFO) << __FUNCTION__ << " START";
      break;
    case buzz::XmppEngine::STATE_OPENING:
      LOG(INFO) << __FUNCTION__ << " OPENING";
      break;
    case buzz::XmppEngine::STATE_OPEN:
      LOG(INFO) << __FUNCTION__ << " OPEN";
      OnSignOn();
      break;
    case buzz::XmppEngine::STATE_CLOSED:
      LOG(INFO) << __FUNCTION__ << " CLOSED";
      break;
  }
}

void XmppNetwork::OnPresenceMessage(const buzz::PresenceStatus &status) {
  if (status.jid().resource().compare(0, 4, kXmppPrefix) == 0 && 
      status.jid() != pump_->client()->jid()) {
    svpn_task_->HandlePeer(status.jid().Str(), status.status());
  }
}

void XmppNetwork::OnCloseEvent(int error) {
  // TODO - need to figure out proper way to kill pump and client
  //pump_->DoDisconnect();
  //delete pump_;
  Init();
}

}  // namespace sjingle

