
import socket
import select
import json
import time
import sys
import os
import binascii

# TODO - Detect local ip addresses
IP4 = "172.31.0.100"
IP6 = "fd50:0dbc:41f2:4a3c:0000:0000:0000:0000"
STUN = "209.141.33.252:19302"
LOCALHOST6= "::1"
SVPN_PORT = 5800
CONTROLLER_PORT = 5801
MODE = "svpn"
SEC = True

def gen_ip4(uid, count, ip4=IP4):
    return ip4[:-3] + str( 101 + count)

def gen_ip6(uid, ip6=IP6):
    uid_key = uid[-18:]
    count = (len(ip6) - 7) / 2
    ip6 = ip6[:19]
    for i in range(0, 16, 4):
        ip6 += ":"
        ip6 += uid_key[i:i+4]
    return ip6

def make_call(sock, params):
    data = json.dumps(params)
    dest = (LOCALHOST6, SVPN_PORT)
    return sock.sendto(data, dest)

def do_set_callback(sock, addr):
    params = {"m": "set_callback", "ip": addr[0], "port": addr[1]}
    return make_call(sock, params)

def do_register_service(sock, username, password, host):
    params = {"m": "register_service", "u": username, "p": password, "h": host}
    return make_call(sock, params)

def do_create_link(sock, uid, fpr, nid, sec, cas, stun=STUN):
    params = {"m": "create_link", "uid": uid, "fpr": fpr, "nid": nid,
              "stun" : stun, "turn": stun, "sec": sec, "cas": cas}
    return make_call(sock, params)

def do_trim_link(sock, uid):
    params = {"m": "trim_link", "uid": uid}
    return make_call(sock, params)

def do_set_local_ip(sock, uid, ip4, ip6, ip4_mask=24, ip6_mask=64):
    params = {"m": "set_local_ip", "uid": uid, "ip4": ip4, "ip6": ip6,
              "ip4_mask": ip4_mask, "ip6_mask": ip6_mask}
    return make_call(sock, params)

def do_set_remote_ip(sock, uid, ip4, ip6):
    params = {"m": "set_remote_ip", "uid": uid, "ip4": ip4, "ip6": ip6}
    return make_call(sock, params)

def do_send_msg(sock, nid, uid, data):
    params = {"m": "send_msg", "nid": nid, "uid": uid, "data": data}
    return make_call(sock, params)

def do_get_state(sock):
    params = {"m": "get_state"}
    return make_call(sock, params)

