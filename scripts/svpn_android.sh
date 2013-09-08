#!/bin/bash
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

wget -O svpn.tgz http://goo.gl/R8sfm6
wget -O python27.tgz http://goo.gl/jjJxyd
tar xzf python27.tgz; tar xzf svpn.tgz

tools/emulator64-arm -avd svpn-android-4.1 -no-window -no-audio -no-skin &> log.txt &
sleep 120

platform-tools/adb shell rm -r data/svpn
platform-tools/adb shell mkdir data/svpn
platform-tools/adb shell mkdir data/svpn/python27

platform-tools/adb push svpn /data/svpn
platform-tools/adb push python27 /data/svpn/python27
platform-tools/adb shell chmod 755 /data/svpn/svpn-jingle-android

cat > start_controller.sh << EOF
export PYTHONHOME=/data/svpn/python27/files/python
export PYTHONPATH=/data/svpn/python27/extras/python:/data/svpn/python27/files/python/lib/python2.7/lib-dynload:/data/svpn/python27/files/python/lib/python2.7
export PATH=$PYTHONHOME/bin:$PATH
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/data/svpn/python27/files/python/lib:/data/svpn/python27/files/python/lib/python2.7/lib-dynload
python vpn_controller.py $@
EOF

platform-tools/adb push start_controller.sh /data/svpn/
sudo tcpdump -i eth0 -w svpn_$HOST.cap &> /dev/null &
platform-tools/adb shell "cd /data/svpn; ./svpn-jingle-android & sh start_controller.sh svpntest@ejabberd password $USERNAME $PASSWORD $XMPP_HOST &> log.txt &" &> log_$HOST.txt &

