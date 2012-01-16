#!/bin/bash

fdisk /dev/hdd <<KONEC
n
p
1


w
KONEC

mkfs.ext3 /dev/hdd1
halt

