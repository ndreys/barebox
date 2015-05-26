#!/bin/bash

set -ex

DISK=$1

if [ ! -b ${DISK} ]
then
    echo "Cannot proceed, ${DISK} is not a block device."
    exit 1
fi

script=$(readlink -f $0)
top=`dirname ${script}`

printf "o\nn\np\n\n\n+100M\nw\n" | fdisk ${DISK}
mkfs.fat ${DISK}1
dd if=${top}/../xload.bin of=${DISK} bs=512 seek=2 skip=2
mount ${DISK}1 /mnt
cp ${top}/../barebox.bin /mnt/barebox.bin
umount /mnt
