#!/usr/bin/env python

import socket, select, json, time, sys, hashlib

IP6_PREFIX = "fd50:0dbc:41f2:4a3c"
STUN = "209.141.33.252:19302"
TURN = ""
LOCALHOST6= "::1"
SVPN_PORT = 5800
CONTROLLER_PORT = 5801
UID_SIZE = 40
SEC = True
WAIT_TIME = 30

def gen_ip6(uid, ip6=IP6_PREFIX):
    for i in range(0, 16, 4): ip6 += ":" + uid[i:i+4]
    return ip6

def gen_uid(ip4):
    return hashlib.sha1(ip4).hexdigest()[:UID_SIZE]

def get_ip4(uid, ip4):
    parts = ip4.split(".")
    ip4 = parts[0] + "." + parts[1] + "." + parts[2] + "."
    for i in range(1, 255):
        if uid == gen_uid(ip4 + str(i)): return ip4 + str(i)
    return None

def make_call(sock, **params):
    return sock.sendto(json.dumps(params), (LOCALHOST6, SVPN_PORT))

def do_set_callback(sock, addr):
    return make_call(sock, m="set_callback", ip=addr[0], port=addr[1])

def do_register_service(sock, username, password, host):
    return make_call(sock, m="register_service", username=username,
                     password=password, host=host)

def do_create_link(sock, uid, fpr, nid, sec, cas, stun=STUN, turn=TURN):
    return make_call(sock, m="create_link", uid=uid, fpr=fpr, nid=nid,
                     stun=stun, turn=turn, turn_user="svpnjingle",
                     turn_pass="1234567890", sec=sec, cas=cas)

def do_trim_link(sock, uid):
    return make_call(sock, m="trim_link", uid=uid)

def do_set_local_ip(sock, uid, ip4, ip6, ip4_mask=24, ip6_mask=64):
    return make_call(sock, m="set_local_ip", uid=uid, ip4=ip4, ip6=ip6,
                     ip4_mask=ip4_mask, ip6_mask=ip6_mask)

def do_set_remote_ip(sock, uid, ip4, ip6):
    return make_call(sock, m="set_remote_ip", uid=uid, ip4=ip4, ip6=ip6)

def do_get_state(sock):
    return make_call(sock, m="get_state")

class UdpServer:
    def __init__(self, user, password, host, ip4):
        self.state = {}
        self.sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        self.sock.bind(("", CONTROLLER_PORT))
        uid = gen_uid(ip4)
        do_set_callback(self.sock, self.sock.getsockname())
        do_set_local_ip(self.sock, uid, ip4, gen_ip6(uid))
        do_register_service(self.sock, user, password, host)
        do_get_state(self.sock)

    def create_connection(self, uid, data, nid, sec, cas, ip4):
        do_create_link(self.sock, uid, data, nid, sec, cas)
        do_set_remote_ip(self.sock, uid, ip4, gen_ip6(uid))

    def trim_connections(self):
        for k, v in self.state.get("peers", {}).iteritems():
            if "fpr" in v and v["status"] == "offline":
                if v["last_time"] > WAIT_TIME * 4: do_trim_link(self.sock, k)

    def serve(self):
        socks = select.select([self.sock], [], [], WAIT_TIME)
        for sock in socks[0]:
            data, addr = sock.recvfrom(4096)
            if data[0] == '{':
                msg = json.loads(data)
                print "recv %s %s" % (addr, data)
                if "_fpr" in msg: self.state = msg; continue
                fpr_len = len(self.state["_fpr"])
                if "data" in msg and len(msg["data"]) >= fpr_len:
                    fpr = msg["data"][:fpr_len]
                    cas = msg["data"][fpr_len + 1:]
                    ip4 = get_ip4(msg["uid"], self.state["_ip4"])
                    self.create_connection(msg["uid"], fpr, 1, SEC, cas, ip4)

def main():
    if len(sys.argv) < 5:
        print "usage: %s username password host ip" % (sys.argv[0],)
        return

    count = 0
    server = UdpServer(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
    last_time = time.time()
    while True:
        server.serve()
        time_diff = time.time() - last_time
        if time_diff > WAIT_TIME:
            count += 1
            server.trim_connections()
            do_get_state(server.sock)
            last_time = time.time()

if __name__ == '__main__':
    main()

