#!/bin/sh
# this script runs SocialVPN inside Android 4.1 emulator
# this script is designed for Ubuntu 12.04 (64-bit)

USERNAME=$1
PASSWORD=$2
XMPP_HOST=$3
NO_CONTAINERS=$4
HOST=$(hostname)

sudo aptitude update
sudo aptitude install -y libc6:i386 libncurses5:i386 libstdc++6:i386 tcpdump

wget -O android.tgz http://goo.gl/zrtLAR
wget -O android-sdk.tgz http://goo.gl/ZCPwF6
tar xzf android.tgz; tar xzf android-sdk.tgz

cd android-sdk

wget -O svpn-arm.tgz http://goo.gl/eBrvy1
wget -O python27.tgz http://goo.gl/jjJxyd
tar xzf python27.tgz; tar xzf svpn-arm.tgz

tools/emulator64-arm -avd svpn-android-4.1 -no-window -no-audio -no-skin &> log.txt &
sleep 60

platform-tools/adb shell rm -r data/svpn
platform-tools/adb shell mkdir data/svpn
platform-tools/adb shell mkdir data/svpn/python27

platform-tools/adb push svpn-arm /data/svpn
platform-tools/adb push python27 /data/svpn/python27
platform-tools/adb shell chmod 755 /data/svpn/svpn-jingle-android

sudo tcpdump -i eth0 -w svpn_$HOST.cap &> /dev/null &
platform-tools/adb shell "cd /data/svpn; ./svpn-jingle-android & sh start_controller.sh $USERNAME $PASSWORD $XMPP_HOST &> log.txt &" &> log_$HOST.txt &

