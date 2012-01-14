#!/bin/bash

if [ ! -e ./dsvsm ]; then
  scp tomas@192.168.0.103:cvut/dsv/semestralka/dsvsm .
fi


cat >> /etc/hosts <<KONEC
11.11.11.101 node1
11.11.11.102 node2
11.11.11.103 node3
11.11.11.104 node4
KONEC

if [ ! -e ./nodeno ]; then
  read -p "Zadejte číslo uzlu (1,254):" nodeno
  echo $nodeno > ./nodeno
else
  nodeno=$(cat ./nodeno)
fi

ifconfig eth0 11.11.11.10$nodeno/24
hostname node$nodeno


