/*
 * ipop-tincan
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

#include "tincan_utils.h"
#include "xmppnetwork.h"

namespace tincan {

static const int kXmppPort = 5222;
static const int kInterval = 15000;

// kPresenceInterval is kInterval * 8 = 120 secs meaning that
// presence message is sent to XMPP server every 2 minutes
static const int kPresenceInterval = 8;

static const buzz::StaticQName QN_TINCAN = { "jabber:iq:tincan", "query" };
static const buzz::StaticQName QN_TINCAN_DATA = { "jabber:iq:tincan", "data" };
static const buzz::StaticQName QN_TINCAN_TYPE = { "jabber:iq:tincan", "type" };
static const char kTemplate[] = "<query xmlns=\"jabber:iq:tincan\" />";
static const char kErrorMsg[] = "error";

static const int kPingPeriod = 15000;
// should be half of ping period
static const int kPingTimeout = 7500;

// TODO - we should not be storing in global map, need to move to a class
static std::map<std::string, std::string> g_uid_map;

static std::string get_key(const std::string& uid) {
  size_t idx = uid.find('/') + sizeof(kXmppPrefix);
  if ((idx + kIdSize) <= uid.size()) {
    return uid.substr(idx, kIdSize);
  }
  return uid;
}

TinCanTask::TinCanTask(buzz::XmppClient* client,
                       PeerHandlerInterface* handler)
  : XmppTask(client, buzz::XmppEngine::HL_TYPE),
    handler_(handler) {
}

void TinCanTask::SendToPeer(int overlay_id, const std::string &uid,
                            const std::string &data,
                            const std::string &type) {
  if (g_uid_map.find(uid) == g_uid_map.end()) return;
  const buzz::Jid to(g_uid_map[uid]);
  talk_base::scoped_ptr<buzz::XmlElement> get(
      MakeIq(buzz::STR_GET, to, task_id()));
  // TODO - Figure out how to build from QN_TINCAN instead of template
  std::string templ(kTemplate);
  buzz::XmlElement* element = buzz::XmlElement::ForStr(templ);
  //buzz::XmlElement* element = new buzz::XmlElement(QN_TINCAN);
  buzz::XmlElement* data_xe = new buzz::XmlElement(QN_TINCAN_DATA);
  buzz::XmlElement* type_xe = new buzz::XmlElement(QN_TINCAN_TYPE);

  data_xe->SetBodyText(data);
  type_xe->SetBodyText(type);
  element->AddElement(data_xe);
  element->AddElement(type_xe);

  get->AddElement(element);
  SendStanza(get.get());
  LOG_TS(INFO) << "XMPP SEND uid " << uid << " data " << data
               << " type " << type;
}

int TinCanTask::ProcessStart() {
  const buzz::XmlElement* stanza = NextStanza();
  if (stanza == NULL) {
    return STATE_BLOCKED;
  }

  buzz::Jid from(stanza->Attr(buzz::QN_FROM));
  if (from.resource().compare(0, sizeof(kXmppPrefix)-1 , kXmppPrefix) == 0 &&
      from != GetClient()->jid()) {
    std::string uid = stanza->Attr(buzz::QN_FROM);
    std::string uid_key = get_key(uid);
    // map each uid to a uid_key
    g_uid_map[uid_key] = uid;

    const buzz::XmlElement* msg = stanza->FirstNamed(QN_TINCAN);
    if (msg != NULL) {
      const buzz::XmlElement* xml_data = msg->FirstNamed(QN_TINCAN_DATA);
      const buzz::XmlElement* xml_type = msg->FirstNamed(QN_TINCAN_TYPE);
      std::string data, type;
      if (xml_data != NULL) {
        data = xml_data->BodyText();
      }
      if (xml_data != NULL) {
        type= xml_type->BodyText();
      }
      handler_->DoHandlePeer(uid_key, data, type);
    }
  }
  return STATE_START;
}

bool TinCanTask::HandleStanza(const buzz::XmlElement* stanza) {
  if (!MatchRequestIq(stanza, buzz::STR_GET, QN_TINCAN)) {
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
  //xcs_.set_allow_plain(true);
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
  pump_->client()->SignalDisconnected.connect(this,
      &XmppNetwork::OnTimeout);

  xmpp_state_ = buzz::XmppEngine::STATE_NONE;
  pump_->DoLogin(xcs_, xmpp_socket_.get(), 0);
  LOG_TS(INFO) << "XMPP CONNECTING";
  main_thread_->Clear(this);
  main_thread_->PostDelayed(kInterval, this, 0, 0);
  on_msg_counter_ = 0;
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

  tincan_task_.reset(new TinCanTask(pump_->client(), this));

  ping_task_.reset(new buzz::PingTask(pump_->client(), main_thread_,
                                      kPingPeriod, kPingTimeout));
  ping_task_->SignalTimeout.connect(this, &XmppNetwork::OnTimeout);

  presence_receive_->Start();
  presence_out_->Send(status_);
  presence_out_->Start();
  ping_task_->Start();
  tincan_task_->Start();
  LOG_TS(INFO) << "XMPP ONLINE " << pump_->client()->jid().Str();
}

void XmppNetwork::OnStateChange(buzz::XmppEngine::State state) {
  xmpp_state_ = state;
  switch (state) {
    case buzz::XmppEngine::STATE_START:
      LOG_TS(INFO) << "START";
      break;
    case buzz::XmppEngine::STATE_OPENING:
      LOG_TS(INFO) << "OPENING";
      break;
    case buzz::XmppEngine::STATE_OPEN:
      LOG_TS(INFO) << "OPEN";
      OnSignOn();
      break;
    case buzz::XmppEngine::STATE_CLOSED:
      LOG_TS(INFO) << "CLOSED";
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

    // Simply update the timestamp of this presence message by uid
    presence_time_[uid_key] = talk_base::Time();
    g_uid_map[uid_key] = uid;

    LOG_TS(INFO) << "uid " << uid << " status " << status.status();
  }
}

void XmppNetwork::OnCloseEvent(int error) {
  LOG_TS(INFO) << "ONCLOSEEVENT " << error;
}

void XmppNetwork::OnTimeout() {
  LOG_TS(INFO) << "ONTIMEOUT";
}

void XmppNetwork::OnMessage(talk_base::Message* msg) {
  if (pump_.get()) {
    if (xmpp_state_ == buzz::XmppEngine::STATE_START ||
        xmpp_state_ == buzz::XmppEngine::STATE_OPENING) {
      pump_->DoDisconnect();
    }
    else if (pump_->client()->AnyChildError() &&
             xmpp_state_ != buzz::XmppEngine::STATE_CLOSED) {
      pump_->DoDisconnect();
    }
    else if (xmpp_state_ == buzz::XmppEngine::STATE_NONE) {
      xmpp_socket_.release();
      Connect();
    }
    else if (xmpp_state_ == buzz::XmppEngine::STATE_CLOSED) {
      xmpp_socket_.release();
      presence_receive_.release();
      presence_out_.release();
      tincan_task_.release();
      ping_task_.release();
      //pump_.release();
      Connect();
    }
    else if (xmpp_state_ == buzz::XmppEngine::STATE_OPEN &&
             on_msg_counter_++ % kPresenceInterval == 0) {
      // Resend presence every 2 min necessary for reconnections
      presence_out_.release();
      presence_out_.reset(new buzz::PresenceOutTask(pump_->client()));
      presence_out_->Send(status_);
      presence_out_->Start();
    }

  }
  main_thread_->PostDelayed(kInterval, this, 0, 0);
}

}  // namespace tincan

