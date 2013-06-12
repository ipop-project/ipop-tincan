
#include <string>

#include "talk/base/thread.h"
#include "talk/xmpp/constants.h"
#include "talk/xmpp/xmppclientsettings.h"
#include "talk/xmpp/xmppthread.h"
#include "talk/xmpp/xmpppump.h"
#include "talk/xmpp/jid.h"
#include "talk/base/logging.h"

#include "xmppnetwork.h"

namespace sjingle {

static const int kXmppPort = 5222;

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
  if (from.resource().compare(0, sizeof(kXmppPrefix)-1 , kXmppPrefix) == 0 &&
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

void XmppNetwork::Login(std::string username, std::string password,
                        std::string pcid, std::string host) {
  if (online_) return;
  talk_base::InsecureCryptStringImpl pass;
  pass.password() = password;
  std::string resource(kXmppPrefix);
  resource += pcid;
  buzz::Jid jid(username);
  xcs_.set_user(jid.node());
  xcs_.set_host(jid.domain());
  xcs_.set_resource(resource);
  xcs_.set_use_tls(buzz::TLS_REQUIRED);
  xcs_.set_pass(talk_base::CryptString(pass));
  xcs_.set_server(talk_base::SocketAddress(host, kXmppPort));
  Connect();
}

void XmppNetwork::Connect() {
  state_.reset(new XmppState);
  state_->xmpp_socket.reset(new buzz::XmppSocket(buzz::TLS_REQUIRED));
  state_->xmpp_socket->SignalCloseEvent.connect(this, 
      &XmppNetwork::OnCloseEvent);

  state_->pump.reset(new buzz::XmppPump());
  state_->pump->DoLogin(xcs_, state_->xmpp_socket.get(), 0);
  state_->pump->client()->SignalStateChange.connect(this, 
      &XmppNetwork::OnStateChange);

  status_.set_jid(state_->pump->client()->jid());
  status_.set_available(true);
  status_.set_show(buzz::PresenceStatus::SHOW_ONLINE);
  status_.set_priority(0);

  state_->presence_receive.reset(
      new buzz::PresenceReceiveTask(state_->pump->client()));
  state_->presence_receive->PresenceUpdate.connect(this,
      &XmppNetwork::OnPresenceMessage);

  state_->presence_out.reset(
      new buzz::PresenceOutTask(state_->pump->client()));
  state_->svpn_task.reset(new SvpnTask(state_->pump->client()));
  LOG(INFO) << __FUNCTION__ << " XMPP CONNECTING ";
}

void XmppNetwork::OnSignOn() {
  state_->presence_receive->Start();
  state_->presence_out->Send(status_);
  state_->presence_out->Start();
  state_->svpn_task->HandlePeer = HandlePeer;
  state_->svpn_task->Start();
  online_ = true;
  LOG(INFO) << "XMPP ONLINE " << state_->pump->client()->jid().Str();
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
      status.jid() != state_->pump->client()->jid()) {
    state_->svpn_task->HandlePeer(status.jid().Str(), status.status());
  }
}

void XmppNetwork::OnCloseEvent(int error) {
  // We release bc they are assumed to be deleted elsewhere, need to verify|
  state_->pump.release();
  state_->xmpp_socket.release();
  state_->presence_receive.release();
  state_->presence_out.release();
  state_->svpn_task.release();
  online_ = false;
  Connect();
}

}  // namespace sjingle

