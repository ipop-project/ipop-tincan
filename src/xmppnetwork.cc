/*
 * ipop-tincan
 * Copyright 2015, University of Florida
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
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

// this timeout sets how frequent XMPP server is pinged (ms)
static const int kPingPeriod = 15000;
// this timeout sets how long to wait for XMPP server response (ms)
static const int kPingTimeout = 5000;
// this constant sets how often XMPP state is checked by OnMessage (ms)
static const int kInterval = 15000;
// this constant sets how often presense message is sent (sec)
static const int kPresenceInterval = 120;

static const int kXmppPort = 5222;

static const buzz::StaticQName QN_TINCAN = { "jabber:iq:tincan", "query" };
static const buzz::StaticQName QN_TINCAN_DATA = { "jabber:iq:tincan", "data" };
static const buzz::StaticQName QN_TINCAN_TYPE = { "jabber:iq:tincan", "type" };
static const char kTemplate[] = "<query xmlns=\"jabber:iq:tincan\" />";
static const char kErrorMsg[] = "error";

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
    else {
      // Assuming this is a presence message therefore update time
      handler_->SetTime(uid_key, talk_base::Time());
    }
  }
  return STATE_START;
}

bool TinCanTask::HandleStanza(const buzz::XmlElement* stanza) {
  if (MatchRequestIq(stanza, buzz::STR_GET, QN_TINCAN) ||
       stanza->Name() == buzz::QN_PRESENCE) {
    QueueStanza(stanza);
    return true;
  }

  return false;
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
  xmpp_socket_.reset(new TinCanXmppSocket(buzz::TLS_REQUIRED));
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

  presence_out_.reset(new buzz::PresenceOutTask(pump_->client()));

  tincan_task_.reset(new TinCanTask(pump_->client(), this));

  ping_task_.reset(new buzz::PingTask(pump_->client(), main_thread_,
                                      kPingPeriod, kPingTimeout));
  ping_task_->SignalTimeout.connect(this, &XmppNetwork::OnTimeout);

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
      presence_out_.release();
      tincan_task_.release();
      ping_task_.release();
      //pump_.release();
      Connect();
    }
    else if (xmpp_state_ == buzz::XmppEngine::STATE_OPEN &&
             on_msg_counter_ % kPresenceInterval == 0) {
      // Resend presence every 2 min necessary for reconnections
      presence_out_.release();
      presence_out_.reset(new buzz::PresenceOutTask(pump_->client()));
      presence_out_->Send(status_);
      presence_out_->Start();
    }

  }
  main_thread_->PostDelayed(kInterval, this, 0, 0);
  on_msg_counter_ += kInterval/1000;
}

}  // namespace tincan

