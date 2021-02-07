#!/bin/bash

# pass target_cpu=\"aarch32\" as arg to build for arm

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'
ERROR="${RED}[ERROR]${NC}"
SUCCESS="${GREEN}[SUCCESS]${NC}"

gn_args="--args='$*'"
gn gen out --args=$*
if [ $? -ne 0 ]; then echo -e "${ERROR} Can't do gn gen" && exit 1; fi
ninja -C out scripts
if [ $? -ne 0 ]; then echo -e "${ERROR} Can't do ninja -C out scripts" && exit 1; fi
chmod +x out/build.sh
chmod +x out/run.sh
chmod +x out/sync.sh
chmod +x out/all.sh

#building one.img
IMAGE_SIZE=16M
qemu-img create -f raw out/one.img $IMAGE_SIZE
if [ $? -ne 0 ]; then echo -e "${ERROR} Can't create an out/one.img" && exit 1; fi
MKFS="" # Provide path here
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    MKFS=mkfs
elif [[ "$OSTYPE" == "darwin"* ]]; then
    MKFS=/usr/local/opt/e2fsprogs/sbin/mkfs.ext2
else
    echo "Please provide path to MKFS in gn_gen.sh"
fi
sudo $MKFS -t ext2 -r 0 -b 1024 out/one.img
if [ $? -ne 0 ]; then echo -e "${ERROR} Can't create an out/one.img" && exit 1; fi
echo -e "${SUCCESS} Generated files with args: $*"