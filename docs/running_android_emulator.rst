
======================================
Running Android SocialVPN on the Cloud
======================================

These instructions are for Ubuntu 12.04. Your VM should have 1GB RAM or more.

Install necessary packages
--------------------------

1. Update Debian/Ubuntu repo::

    sudo apt-get update
    sudo apt-get install openjdk-7-jdk libc6:i386 libncurses5:i386 libstdc++6:i386

2. Download and extract Android SDK::

    wget http://dl.google.com/android/android-sdk_r22.0.1-linux.tgz
    tar xzvf android-sdk_r22.0.1-linux.tgz
    cd android-sdk-linux

3. Download platform-tools and Android 4.1.2 qemu images (this takes a while)::

    tools/android update sdk -u -t platform-tools,android-16,sysimg-16

Instantiate Android Virtual Device
----------------------------------

1. Define and create Android Virtual Device (AVD) (this also takes a while)::

    tools/android create avd -n svpn-android-4.1 -t android-16 -c 1024M --abi armeabi-v7a

2. Launch the newly created AVD::

    tools/emulator64-arm -avd svpn-android-4.1 -no-window -no-audio &> log.txt &

3. Wait about one minute and test emulator is running with following command::

    platform-tools/adb shell netcfg

Download and run Android SocialVPN
----------------------------------

1. Create directory for socialvpn files::

    platform-tools/adb shell mkdir data/svpn

2. Download socialvpn for android::

    wget http://www.acis.ufl.edu/~ptony82/svpn-jingle-android

3. Download curl for android needed to communicate with socialvpn over http::

    wget http://www.acis.ufl.edu/~ptony82/curl-android

4. Use *adb push* to copy downloaded files to AVD::

    platform-tools/adb push svpn-jingle-android /data/svpn
    platform-tools/adb push curl-android /data/svpn

5. Access the AVD shell and go to svpn directory::

    platform-tools/adb shell
    cd /data/svpn

6. Launch socialvpn::

    chmod 755 svpn-jingle-android curl-android
    ./svpn-jingle-android &> log.txt &

7. Log into XMPP (Google Chat or Jabber.org) using credentials::

    ./curl-android http://127.0.0.1:5800/ -d \
    '{"m":1,"u":"username@gmail.com","p":"password","h":"talk.google.com"}'

8. Check on status, including showing list of connected friends[1,2]::

    ./curl-android http://127.0.0.1:5800/

9. Check the network devices and ip address for your device::

    netcfg

9. Run socialvpn on another machine using same credentials and they will
   connect with each other.

10. Kill socialvpn process and terminate the AVD::

    pkill svpn-jingle-android
    exit
    platform-tools/adb shell emu kill


.. [#] *rx : true* means the connection is readable, *tx : true* means writable
.. [#] *ip : 101* means the peer's ip address is 172.31.0.101 (will fix later)
