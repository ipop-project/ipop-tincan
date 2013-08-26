
================================
Running SocialVPN Linux (64-bit)
================================

These instructions are for Ubuntu or Debian.

Download and run SocialVPN
--------------------------

1. Download socialvpn for android::

    wget http://www.acis.ufl.edu/~ptony82/stable/svpn-jingle
    wget http://www.acis.ufl.edu/~ptony82/stable/vpn_controller.py

2. Launch socialvpn::

    chmod 755 svpn-jingle vpn_controller.py
    sudo ./svpn-jingle &> log.txt &

3. Log into XMPP (Google Chat or Jabber.org) using credentials::

    python vpn_controller.py username password xmpp-host

4. Check the network devices and ip address for your device::

    /sbin/ifconfig svpn

5. Kill socialvpn::

    pkill svpn-jingle

.. [#] Run socialvpn on another machine using same credentials and they will
   connect with each other.
