
# Running Android SocialVPN on the Cloud

These instructions are for Ubuntu 12.04. Your VM should have 1GB RAM or more.

Your can create such a VM on FutureGrid as described in the following link

https://portal.futuregrid.org/manual/openstack/grizzly

### Install necessary packages

1. Update Debian/Ubuntu repo
```bash
    sudo apt-get update
    sudo apt-get install openjdk-7-jdk libc6:i386 libncurses5:i386 libstdc++6:i386
```
2. Download and extract Android SDK
```bash
    wget http://dl.google.com/android/android-sdk_r22.0.1-linux.tgz
    tar xzvf android-sdk_r22.0.1-linux.tgz
    cd android-sdk-linux
```
3. Download platform-tools and Android 4.1.2 qemu images (this takes a while)
```bash
    tools/android update sdk -u -t platform-tools,android-16,sysimg-16
```

### Instantiate Android Virtual Device

1. Define and create Android Virtual Device (AVD) (this also takes a while)
```bash
    tools/android create avd -n svpn-android-4.1 -t android-16 -c 1024M --abi armeabi-v7a
```
2. Launch the newly created AVD
```bash
    tools/emulator64-arm -avd svpn-android-4.1 -no-window -no-audio -no-skin &> log.txt &
```
3. Wait about one minute and test emulator is running with following command
   (a list of network devices along with ip addresses should appear)
```bash
    platform-tools/adb shell netcfg
```

### Download and run Android SocialVPN

1. Create directory for socialvpn files
```bash
    platform-tools/adb shell mkdir data/svpn
    platform-tools/adb shell mkdir data/svpn/python27
```
2. Download socialvpn and Python 2.7 for android
```bash
    wget -O svpn-arm.tgz http://goo.gl/eBrvy1
    wget -O python27.tgz http://goo.gl/jjJxyd
    tar xzvf python27.tgz; tar xzvf svpn-arm.tgz
```
3. Use *adb push* to copy downloaded files to AVD
```bash
    platform-tools/adb push svpn-arm /data/svpn
    platform-tools/adb push python27 /data/svpn/python27
```
4. Access the AVD shell and go to svpn directory
```bash
    platform-tools/adb shell
    cd /data/svpn
```
5. Launch socialvpn
```bash
    chmod 755 svpn-jingle-android
    ./svpn-jingle-android &> log.txt &
```
6. Log into XMPP (Google Chat or Jabber.org) using credentials
```bash
    sh start_controller.sh username password xmpp-host
```
7. Check the network devices and ip address for your device
```bash
    netcfg
```
8. Kill socialvpn process and terminate the AVD
```bash
    ps
    kill <svpn-jingle-android process id>
    exit
    platform-tools/adb shell emu kill
```

.. [#] It is important to kill svpn-jingle-android process in order to exit shell
.. [#] Run socialvpn on another machine using same credentials and they will
   connect with each other.

