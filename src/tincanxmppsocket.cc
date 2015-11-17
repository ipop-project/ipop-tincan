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

#include "tincanxmppsocket.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include "talk/base/basicdefs.h"
#include "talk/base/logging.h"
#include "talk/base/thread.h"
#ifdef FEATURE_ENABLE_SSL
#include "talk/base/ssladapter.h"
#include "talk/base/schanneladapter.h"
#endif

namespace tincan {

TinCanXmppSocket::TinCanXmppSocket(buzz::TlsOptions tls) : cricket_socket_(NULL),
                                               tls_(tls) {
  state_ = buzz::AsyncSocket::STATE_CLOSED;
}

void TinCanXmppSocket::CreateCricketSocket(int family) {
  talk_base::Thread* pth = talk_base::Thread::Current();
  if (family == AF_UNSPEC) {
    family = AF_INET;
  }
  talk_base::AsyncSocket* socket =
      pth->socketserver()->CreateAsyncSocket(family, SOCK_STREAM);
#ifdef FEATURE_ENABLE_SSL
  if (tls_ != buzz::TLS_DISABLED) {
#ifdef WIN32
    socket = new talk_base::SChannelAdapter(socket);
#else
    socket = talk_base::SSLAdapter::Create(socket);
#endif
  }
#endif  // FEATURE_ENABLE_SSL
  cricket_socket_ = socket;
  cricket_socket_->SignalReadEvent.connect(this, &TinCanXmppSocket::OnReadEvent);
  cricket_socket_->SignalWriteEvent.connect(this, &TinCanXmppSocket::OnWriteEvent);
  cricket_socket_->SignalConnectEvent.connect(this,
                                              &TinCanXmppSocket::OnConnectEvent);
  cricket_socket_->SignalCloseEvent.connect(this, &TinCanXmppSocket::OnCloseEvent);
}

TinCanXmppSocket::~TinCanXmppSocket() {
  Close();
  delete cricket_socket_;
}

void TinCanXmppSocket::OnReadEvent(talk_base::AsyncSocket * socket) {
  SignalRead();
}

void TinCanXmppSocket::OnWriteEvent(talk_base::AsyncSocket * socket) {
  // Write bytes if there are any
  while (buffer_.Length() != 0) {
    int written = cricket_socket_->Send(buffer_.Data(), buffer_.Length());
    if (written > 0) {
      buffer_.Consume(written);
      continue;
    }
    if (!cricket_socket_->IsBlocking())
      LOG(LS_ERROR) << "Send error: " << cricket_socket_->GetError();
    return;
  }
}

void TinCanXmppSocket::OnConnectEvent(talk_base::AsyncSocket * socket) {
#if defined(FEATURE_ENABLE_SSL)
  if (state_ == buzz::AsyncSocket::STATE_TLS_CONNECTING) {
    state_ = buzz::AsyncSocket::STATE_TLS_OPEN;
    SignalSSLConnected();
    OnWriteEvent(cricket_socket_);
    return;
  }
#endif  // !defined(FEATURE_ENABLE_SSL)
  state_ = buzz::AsyncSocket::STATE_OPEN;
  SignalConnected();
}

void TinCanXmppSocket::OnCloseEvent(talk_base::AsyncSocket * socket, int error) {
  SignalCloseEvent(error);
}

buzz::AsyncSocket::State TinCanXmppSocket::state() {
  return state_;
}

buzz::AsyncSocket::Error TinCanXmppSocket::error() {
  return buzz::AsyncSocket::ERROR_NONE;
}

int TinCanXmppSocket::GetError() {
  return 0;
}

bool TinCanXmppSocket::Connect(const talk_base::SocketAddress& addr) {
  if (cricket_socket_ == NULL) {
    CreateCricketSocket(addr.family());
  }
  if (cricket_socket_->Connect(addr) < 0) {
    return cricket_socket_->IsBlocking();
  }
  return true;
}

bool TinCanXmppSocket::Read(char * data, size_t len, size_t* len_read) {
  int read = cricket_socket_->Recv(data, len);
  if (read > 0) {
    *len_read = (size_t)read;
    return true;
  }
  return false;
}

bool TinCanXmppSocket::Write(const char * data, size_t len) {
  buffer_.WriteBytes(data, len);
  OnWriteEvent(cricket_socket_);
  return true;
}

bool TinCanXmppSocket::Close() {
  if (state_ != buzz::AsyncSocket::STATE_OPEN)
    return false;
  if (cricket_socket_->Close() == 0) {
    state_ = buzz::AsyncSocket::STATE_CLOSED;
    SignalClosed();
    return true;
  }
  return false;
}

bool TinCanXmppSocket::StartTls(const std::string & domainname) {
#if defined(FEATURE_ENABLE_SSL)
  if (tls_ == buzz::TLS_DISABLED)
    return false;
  talk_base::SSLAdapter* ssl_adapter =
    static_cast<talk_base::SSLAdapter *>(cricket_socket_);
  if (ssl_adapter->StartSSL(domainname.c_str(), false) != 0)
    return false;
  state_ = buzz::AsyncSocket::STATE_TLS_CONNECTING;
  return true;
#else  // !defined(FEATURE_ENABLE_SSL)
  return false;
#endif  // !defined(FEATURE_ENABLE_SSL)
}

}  // namespace buzz

