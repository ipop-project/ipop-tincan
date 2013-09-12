
# Running SocialVPN Linux (64-bit)


These instructions are for Ubuntu or Debian.

## Download and run SocialVPN

1. Download socialvpn and extract for Linux
```bash
    wget -O svpn.tgz http://goo.gl/Sg4Vh2
    tar xvzf svpn.tgz
    cd svpn
```
2. Launch socialvpn
```bash
    sudo ./svpn-jingle &> log.txt &
```
3. Log into XMPP (Google Chat or Jabber.org) using credentials
```bash
    python vpn_controller.py username password xmpp-host
```
4. Check the network devices and ip address for your device
```bash
    /sbin/ifconfig svpn
```
5. Kill socialvpn::
```bash
    pkill svpn-jingle
    pkill vpn_controller.py
```
.. [#] Run socialvpn on another machine using same credentials and they will
   connect with each other.
