
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

const std::string SvpnTask::uid() {
  return GetClient()->jid().Str();
}

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

XmppNetwork::XmppNetwork(buzz::XmppClient *client)
                         : client_(client),
                           presence_receive_(client),
                           presence_out_(client),
                           svpn_task_(client) {
  my_status_.set_jid(client->jid());
  my_status_.set_available(true);
  my_status_.set_show(buzz::PresenceStatus::SHOW_ONLINE);
  my_status_.set_priority(0);
  client_->SignalStateChange.connect(this, &XmppNetwork::OnStateChange);
  presence_receive_.PresenceUpdate.connect(this,
      &XmppNetwork::OnPresenceMessage);
}

void XmppNetwork::OnSignOn() {
  presence_receive_.Start();
  presence_out_.Send(my_status_);
  presence_out_.Start();
  svpn_task_.Start();
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
      LOG(INFO) << __FUNCTION__ << " " << client_->jid().Str() << " "
                << my_status_.status();
      OnSignOn();
      break;
    case buzz::XmppEngine::STATE_CLOSED:
      LOG(INFO) << __FUNCTION__ << " CLOSED";
      break;
  }
}

void XmppNetwork::OnPresenceMessage(const buzz::PresenceStatus &status) {
  if (status.jid().resource().compare(0, 4, kXmppPrefix) == 0 && 
      status.jid() != client_->jid()) {
    svpn_task_.HandlePeer(status.jid().Str(), status.status());
  }
}

}  // namespace sjingle

