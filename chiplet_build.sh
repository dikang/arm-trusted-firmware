# Linaro
#export CROSS_COMPILE=aarch64-linux-gnu-
# Yocto
export CROSS_COMPILE=aarch64-poky-linux-
make PLAT=zynqmp bl31
#make PLAT=zynqmp LOG_LEVEL=99 bl31
