#!/bin/bash
#
# Copyright (C) 2020 azrim.
# Copyright (C) 2021 deadlylxrd.
#
# All rights reserved.

# Init
KERNEL_DIR="${PWD}"
DTB_TYPE="single" # define as "single" if want use single file
KERN_IMG="${KERNEL_DIR}"/out/arch/arm64/boot/Image.gz-dtb             # if use single file define as Image.gz-dtb instead
KERN_DTB="${KERNEL_DIR}"/out/arch/arm64/boot/dtbo.img # and comment this variable
ANYKERNEL="${HOME}"/AnyKernel3

# Repo URL
CLANG_REPO="https://github.com/silont-project/silont-clang"
GCC_REPO="https://github.com/silont-project/aarch64-elf-gcc"
GCC32_REPO="https://github.com/silont-project/arm-eabi-gcc"
ANYKERNEL_REPO="https://github.com/Dragonfire-Kernel/AnyKernel3.git"
ANYKERNEL_BRANCH="dragonfire_olives"

# Compiler
COMP_TYPE="clang" # unset if want to use gcc as compiler
if [[ "{$COMP_TYPE}" =~ "clang" ]]; then
    COMPILER="SiLonT Clang"
else
    COMPILER="Menrva GCC"
fi

CLANG_DIR="$HOME/toolchain/clang"
if ! [ -d "${CLANG_DIR}" ]; then
    git clone "$CLANG_REPO" --depth=1 "$CLANG_DIR"
fi

GCC_DIR="$HOME/toolchain/gcc" # Doesn't needed if use proton-clang
GCC32_DIR="$HOME/toolchain/gcc32" # Doesn't needed if use proton-clang
if ! [ -d "${GCC_DIR}" ]; then
    git clone --depth=1 -b arm64/11 "$GCC_REPO" "$GCC_DIR"
elif ! [ -d "${GCC32_DIR}" ]; then
    git clone --depth=1 -b arm/11 "$GCC32_REPO" "$GCC32_DIR"
fi

if [[ "${COMP_TYPE}" =~ "clang" ]]; then
    COMP_PATH="$CLANG_DIR/bin:${PATH}"
else
    COMP_PATH="${GCC_DIR}/bin:${GCC32_DIR}/bin:${PATH}"
fi

# Defconfig
DEFCONFIG="mi439-perf_defconfig"
REGENERATE_DEFCONFIG="true" # unset if don't want to regenerate defconfig

# Costumize
KERNEL="DragonFire"
KERNEL_VERSION="v1.0"
BUILD_TYPE="[STABLE]"
DEVICE="olives"
KERNELNAME="${BUILD_TYPE}-${KERNEL}-${KERNEL_VERSION}-${DEVICE}-$(date +%y%m%d-%H%M)"
TEMPZIPNAME="${KERNELNAME}-unsigned.zip"
ZIPNAME="${KERNELNAME}.zip"

# Get branch
CI_BRANCH=$(git rev-parse --abbrev-ref HEAD)

# Check Linux Version
LINUX_VERSION=$(make kernelversion)

# Set Date
DATE=$(TZ=Asia/Yekaterinburg date +"%F")

# Regenerating Defconfig
regenerate() {
    cp out/.config arch/arm64/configs/"${DEFCONFIG}"
    git add arch/arm64/configs/"${DEFCONFIG}"
    git commit -m "defconfig: Regenerate"
}

# Building
makekernel() {
    export PATH="${COMP_PATH}"
    rm -rf "${KERNEL_DIR}"/out/arch/arm64/boot # clean previous compilation
    mkdir -p out
    make O=out ARCH=arm64 ${DEFCONFIG}
    if [[ "${REGENERATE_DEFCONFIG}" =~ "true" ]]; then
        regenerate
    fi
    if [[ "${COMP_TYPE}" =~ "clang" ]]; then
        make -j$(nproc --all) CC=clang CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- O=out ARCH=arm64
    else
	make -j$(nproc --all) O=out ARCH=arm64 CROSS_COMPILE="${GCC_DIR}/bin/aarch64-elf-" CROSS_COMPILE_ARM32="${GCC32_DIR}/bin/arm-eabi-"
    fi
    # Check If compilation is success
    if ! [ -f "${KERN_IMG}" ]; then
	    END=$(date +"%s")
	    DIFF=$(( END - START ))
	    echo -e "Kernel compilation failed, See buildlog to fix errors"
	    exit 1
    fi
}

# Packing kranul
packingkernel() {
    # Copy compiled kernel
    if [ -d "${ANYKERNEL}" ]; then
        rm -rf "${ANYKERNEL}"
    fi
    git clone "$ANYKERNEL_REPO" -b "$ANYKERNEL_BRANCH" "${ANYKERNEL}"
    if [[ "${DTB_TYPE}" =~ "single" ]]; then
        cp "${KERN_IMG}" "${ANYKERNEL}"/Image.gz-dtb
    else
        mkdir "${ANYKERNEL}"/kernel/
        cp "${KERN_IMG}" "${ANYKERNEL}"/kernel/Image.gz
        cp "${KERN_DTB}" "${ANYKERNEL}"/dtbo.img
    fi

    # Zip the kernel, or fail
    cd "${ANYKERNEL}" || exit
    zip -r9 "${TEMPZIPNAME}" ./*

    # Sign the zip before sending it to Telegram
    curl -sLo zipsigner-4.0.jar https://raw.githubusercontent.com/baalajimaestro/AnyKernel3/master/zipsigner-4.0.jar
    java -jar zipsigner-4.0.jar "${TEMPZIPNAME}" "${ZIPNAME}"

}

# Starting
START=$(date +"%s")
makekernel
packingkernel
END=$(date +"%s")
DIFF=$(( END - START ))
echo "Build Done! $((DIFF / 60)) minute(s) and $((DIFF % 60)) second(s)!"
