
#include <algorithm>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>

#include "talk/base/ssladapter.h"

#include "svpnconnectionmanager.h"
#include "httpui.h"

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
int setup_svpn(thread_opts_t *opts, const char *tap_device_name,
               const char *ipv4_addr, const char *ipv6_addr, 
               const char *client_id) {
  opts->tap = tap_open(tap_device_name, opts->mac);
  opts->local_ip4 = ipv4_addr;
  opts->local_ip6 = ipv6_addr;

  // configure the tap device
  tap_set_ipv4_addr(ipv4_addr, 24);
  tap_set_ipv6_addr(ipv6_addr, 64);
  tap_set_mtu(MTU);
  tap_set_base_flags();
  tap_set_up();

  peerlist_init(TABLE_SIZE);
  peerlist_set_local_p(client_id, ipv4_addr, ipv6_addr);

#ifndef DROID_BUILD
  // drop root privileges and set to nobody, causes Android issues
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
#endif
  return 0;
}

int main(int argc, char **argv) {

  if (argc > 1) {
    if (strncmp(argv[1], "-v", 2) == 0) {
      talk_base::LogMessage::LogToDebug(talk_base::LS_INFO);
    }
  }

  talk_base::InitializeSSL();

  struct threadqueue send_queue, rcv_queue;
  thread_queue_init(&send_queue);
  thread_queue_init(&rcv_queue);

  talk_base::Thread worker_thread, send_thread, recv_thread;
  talk_base::AutoThread signaling_thread;
  signaling_thread.WrapCurrent();

  sjingle::XmppNetwork network;
  sjingle::SvpnConnectionManager manager(&network, &signaling_thread,
                                         &worker_thread, &send_queue, 
                                         &rcv_queue);
  network.set_status(manager.fingerprint());
  network.HandlePeer.connect(&manager,
      &sjingle::SvpnConnectionManager::HandlePeer);
  sjingle::HttpUI httpui(manager, network);

  // TODO - Use BasicNetworkManager to determine available network
  thread_opts_t opts;
  opts.send_queue = &send_queue;
  opts.rcv_queue = &rcv_queue;
  opts.send_signal = &sjingle::SvpnConnectionManager::HandleQueueSignal;

  // need to make sure we get all handles because we become nobody
  setup_svpn(&opts, manager.tap_name().c_str(), manager.ipv4().c_str(), 
             manager.ipv6().c_str(), manager.uid().c_str());

  // Setup/run threads
  SendRunnable send_runnable(&opts);
  RecvRunnable recv_runnable(&opts);

  send_thread.Start(&send_runnable);
  recv_thread.Start(&recv_runnable);
  worker_thread.Start();
  signaling_thread.Run();
  
  return 0;
}

