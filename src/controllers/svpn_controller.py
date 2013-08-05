
import socket
import select
import json
import httplib
import urllib
import time
import sys
import os
import binascii

def gen_ip6(uid, ip6="fd50:0dbc:41f2:4a3c:0000:0000:0000:0000"):
    uid_key = uid[-18:]
    count = (len(ip6) - 7) / 2
    ip6 = ip6[:19]
    for i in range(0, 16, 4):
        ip6 += ":"
        ip6 += uid_key[i:i+4]
    return ip6

def make_http_call(params):
    data = json.dumps(params)
    headers = {"content-type": "application/json",
                "content-length": str(len(data))}
    conn = httplib.HTTPConnection("127.0.0.1:5800")
    conn.request("POST", "", data, headers)
    response = conn.getresponse()
    data = response.read()
    conn.close()
    return json.loads(data)

def do_set_callback(address):
    addr = "%s:%s" % address
    params = {"m": "set_callback", "addr": addr}
    return make_http_call(params)

def do_register_service(username, password, host):
    params = {"m": "register_service", "u": username, "p": password, "h": host}
    return make_http_call(params)

def do_create_link(uid, fpr, nid, sec, stun="209.141.33.252:19302"):
    params = {"m": "create_link", "uid": uid, "fpr": fpr, "nid": nid,
              "stun" : stun, "turn": stun, "sec": sec}
    return make_http_call(params)

def do_trim_link(uid):
    params = {"m": "trim_link", "uid": uid}
    return make_http_call(params)

def do_set_local_ip(uid, ip4, ip6, ip4_mask=24, ip6_mask=64):
    params = {"m": "set_local_ip", "uid": uid, "ip4": ip4, "ip6": ip6,
              "ip4_mask": ip4_mask, "ip6_mask": ip6_mask}
    return make_http_call(params)

def do_set_remote_ip(uid, ip4, ip6):
    params = {"m": "set_remote_ip", "uid": uid, "ip4": ip4, "ip6": ip6}
    return make_http_call(params)

def do_ping(nid, uid, fpr):
    params = {"m": "ping", "uid": uid, "fpr": fpr}
    return make_http_call(params)

def do_get_state():
    params = {}
    return make_http_call(params)

class UdpServer:

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.statuses = {}

    def serve(self):
        socks = select.select([self.sock], [], [], 15)
        if len(socks[0]) == 0: return
        data, addr = socks[0][0].recvfrom(4096)
        print "recv %s %s" % (addr, data)
        parts = data.split()
        if parts[1] == "1":
            do_create_link(parts[0], parts[2], 0, True)
            state = do_get_state()
            ip4 = state["_ip4"][:-3] + str(len(state["peers"]) + 101)
            ip6 = gen_ip6(parts[0], state["_ip6"])
            do_set_remote_ip(parts[0], ip4, ip6)
        elif parts[1] == "2":
            self.statuses[parts[0]] = parts[2]

def main():
    counter = 0
    server = UdpServer()
    uid = binascii.b2a_hex(os.urandom(9))
    do_set_callback(server.sock.getsockname())
    do_set_local_ip(uid, "172.31.0.100", gen_ip6(uid))
    do_register_service(sys.argv[1], sys.argv[2], sys.argv[3])

    while True:
        counter += 1
        for k, v in server.statuses.iteritems():
            if v == "offline":
                do_trim_link(k)
                server.statuses[k] = "unknown"
        server.serve()
        try:
            state = do_get_state()
            if counter % 4 == 0:
                for peer in state["peers"]:
                    do_ping(0, peer["uid"], state["_fpr"])
                    time.sleep(1)
        except Exception as ex:
            print ex

if __name__ == '__main__':
    main()

