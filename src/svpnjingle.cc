
#include <iostream>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#include "talk/base/ssladapter.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/host.h"

#include "svpnconnectionmanager.h"

static const int kXmppPort = 5222;

class SendRunnable : public talk_base::Runnable {
 public:
  SendRunnable(thread_opts_t *opts) : opts_(opts) {}

  virtual void Run(talk_base::Thread *thread) {
    udp_send_thread(opts_);
  }

 private:
  thread_opts_t *opts_;
};

class RecvRunnable : public talk_base::Runnable {
 public:
  RecvRunnable(thread_opts_t *opts) : opts_(opts) {}

  virtual void Run(talk_base::Thread *thread) {
    udp_recv_thread(opts_);
  }

 private:
  thread_opts_t *opts_;
};

// essential parts of svpn-core svpn.c main function
int setup_svpn(thread_opts_t *opts, char *tap_device_name, char *ipv4_addr, 
               char *ipv6_addr, const char *client_id) {
  opts->tap = tap_open(tap_device_name, opts->mac);
  opts->local_ip4 = ipv4_addr;
  //opts->local_ip6 = ipv6_addr;

  // configure the tap device
  tap_set_ipv4_addr(ipv4_addr, 24);
  tap_set_ipv6_addr(ipv6_addr, 64);
  tap_set_mtu(MTU);
  tap_set_base_flags();
  tap_set_up();

  peerlist_init(TABLE_SIZE);
  peerlist_set_local_p(client_id, ipv4_addr, ipv6_addr);

  // drop root privileges and set to nobody
  struct passwd * pwd = getpwnam("nobody");
  if (getuid() == 0) {
    if (setgid(pwd->pw_uid) < 0) {
      fprintf(stderr, "setgid failed\n");
      tap_close();
      return -1;
    }
   if (setuid(pwd->pw_gid) < 0) {
      fprintf(stderr, "setuid failed\n");
      tap_close();
      return -1;
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  //talk_base::LogMessage::LogToDebug(talk_base::LS_INFO);
  talk_base::InitializeSSL();

  std::cout << "User Name: ";
  std::string username;
  std::cin >> username;

  std::cout << "Password: ";
  std::string password;
  std::cin >> password;

  std::cout << "Xmpp Host: ";
  std::string host;
  std::cin >> host;

  std::string tmp_uid = talk_base::CreateRandomString(sjingle::kResourceSize/2);
  std::string uid = talk_base::hex_encode(tmp_uid.c_str(), tmp_uid.size());
  std::cout << "\nUID " << uid << std::endl;

  struct threadqueue send_queue, rcv_queue;
  thread_queue_init(&send_queue);
  thread_queue_init(&rcv_queue);

  char tap_name[] = "svpn0";
  char ipv4[] = "172.31.0.100";
  char ipv6[] = "fd50:0dbc:41f2:4a3c:b683:19a7:63b4:f736";
  thread_opts_t opts;
  opts.send_queue = &send_queue;
  opts.rcv_queue = &rcv_queue;
  opts.send_signal = &sjingle::SvpnConnectionManager::HandleQueueSignal;
  setup_svpn(&opts, tap_name, ipv4, ipv6, uid.c_str());

  SendRunnable send_runnable(&opts);
  RecvRunnable recv_runnable(&opts);
  talk_base::Thread send_thread, recv_thread;
  send_thread.Start(&send_runnable);
  recv_thread.Start(&recv_runnable);

  talk_base::InsecureCryptStringImpl pass;
  pass.password() = password;

  std::string resource(sjingle::kXmppPrefix);
  buzz::Jid jid(username);
  buzz::XmppClientSettings xcs;
  xcs.set_user(jid.node());
  xcs.set_host(jid.domain());
  xcs.set_resource(resource + uid);
  xcs.set_use_tls(buzz::TLS_REQUIRED);
  xcs.set_pass(talk_base::CryptString(pass));
  xcs.set_server(talk_base::SocketAddress(host, kXmppPort));
  talk_base::AutoThread signaling_thread;
  talk_base::Thread worker_thread;
  signaling_thread.WrapCurrent();
  worker_thread.Start();

  sjingle::XmppNetwork network(xcs);
  sjingle::SvpnConnectionManager manager(&network, &signaling_thread,
                                         &worker_thread, &send_queue, 
                                         &rcv_queue, uid);

  network.set_status(manager.fingerprint());
  network.HandlePeer.connect(&manager,
      &sjingle::SvpnConnectionManager::HandlePeer);
  signaling_thread.Run();
  return 0;
}


