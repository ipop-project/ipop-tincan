#!/bin/bash
# this script uses lxc to run multiple instances of SocialVPN
# this script is designed for Ubuntu 12.04 (64-bit)
#
# for VM 1
# svpn_lxc.sh username password host 1 10 30 10.0.3 gvpn"
#
# for VM 2
# svpn_lxc.sh username password host 11 20 30 10.0.4 gvpn"
#

USERNAME=$1
PASSWORD=$2
XMPP_HOST=$3
CONTAINER_START=$4
CONTAINER_END=$5
WAIT_INTERVAL=$6
LXC_NETWORK_BASE=$7
MODE=$8
HOST=$(hostname)
IP_PREFIX="172.16.5"

START_PATH=container/rootfs/home/ubuntu/start.sh
LXC_CONFIG_PATH=/etc/default/lxc

sudo apt-get update
sudo apt-get install -y lxc tcpdump

wget -O ubuntu.tgz http://goo.gl/Ze7hYz
wget -O container.tgz http://goo.gl/XJgdtf
wget -O svpn.tgz http://goo.gl/1nmORG

sudo tar xzf ubuntu.tgz; tar xzf container.tgz; tar xzf svpn.tgz
sudo cp -a ubuntu/* container/rootfs/
sudo mv container/home/ubuntu container/rootfs/home/ubuntu/
mv svpn container/rootfs/home/ubuntu/svpn/

cat > lxc-net.cfg << EOF
LXC_AUTO="true"
USE_LXC_BRIDGE="true"
LXC_BRIDGE="lxcbr0"
LXC_ADDR="$LXC_NETWORK_BASE.1"
LXC_NETMASK="255.255.255.0"
LXC_NETWORK="$LXC_NETWORK_BASE.0/24"
LXC_DHCP_RANGE="$LXC_NETWORK_BASE.2,$LXC_NETWORK_BASE.254"
LXC_DHCP_MAX="253"
LXC_SHUTDOWN_TIMEOUT=120

EOF

sudo cp lxc-net.cfg $LXC_CONFIG_PATH
sudo service lxc-net restart
sudo tcpdump -i eth0 -w svpn_$HOST.cap &> /dev/null &

cat > $START_PATH << EOF
#!/bin/bash
SVPN_HOME=/home/ubuntu/svpn
CONFIG=\`cat \$SVPN_HOME/config\`
\$SVPN_HOME/svpn-jingle &> \$SVPN_HOME/svpn_log.txt &
EOF

if [ "$MODE" == "gvpn" ]
then
    echo -n "python \$SVPN_HOME/gvpn_controller.py " >> $START_PATH
else
    echo -n "python \$SVPN_HOME/vpn_controller.py " >> $START_PATH
fi

echo " \$CONFIG &> \$SVPN_HOME/controller_log.txt &" >> $START_PATH
chmod 755 $START_PATH

for i in $(seq $CONTAINER_START $CONTAINER_END)
do
    container_name=container$i
    lxc_path=/var/lib/lxc
    container_path=$lxc_path/$container_name

    sudo cp -a container $container_name

    if [ "$MODE" == "gvpn" ]
    then
        echo -n "$USERNAME $PASSWORD $XMPP_HOST $IP_PREFIX.$i" > \
                 $container_name/rootfs/home/ubuntu/svpn/config
    else
        echo -n "$USERNAME $PASSWORD $XMPP_HOST" > \
                 $container_name/rootfs/home/ubuntu/svpn/config
    fi

    sudo mv $container_name $lxc_path
    sudo echo "lxc.rootfs = $container_path/rootfs" >> $container_path/config
    sudo echo "lxc.mount = $container_path/fstab" >> $container_path/config
    sudo lxc-start -n $container_name -d
    sleep $WAIT_INTERVAL
done

