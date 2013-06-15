
================================
Running SocialVPN Linux (64-bit)
================================

These instructions are for Ubuntu or Debian.

Download and run SocialVPN
--------------------------

1. Download socialvpn for android::

    wget http://www.acis.ufl.edu/~ptony82/svpn-jingle

2. Launch socialvpn::

    chmod 755 svpn-jingle
    ./svpn-jingle-android &> log.txt &

3. Log into XMPP (Google Chat or Jabber.org) using credentials::

    ./curl-android http://127.0.0.1:5800/ -d \
    '{"m":1,"u":"username@gmail.com","p":"password","h":"talk.google.com"}'

4. Check on status, including showing list of connected friends[#]::

    curl http://127.0.0.1:5800/

5. Check the network devices and ip address for your device::

    /sbin/ifconfig svpn

6. Run socialvpn on another machine using same credentials and they will
   connect with each other.

7. Kill socialvpn::

    pkill svpn-jingle

.. [#] *rx : true* means the connection is readable, *tx : true* means writable
.. [#] *ip : 101* means the peer's ip address is 172.31.0.101 (will fix later)
