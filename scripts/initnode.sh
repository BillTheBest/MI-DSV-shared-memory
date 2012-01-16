#!/bin/bash

grep node1 /etc/hosts ||
  cat >> /etc/hosts <<KONEC
10.11.11.100 node0
10.11.11.101 node1
10.11.11.102 node2
10.11.11.103 node3
10.11.11.104 node4
KONEC

if [ ! -e /etc/ssh/ssh_host_rsa_key ]; then
	ssh-keygen -f /etc/ssh/ssh_host_rsa_key
fi
if [ ! -e /etc/ssh/ssh_host_dsa_key ]; then
	ssh-keygen -f /etc/ssh/ssh_host_dsa_key
fi

killall sshd
/usr/sbin/sshd


if [ ! -e ./nodeno ]; then
  read -p "Zadejte číslo uzlu (1,254):" nodeno
  echo $nodeno > ./nodeno
else
  nodeno=$(cat ./nodeno)
fi

ifconfig eth0 10.11.11.10$nodeno/24
hostname node$nodeno

if [ ! -e ./dsvsm ]; then
  scp tomas@node0:cvut/dsv/semestralka/dsvsm .
fi

if [ ! -e ~/.ssh/authorized_keys ]; then
  cp authorized_keys ~/.ssh/
  chmod 600 ~/.ssh/authorized_keys
fi



