#!/bin/sh
#
# this script shuts down container and collects data

mkdir logs

for i in `ls /var/lib/lxc`
do
    sudo lxc-shutdown -n $i
    mkdir logs/$i
    mv /var/lib/lxc/$i/rootfs/home/ubuntu/svpn/*.txt logs/$i
done

tar czvf logs.tgz logs

