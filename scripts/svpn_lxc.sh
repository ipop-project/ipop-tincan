#!/bin/bash
# this script uses lxc to run multiple instances of SocialVPN
# this script is designed for Ubuntu 12.04 (64-bit)

USERNAME=$1
PASSWORD=$2
XMPP_HOST=$3
NO_CONTAINERS=$4
HOST=$(hostname)
IP_PREFIX="172.16.5"
START_PATH=container/home/ubuntu/start.sh

sudo apt-get update
sudo apt-get install -y lxc tcpdump

wget -O ubuntu.tgz http://goo.gl/Ze7hYz
wget -O container.tgz http://goo.gl/XJgdtf
wget -O svpn.tgz http://goo.gl/R8sfm6

sudo tar xzf ubuntu.tgz; tar xzf container.tgz; tar xzf svpn.tgz
mv svpn container/home/ubuntu/

sudo tcpdump -i eth0 -w svpn_$HOST.cap &> /dev/null &

cat > $START_PATH << EOF
#!/bin/bash
SVPN_HOME=/home/ubuntu/svpn
CONFIG=\`cat \$SVPN_HOME/config\`
\$SVPN_HOME/svpn-jingle &> \$SVPN_HOME/svpn_log.txt &
python \$SVPN_HOME/vpn_controller.py \$CONFIG &> \$SVPN_HOME/controller_log.txt &
EOF

chmod 755 $START_PATH

for i in $(seq 1 $NO_CONTAINERS)
do
    container_name=container$i
    lxc_path=/var/lib/lxc
    container_path=$lxc_path/$container_name

    cp -r container $container_name
    echo -n "$USERNAME $PASSWORD $XMPP_HOST" > \
             $container_name/home/ubuntu/svpn/config

    #echo -n "$USERNAME $PASSWORD $XMPP_HOST $IP_PREFIX.$i" > \
    #         $container_name/home/ubuntu/svpn/config

    sudo mv $container_name $lxc_path
    sudo echo "lxc.rootfs = $container_path/rootfs" >> $container_path/config
    sudo echo "lxc.mount = $container_path/fstab" >> $container_path/config
    sudo mount -o ro,bind ./ubuntu $container_path/rootfs
    sudo mount --bind $container_path/home $container_path/rootfs/home
    sudo lxc-start -n $container_name -d
    sleep 15
done

