
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
LOCALHOST = "127.0.0.1"
PORT = 5800

# TODO - this is temporary, never global
sock = None

def gen_ip6(uid, ip6=IP6):
    uid_key = uid[-18:]
    count = (len(ip6) - 7) / 2
    ip6 = ip6[:19]
    for i in range(0, 16, 4):
        ip6 += ":"
        ip6 += uid_key[i:i+4]
    return ip6

def make_call(params):
    data = json.dumps(params)
    dest = (LOCALHOST, PORT)
    print sock, data, dest
    return sock.sendto(data, dest)

def do_set_callback(address):
    addr = "%s:%s" % address
    params = {"m": "set_callback", "addr": addr}
    return make_call(params)

def do_register_service(username, password, host):
    params = {"m": "register_service", "u": username, "p": password, "h": host}
    return make_call(params)

def do_create_link(uid, fpr, nid, sec, stun=STUN):
    params = {"m": "create_link", "uid": uid, "fpr": fpr, "nid": nid,
              "stun" : stun, "turn": stun, "sec": sec}
    return make_call(params)

def do_trim_link(uid):
    params = {"m": "trim_link", "uid": uid}
    return make_call(params)

def do_set_local_ip(uid, ip4, ip6, ip4_mask=24, ip6_mask=64):
    params = {"m": "set_local_ip", "uid": uid, "ip4": ip4, "ip6": ip6,
              "ip4_mask": ip4_mask, "ip6_mask": ip6_mask}
    return make_call(params)

def do_set_remote_ip(uid, ip4, ip6):
    params = {"m": "set_remote_ip", "uid": uid, "ip4": ip4, "ip6": ip6}
    return make_call(params)

def do_ping(nid, uid, fpr):
    params = {"m": "ping", "uid": uid, "fpr": fpr}
    return make_call(params)

def do_get_state():
    params = {"m": "get_state"}
    return make_call(params)

class UdpServer:

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.state = None
        self.statuses = {}

    def serve(self):
        results = None
        socks = select.select([self.sock], [], [], 15)
        if len(socks[0]) == 0: return
        data, addr = socks[0][0].recvfrom(4096)
        print "recv %s %s" % (addr, data)

        if data[0] == '{':
            results = json.loads(data);

        if isinstance(results, dict):
            # this is a state response
            if "_uid" in results: self.state = results

            # we only process if we have state
            if self.state == None: return

            # this is a connection request notification
            if "data" in results and len(results["data"]) > 20:
                do_create_link(results["uid"], results["data"], 1, True)
                ip4 = self.state["_ip4"][:-3] + str(len(self.state["peers"]) + 101)
                ip6 = gen_ip6(results["uid"], self.state["_ip6"])
                do_set_remote_ip(results["uid"], ip4, ip6)
            # this is a connection state notification
            elif "data" in results and len(results["data"]) < 20:
                self.statuses[results["uid"]] = results["data"]
                if results["data"] == "offline":
                    do_trim_link(results["uid"])

def main():
    global sock
    server = UdpServer()
    sock = server.sock
    # controller decides on mechanism to generate unique id
    # uid has to be in hex and has to be 18 characters long
    uid = binascii.b2a_hex(os.urandom(9))
    do_set_callback(server.sock.getsockname())
    do_set_local_ip(uid, IP4, gen_ip6(uid))
    do_register_service(sys.argv[1], sys.argv[2], sys.argv[3])
    do_get_state()

    while True:
        try:
            server.serve()
        except Exception as ex:
            server.sock.close()
            print ex
            break

if __name__ == '__main__':
    main()

