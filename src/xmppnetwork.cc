/*
 * tincan-jingle
 * Copyright 2013, University of Florida
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string>

#include "talk/base/thread.h"
#include "talk/xmpp/constants.h"
#include "talk/xmpp/xmppclientsettings.h"
#include "talk/xmpp/xmppthread.h"
#include "talk/xmpp/xmpppump.h"
#include "talk/xmpp/jid.h"
#include "talk/xmpp/constants.h"

#include "xmppnetwork.h"

namespace sjingle {

static const int kXmppPort = 5222;
static const int kInterval = 120000;
static const buzz::StaticQName QN_SVPN = { "jabber:iq:tincan", "query" };
static const char kTemplate[] = "<query xmlns=\"jabber:iq:tincan\" />";

static std::string get_key(const std::string& uid) {
  int idx = uid.find('/') + sizeof(kXmppPrefix);
  if ((idx + kIdSize) <= uid.size()) {
    return uid.substr(idx, kIdSize);
  }
  return uid;
}


void SvpnTask::SendToPeer(int nid, const std::string &uid,
                          const std::string &data) {
  const buzz::Jid to(get_xmpp_id(uid));
  talk_base::scoped_ptr<buzz::XmlElement> get(
      MakeIq(buzz::STR_GET, to, task_id()));
  // TODO - Figure out how to build from QN_SVPN instead of template
  std::string templ(kTemplate);
  buzz::XmlElement* element = buzz::XmlElement::ForStr(templ);
  //buzz::XmlElement* element = new buzz::XmlElement(QN_SVPN);
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
    std::string uid = stanza->Attr(buzz::QN_FROM);
    std::string uid_key = get_key(uid);
    set_xmpp_id(uid_key, uid);
    HandlePeer(uid_key, stanza->FirstNamed(QN_SVPN)->BodyText());
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

bool XmppNetwork::Login(std::string username, std::string password,
                        std::string pcid, std::string host) {
  if (pump_.get() || username.empty() || password.empty() || 
      pcid.empty() || host.empty()) return false;

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
  return Connect();
}

bool XmppNetwork::Connect() {
  xmpp_socket_.reset(new buzz::XmppSocket(buzz::TLS_REQUIRED));
  xmpp_socket_->SignalCloseEvent.connect(this, &XmppNetwork::OnCloseEvent);

  pump_.reset(new buzz::XmppPump());
  pump_->client()->SignalLogInput.connect(this, &XmppNetwork::OnLogging);
  pump_->client()->SignalLogOutput.connect(this, &XmppNetwork::OnLogging);
  pump_->client()->SignalStateChange.connect(this, 
      &XmppNetwork::OnStateChange);
  pump_->DoLogin(xcs_, xmpp_socket_.get(), 0);
  LOG_F(INFO) << "XMPP CONNECTING";
  return true;
}

void XmppNetwork::OnSignOn() {
  if (!pump_.get()) return;
  uid_ = pump_->client()->jid().Str();
  status_.set_jid(pump_->client()->jid());
  status_.set_available(true);
  status_.set_show(buzz::PresenceStatus::SHOW_ONLINE);
  status_.set_priority(0);

  presence_receive_.reset(new buzz::PresenceReceiveTask(pump_->client()));
  presence_receive_->PresenceUpdate.connect(this,
      &XmppNetwork::OnPresenceMessage);

  presence_out_.reset(new buzz::PresenceOutTask(pump_->client()));
  tincan_task_.reset(new SvpnTask(pump_->client()));

  presence_receive_->Start();
  presence_out_->Send(status_);
  presence_out_->Start();
  tincan_task_->HandlePeer = HandlePeer;
  tincan_task_->Start();
  main_thread_->Clear(this);
  main_thread_->PostDelayed(kInterval, this, 0, 0);
  LOG_F(INFO) << "XMPP ONLINE " << pump_->client()->jid().Str();
}

void XmppNetwork::OnStateChange(buzz::XmppEngine::State state) {
  switch (state) {
    case buzz::XmppEngine::STATE_START:
      LOG_F(INFO) << "START";
      break;
    case buzz::XmppEngine::STATE_OPENING:
      LOG_F(INFO) << "OPENING";
      break;
    case buzz::XmppEngine::STATE_OPEN:
      LOG_F(INFO) << "OPEN";
      OnSignOn();
      break;
    case buzz::XmppEngine::STATE_CLOSED:
      LOG_F(INFO) << "CLOSED";
      OnCloseEvent(0);
      break;
  }
}

void XmppNetwork::OnPresenceMessage(const buzz::PresenceStatus &status) {
  if (!pump_.get() || !tincan_task_.get()) return;
  if (status.jid().resource().size() > (sizeof(kXmppPrefix) - 1) && 
      status.jid().resource().compare(0, sizeof(kXmppPrefix) - 1, 
      kXmppPrefix) == 0 && status.jid() != pump_->client()->jid()) {
    std::string uid = status.jid().Str();
    std::string uid_key = get_key(uid);
    tincan_task_->set_xmpp_id(uid_key, uid);
    tincan_task_->HandlePeer(uid_key, status.status());
  }
}

void XmppNetwork::OnCloseEvent(int error) {
  // Release all assuming they are deleted by XmppClient
  xmpp_socket_.release();
  presence_receive_.release();
  presence_out_.release();
  tincan_task_.release();
  pump_.release();
  LOG_F(INFO) << "XMPP CLOSE " << error;
}

void XmppNetwork::OnMessage(talk_base::Message* msg) {
  // This handles reconnection if there's a connection timeout
  if (!pump_.get()) {
    Connect();
  }
  else {
    presence_out_.release();
    presence_out_.reset(new buzz::PresenceOutTask(pump_->client()));
    presence_out_->Send(status_);
    presence_out_->Start();
  }
  main_thread_->PostDelayed(kInterval, this, 0, 0);
}

}  // namespace sjingle

