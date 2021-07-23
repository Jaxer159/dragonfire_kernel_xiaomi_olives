#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2018 Raphiel Rollerscaperers (raphielscape)
# Copyright (C) 2018 Rama Bondan Prakoso (rama982)
# Android Kernel Build Script

# Main environtment
KERNEL_DIR=$PWD
KERN_IMG=$KERNEL_DIR/out/arch/arm64/boot/Image.gz-dtb
ZIP_DIR=$KERNEL_DIR/AnyKernel3
CONFIG=olives-perf_defconfig

# Export
export ARCH=arm64
export CROSS_COMPILE
export CROSS_COMPILE_ARM32

# Main Staff

clang_bin="$HOME/toolchains/proton-clang/bin"
gcc_prefix64="aarch64-linux-gnu-"
gcc_prefix32="arm-linux-gnueabi-"
CROSS_COMPILE="aarch64-linux-gnu-"
CROSS_COMPILE_ARM32="arm-linux-gnueabi-"

_ksetup_old_path="$PATH"
export PATH="$clang_bin:$PATH"

# Index of variables for cleanup in unsetup
_ksetup_vars+=(
	clang_bin
	gcc_prefix64
	gcc_prefix32
	jobs
	kmake_flags
)

kmake_flags+=(
	CC="clang"
	AR="llvm-ar"
	NM="llvm-nm"
	OBJCOPY="llvm-objcopy"
	OBJDUMP="llvm-objdump"
	STRIP="llvm-strip"

	CROSS_COMPILE="$gcc_prefix64"
	CROSS_COMPILE_ARM32="$gcc_prefix32"
)

# Build start
make O=out $CONFIG
make -j$(nproc --all) O=out \
                      ARCH=arm64 \
                      CC=clang \

if ! [ -a $KERN_IMG ]; then
    echo "Build error!"
    exit 1
fi

cd $ZIP_DIR
make clean &>/dev/null
cd ..

# For MIUI Build
# Credit Adek Maulana <adek@techdro.id>
OUTDIR="$KERNEL_DIR/out"
VENDOR_MODULEDIR="$KERNEL_DIR/AnyKernel3/modules/vendor/lib/modules"

STRIP="$HOME/toolchains/proton-clang/aarch64-linux-gnu/bin/strip$(echo "$(find "$HOME/toolchains/proton-clang/bin" -type f -name "aarch64-*-gcc")" | awk -F '/' '{print $NF}' |\
            sed -e 's/gcc/strip/')"

for MODULES in $(find "${OUTDIR}" -name '*.ko'); do
    "${STRIP}" --strip-unneeded --strip-debug "${MODULES}"
    find "${OUTDIR}" -name '*.ko' -exec cp {} "${VENDOR_MODULEDIR}" \;

    case ${MODULES} in
            */pronto_wlan.ko)
        cp "${MODULES}" "${VENDOR_MODULEDIR}/pronto_wlan.ko" ;;
    esac

done

cd libufdt/src && python2 mkdtboimg.py create $OUTDIR/arch/arm64/boot/dtbo.img $OUTDIR/arch/arm64/boot/dts/qcom/*.dtbo

echo -e "\n(i) Done moving modules"

rm "${VENDOR_MODULEDIR}/wlan.ko"

cd $ZIP_DIR
cp $KERN_IMG zImage
cp $OUTDIR/arch/arm64/boot/dtbo.img $ZIP_DIR
make normal &>/dev/null
cd ..
cd product/
ls | grep -v empty.txt | xargs rm -rf
cd ..
VER=3.2
ZIP_NAME="DragonFire-Kernel-olives-$VER.zip"
cp AnyKernel3/$ZIP_NAME product/
# Build end
sudo ./build_iw.sh
