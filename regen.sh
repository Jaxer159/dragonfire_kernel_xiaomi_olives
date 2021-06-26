CONFIG=dragonfire_olives_defconfig
export ARCH=arm64
make O=out $CONFIG savedefconfig
cp out/defconfig arch/arm64/configs/$CONFIG
