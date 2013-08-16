
import socket
import select
import json
import time
import sys
import os
import binascii

IP4 = "172.31.0.100"
IP6 = "fd50:0dbc:41f2:4a3c:0000:0000:0000:0000"
STUN = "209.141.33.252:19302"
LOCALHOST6= "::1"
ANY = "0.0.0.0"
SVPN_PORT = 5800
CONTROLLER_PORT = 5801

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
    print sock, data, dest
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

def do_ping(sock, nid, uid, fpr):
    params = {"m": "ping", "uid": uid, "fpr": fpr}
    return make_call(sock, params)

def do_get_state(sock):
    params = {"m": "get_state"}
    return make_call(sock, params)

class UdpServer:

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        self.sock.bind(("", CONTROLLER_PORT))
        self.state = None
        self.statuses = {}

    def do_connect(self, uid, data, nid, sec, cas, addr):
        if uid == self.state["_uid"]: return
        do_create_link(self.sock, uid, data, nid, sec, cas)
        ip4 = self.state["_ip4"][:-3] + str(len(self.state["peers"]) + 101)
        ip6 = gen_ip6(uid, self.state["_ip6"])
        do_set_remote_ip(self.sock, uid, ip4, ip6)
        do_get_state(self.sock)

    def discover(self):
        if self.state == None: return
        for k, v in self.state["peers"].iteritems():
            if v["status"] == "online":
                request = {"m": "discover"}
                dest = (v["ip6"], CONTROLLER_PORT)
                self.sock.sendto(json.dumps(request), dest)

    def process_discover(self, addr):
        if self.state == None: return
        for k, v in self.state["peers"].iteritems():
            if v["status"] == "online":
                response = {"uid": k}
                response["data"] = v["fpr"]
                self.sock.sendto(json.dumps(response), addr)

    def route_notification(self, msg, addr):
        if addr[0] == LOCALHOST6:
            msg["from"] = self.state["_uid"]
            for k, v in self.state["peers"].iteritems():
                if v["status"] == "online":
                    dest = (v["ip6"], CONTROLLER_PORT, 0, 0)
                    self.sock.sendto(json.dumps(msg), dest)
        elif msg["uid"] in self.state["peers"]:
            peer = self.state["peers"][msg["uid"]]
            if peer["status"] == "online":
                dest = (peer["ip6"], CONTROLLER_PORT, 0, 0)
                self.sock.sendto(json.dumps(msg), dest)

    def serve(self):
        msg = None
        socks = select.select([self.sock], [], [], 15)
        for sock in socks[0]:
            data, addr = sock.recvfrom(4096)
            print "recv %s %s" % (addr, data)
            if data[0] == '{': msg = json.loads(data);

            if isinstance(msg, dict) and "_uid" in msg:
                self.state = msg

            # we only process if we have state and msg is json dict
            if self.state == None or not isinstance(msg, dict): continue

            if "m" in msg and msg["m"] == "discover":
                self.process_discover(addr)
                continue

            sec = True
            fpr_len = len(self.state["_fpr"])
            # this is a peer discovery notification
            if "data" in msg and len(msg["data"]) == fpr_len:
                if addr[0] == LOCALHOST6:
                    self.do_connect(msg["uid"], msg["data"], 1, sec, "", addr)
                else:
                    self.do_connect(msg["uid"], msg["data"], 0, sec, "", addr)
            # this is a connection request notification
            elif "data" in msg and len(msg["data"]) > fpr_len:
                if msg["uid"] == self.state["_uid"]:
                    self.do_connect(msg["from"], msg["data"][:fpr_len], 0,
                                    sec, msg["data"][fpr_len:], addr)
                else:
                    self.route_notification(msg, addr)
            # this is a connection state notification
            elif "data" in msg and len(msg["data"]) < fpr_len:
                self.statuses[msg["uid"]] = msg["data"]
                if msg["data"] == "offline" and addr[0] == LOCALHOST6:
                    do_trim_link(self.sock, msg["uid"])

def main():
    # uid has to be in hex and has to be 18 characters long
    user = sys.argv[1]
    password = sys.argv[2]
    host = sys.argv[3]
    uid = binascii.b2a_hex(os.urandom(9))
    server = UdpServer()
    do_set_callback(server.sock, server.sock.getsockname())
    do_set_local_ip(server.sock, uid, IP4, gen_ip6(uid))
    do_register_service(server.sock, user, password, host)
    do_get_state(server.sock)
    last_time = time.time()

    while True:
        server.serve()
        time_diff = time.time() - last_time
        if time_diff > 15:
            do_get_state(server.sock)
            server.discover()
            last_time = time.time()
            # server.do_pings()

if __name__ == '__main__':
    main()

