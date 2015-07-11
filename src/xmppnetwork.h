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

#ifndef TINCAN_XMPPNETWORK_H_
#define TINCAN_XMPPNETWORK_H_
#pragma once

#include "talk/xmpp/xmpptask.h"
#include "talk/xmpp/xmppengine.h"
#include "talk/xmpp/presencestatus.h"
#include "talk/xmpp/presencereceivetask.h"
#include "talk/xmpp/presenceouttask.h"
#include "talk/xmpp/pingtask.h"
#include "talk/xmpp/xmppclient.h"
#include "talk/xmpp/xmpppump.h"
#include "talk/base/logging.h"

#include "tincanxmppsocket.h"
#include "peersignalsender.h"
#include "tincan_utils.h"

namespace tincan {

static const char kXmppPrefix[] = "tincan";

class PeerHandlerInterface {

 public:
  virtual void DoHandlePeer(std::string& uid, std::string& data, 
                            std::string& type) = 0;
  virtual void SetTime(std::string& uid, uint32) = 0;
};

class TinCanTask
    :  public buzz::XmppTask {
 public:
  explicit TinCanTask(buzz::XmppClient* client,
                      PeerHandlerInterface* handler);

  virtual void SendToPeer(int overlay_id, const std::string& uid,
                          const std::string& data, const std::string& type);

 protected:
  virtual int ProcessStart();
  virtual bool HandleStanza(const buzz::XmlElement* stanza);

 private:
  PeerHandlerInterface* handler_;
};

class XmppNetwork 
    : public PeerSignalSenderInterface,
      public PeerHandlerInterface,
      public talk_base::MessageHandler,
      public sigslot::has_slots<> {
 public:
  explicit XmppNetwork(talk_base::Thread* main_thread) 
      : main_thread_(main_thread){};

  // Slot for message callbacks
  sigslot::signal3<const std::string&, const std::string&,
                   const std::string&> HandlePeer;

  // inherited from PeerSignalSenderInterface
  virtual const std::string uid() { 
    return uid_;
  }

  virtual const std::map<std::string, uint32> friends() {
    return presence_time_;
  }

  // inherited from PeerHandler
  virtual void DoHandlePeer(std::string& uid, std::string& data,
                            std::string& type) {
    HandlePeer(uid, data, type);
  }
  
  virtual void SetTime(std::string& uid, uint32 xmpp_time) {
    presence_time_[uid] = xmpp_time;
  }

  virtual void SendToPeer(int overlay_id, const std::string& uid,
                          const std::string& data, const std::string& type) {
    if (xmpp_state_ == buzz::XmppEngine::STATE_OPEN && tincan_task_.get()) {
      tincan_task_->SendToPeer(overlay_id, uid, data, type);
    }
  }

  void OnLogging(const char* data, int len) {
    LOG_TS(LS_VERBOSE) << std::string(data, len);
  }

  virtual void OnMessage(talk_base::Message* msg);

  bool Login(std::string username, std::string password,
             std::string pcid, std::string host);

 private:
  bool Connect();
  void OnSignOn();
  void OnStateChange(buzz::XmppEngine::State state);
  void OnCloseEvent(int error);
  void OnTimeout();

  talk_base::Thread* main_thread_;
  buzz::XmppClientSettings xcs_;
  buzz::PresenceStatus status_;
  talk_base::scoped_ptr<buzz::XmppPump> pump_;
  talk_base::scoped_ptr<TinCanXmppSocket> xmpp_socket_;
  talk_base::scoped_ptr<buzz::PresenceOutTask> presence_out_;
  talk_base::scoped_ptr<buzz::PingTask> ping_task_;
  talk_base::scoped_ptr<TinCanTask> tincan_task_;
  std::map<std::string, uint32> presence_time_;
  buzz::XmppEngine::State xmpp_state_;
  int on_msg_counter_;
  std::string uid_;

};

}  // namespace tincan

#endif  // TINCAN_XMPPNETWORK_H_