class UdpServer:

    def __init__(self, user, password, host, ip4):
        self.user = user
        self.password = password
        self.host = host
        self.ip4 = ip4
        self.sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        self.sock.bind(("", CONTROLLER_PORT))
        self.state = {}
        self.discover_peers = {}
        self.connect_times = {}

    def setup_svpn(self):
        uid = binascii.b2a_hex(os.urandom(9))
        if self.ip4 == None: self.ip4 = IP4
        do_set_callback(self.sock, self.sock.getsockname())
        do_set_local_ip(self.sock, uid, self.ip4, gen_ip6(uid))
        do_register_service(self.sock, self.user, self.password, self.host)
        do_get_state(self.sock)

    def set_state(self, state):
        self.state = state
        if len(state["_uid"]) == 0: self.setup_svpn()
        for k, v in self.state.get("peers", {}).iteritems():
            if k not in self.connect_times:
                self.connect_times[k] = time.time()

    def create_connection(self, uid, data, nid, sec, cas, ip4=None):
        if uid != self.state["_uid"]:
            if MODE == "gvpn" and ip4 == None:
                ip4 = ""
                do_send_msg(self.sock, 1, uid, "ip4:" + self.state["_ip4"])
            elif MODE == "svpn" and ip4 == None:
                ip4 = gen_ip4(uid, len(self.state["peers"]))

            ip6 = gen_ip6(uid)
            do_create_link(self.sock, uid, data, nid, sec, cas)
            do_set_remote_ip(self.sock, uid, ip4, ip6)
            do_get_state(self.sock)

            if uid not in self.connect_times:
                self.connect_times[uid] = time.time()

    def trim_connections(self):
        for k, v in self.state.get("peers", {}).iteritems():
            if "fpr" in v and v["status"] == "offline":
                time_diff = time.time() - self.connect_times[k]
                if time_diff > 300: do_trim_link(self.sock, k)

    def social_pings(self):
        for k, v in self.state.get("peers", {}).iteritems():
            do_send_msg(self.sock, 1, k, self.state["_fpr"])

    def lookup(self, ip4=None, uid=None):
        peers = self.discover_peers
        if uid != None: peers[uid] = self.state["peers"][uid]
        for k, v in peers.iteritems():
            if v["status"] == "online":
                request = {"m": "lookup", "ip4": ip4}
                ip6 = gen_ip6(k)
                dest = (ip6, CONTROLLER_PORT)
                self.sock.sendto(json.dumps(request), dest)

    def process_lookup(self, request, addr):
        for k, v in self.state.get("peers", {}).iteritems():
            ip4 = request.get("ip4", None)
            if ip4 != None and ip4 != v["ip4"]: continue

            if v["status"] == "online":
                response = {"uid": k}
                response["data"] = v["fpr"]
                if ip4 == v["ip4"]: response["ip4"] = ip4
                self.sock.sendto(json.dumps(response), addr)

    def route_notification(self, msg, addr):
        if addr[0] == LOCALHOST6:
            msg["from"] = self.state["_uid"]
            msg["ip4"] = self.state["_ip4"]
            for k, v in self.state.get("peers", {}).iteritems():
                if v["status"] == "online":
                    ip6 = gen_ip6(k, IP6)
                    dest = (ip6, CONTROLLER_PORT, 0, 0)
                    self.sock.sendto(json.dumps(msg), dest)
        elif msg["uid"] in self.state.get("peers", {}):
            peer = self.state["peers"][msg["uid"]]
            if peer["status"] == "online":
                ip6 = gen_ip6(msg["uid"])
                dest = (ip6, CONTROLLER_PORT, 0, 0)
                self.sock.sendto(json.dumps(msg), dest)

    def serve(self):
        msg = None
        socks = select.select([self.sock], [], [], 15)
        for sock in socks[0]:
            data, addr = sock.recvfrom(4096)
            print "recv %s %s" % (addr, data)
            if data[0] == '{': msg = json.loads(data)

            if isinstance(msg, dict) and "_uid" in msg:
                self.set_state(msg)
                continue

            # we only process if we have state and msg is json dict
            if len(self.state["_fpr"]) == 0 or not isinstance(msg, dict):
                continue

            if msg.get("m", None) == "lookup":
                self.process_lookup(msg, addr)
                continue

            if msg.get("m", None) == "nc_lookup":
                self.lookup(msg["ip"], msg["uid"])
                continue

            ip4 = msg.get("ip4", None)
            fpr_len = len(self.state["_fpr"])
            # TODO - fpr_len should not be used for decision making

            # this is a peer discovery notification
            if "data" in msg and len(msg["data"]) == fpr_len:
                if addr[0] == LOCALHOST6:
                    self.create_connection(msg["uid"], msg["data"], 1, SEC,
                                           "", ip4)
                else:
                    self.create_connection(msg["uid"], msg["data"], 0, SEC,
                                           "", ip4)
            # this is a connection request notification
            elif "data" in msg and len(msg["data"]) > fpr_len:
                fpr = msg["data"][:fpr_len]
                cas = msg["data"][fpr_len + 1:]
                # this came from social network
                if addr[0] == LOCALHOST6 and fpr != self.state["_fpr"]:
                    self.create_connection(msg["uid"], fpr, 1, SEC, cas, ip4)
                # this came from another controller
                elif msg["uid"] == self.state["_uid"] and "from" in msg:
                    self.create_connection(msg["from"], fpr, 0, SEC, cas, ip4)
                else:
                    self.route_notification(msg, addr)
            # this is an ip address update
            elif "data" in msg and msg["data"].startswith("ip4:"):
                ip4 = msg["data"][4:]
                ip6 = gen_ip6(msg["uid"])
                do_set_remote_ip(self.sock, msg["uid"], ip4, ip6)

def main():
    ip4 = IP4
    if len(sys.argv) < 4:
        print "usage: %s username password host [ip]" % (sys.argv[0],)

    if len(sys.argv) > 4:
        ip4 = sys.argv[4]
        global MODE
        MODE = "gvpn"

    server = UdpServer(sys.argv[1], sys.argv[2], sys.argv[3], ip4)
    do_get_state(server.sock)
    last_time = time.time()
    count = 0

    while True:
        server.serve()
        time_diff = time.time() - last_time
        if time_diff > 30:
            count += 1
            server.trim_connections()
            do_get_state(server.sock)
            last_time = time.time()
            if count % 2 == 0: server.social_pings()

if __name__ == '__main__':
    main()

