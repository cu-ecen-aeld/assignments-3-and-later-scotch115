#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
OS=$(cat /etc/os-release | sed -n -e 's/^ID_LIKE=//p')

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd ${OUTDIR}
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    # Support for Arch Linux (my distro)
    if [ $OS == "arch" ]; then
        sudo pacman -S --needed bc uboot-tools kmod cpio flex bison psmisc && \
        pacman -Sy --needed qemu-system-arm && \
        pacman -Sy --needed qemu-system-aarch64
    elif [ "${OS}" == "ubuntu"]; then
        sudo apt-get update && apt-get install -y --no-install-recommends \
            bc u-boot-tools kmod cpio flex bison libssl-dev psmisc && \
            apt-get install -y qemu-system-arm && \
            apt-get install -y qemu-system-aarch64

    fi

  make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper  # Clean
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig # Defconfig
	make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all   # vmlinux
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules   # Modules
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs      # Devicetree
fi

echo "Adding the Image in outdir"
if [ -f ${OUTDIR}/Image ]; then
  rm ${OUTDIR}/Image
fi

sleep 0.5
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image


echo "Creating the staging directory for the root filesystem"
cd ${OUTDIR}
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
echo "Creating '/bin' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/bin
echo "Creating '/dev' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/dev
echo "Creating '/etc' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/etc
echo "Creating '/lib' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/lib
echo "Creating '/proc' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/proc
echo "Creating '/sys' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/sys
echo "Creating '/sbin' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/sbin
echo "Creating '/tmp' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/tmp
echo "Creating '/usr' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/usr
echo "Creating '${OUTDIR}/bin' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/usr/bin
echo "Creating '${OUTDIR}/sbin' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/usr/sbin
echo "Creating '/var' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/var
echo "Creating '/home' in ${OUTDIR}/rootfs"
sleep 1
mkdir -p ${OUTDIR}/rootfs/home

cd ${OUTDIR}
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox

else
    cd busybox
fi

# TODO: Make and install busybox
make distclean
make defconfig
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install


# TODO: Add library dependencies to rootfs
echo "Library dependencies"
cd ${OUTDIR}/rootfs
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"


# TODO: Make device nodes
# mknod <name> <type> <major> <minor>
cd ${OUTDIR}/rootfs/dev
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1


# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
rm -f writer
rm -f *.o
${CROSS_COMPILE}gcc writer.c -o writer


# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cd ..
cp -r finder-app/ ${OUTDIR}/rootfs/home

# TODO: Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs


# TODO: Create initramfs.cpio.gz
cd "$OUTDIR/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio


