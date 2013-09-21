#!/bin/sh
# this script uses lxc to run multiple instances of SocialVPN
# this script is designed for Ubuntu 12.04 (64-bit)
#
# usage: svpn_lxc.sh username password host 1 10 30 svpn"

USERNAME=$1
PASSWORD=$2
XMPP_HOST=$3
CONTAINER_START=$4
CONTAINER_END=$5
WAIT_TIME=$6
MODE=$7
HOST=$(hostname)
IP_PREFIX="172.16.5"
CONTROLLER=gvpn_controller.py
START_PATH=container/rootfs/home/ubuntu/start.sh

sudo apt-get update
sudo apt-get install -y lxc tcpdump

wget -O ubuntu.tgz http://goo.gl/Ze7hYz
wget -O container.tgz http://goo.gl/XJgdtf
wget -O svpn.tgz http://goo.gl/Sg4Vh2

sudo tar xzf ubuntu.tgz; tar xzf container.tgz; tar xzf svpn.tgz
sudo cp -a ubuntu/* container/rootfs/
sudo mv container/home/ubuntu container/rootfs/home/ubuntu/
mv svpn container/rootfs/home/ubuntu/svpn/

STUN="stun.l.google.com:19302"
TURN=""
TURN_USER=""
TURN_PASS=""
for i in `ls container/rootfs/home/ubuntu/svpn/*.py`
do
    sed -i "s/STUN = .*/STUN = \"$STUN\"/g" $i
    sed -i "s/TURN = .*/TURN = \"$TURN\"/g" $i
    sed -i "s/TURN_USER = .*/TURN_USER = \"$TURN_USER\"/g" $i
    sed -i "s/TURN_PASS = .*/TURN_PASS = \"$TURN_PASS\"/g" $i
done

if [ "x$MODE" = "xsvpn" ]
then
    CONTROLLER=svpn_controller.py
fi

cat > $START_PATH << EOF
#!/bin/bash
SVPN_HOME=/home/ubuntu/svpn
CONFIG=\`cat \$SVPN_HOME/config\`
\$SVPN_HOME/svpn-jingle &> \$SVPN_HOME/svpn_log.txt &
python \$SVPN_HOME/$CONTROLLER \$CONFIG &> \$SVPN_HOME/controller_log.txt &
EOF

chmod 755 $START_PATH

sudo tcpdump -i lxcbr0 -w dump_$HOST.cap &> /dev/null &

for i in $(seq $CONTAINER_START $CONTAINER_END)
do
    container_name=container$i
    lxc_path=/var/lib/lxc
    container_path=$lxc_path/$container_name

    sudo cp -a container $container_name

    echo -n "$USERNAME $PASSWORD $XMPP_HOST $IP_PREFIX.$i" > \
             $container_name/rootfs/home/ubuntu/svpn/config

    if [ "x$MODE" = "xsvpn" ]
    then
        echo -n "$USERNAME $PASSWORD $XMPP_HOST" > \
                 $container_name/rootfs/home/ubuntu/svpn/config
    fi

    sudo mv $container_name $lxc_path
    sudo echo "lxc.rootfs = $container_path/rootfs" >> $container_path/config
    sudo echo "lxc.mount = $container_path/fstab" >> $container_path/config
    sudo lxc-start -n $container_name -d
    sleep $WAIT_TIME
done

