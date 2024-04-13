cmake_minimum_required(VERSION 3.0)
project(common)


set(CMAKE_C_COMPILER arm-himix200-linux-gcc)

set(3516_SDK_PATH
    /home/cjj/Downloads/Hi3516CV500_SDK_V2.0.2.0
)

include_directories(
    ${3516_SDK_PATH}/smp/a7_linux/mpp/include/
    # ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/common/
    ./svp/common
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/adp/
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/mp3dec/include/
)


# 原本一些宏是Makefile定义的，这里定义一下
# 传感器类型
add_definitions(-DSENSOR0_TYPE=GC2053_TEST)
add_definitions(-DSENSOR1_TYPE=GC2053_TEST)

link_directories(
    ${3516_SDK_PATH}/smp/a7_linux/mpp/lib/
)


aux_source_directory(./ 3516_COMMON_SOURCE)
aux_source_directory(./svp/common 3516_SVP_COMMON_SOURCE)



add_library(
    common
    ${3516_COMMON_SOURCE}
    ${3516_SVP_COMMON_SOURCE}
)



