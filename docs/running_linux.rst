
================================
Running SocialVPN Linux (64-bit)
================================

These instructions are for Ubuntu or Debian.

Download and run SocialVPN
--------------------------

1. Download socialvpn for android::

    wget http://www.acis.ufl.edu/~ptony82/svpn-jingle

2. Launch socialvpn (use -v flag to enable logging)::

    chmod 755 svpn-jingle
    ./svpn-jingle &> log.txt &

3. Log into XMPP (Google Chat or Jabber.org) using credentials::

    curl http://127.0.0.1:5800/ -d \
    '{"m":1,"u":"username@gmail.com","p":"password","h":"talk.google.com"}'

4. Check on status, including showing list of connected friends[1,2]::

    curl http://127.0.0.1:5800/

5. Check the network devices and ip address for your device::

    /sbin/ifconfig svpn

6. Kill socialvpn::

    pkill svpn-jingle

.. [#] Run socialvpn on another machine using same credentials and they will
   connect with each other.
