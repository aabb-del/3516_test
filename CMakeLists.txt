cmake_minimum_required(VERSION 3.0)
project(3516_test)

set(CMAKE_C_COMPILER arm-himix200-linux-gcc)

set(3516_SDK_PATH
    /home/cjj/Downloads/Hi3516CV500_SDK_V2.0.2.0
)

include_directories(
    ${3516_SDK_PATH}/smp/a7_linux/mpp/include/
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/common/
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/adp/
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/mp3dec/include/
)

# 原本一些宏是Makefile定义的，这里定义一下
add_definitions(-DSENSOR0_TYPE=GC2053_TEST)
add_definitions(-DSENSOR1_TYPE=GC2053_TEST)

link_directories(
    ${3516_SDK_PATH}/smp/a7_linux/mpp/lib/
)

aux_source_directory(${3516_SDK_PATH}/smp/a7_linux/mpp/sample/common/ 3516_COMMON_SOURCE)
# aux_source_directory(${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/adp/ 3516_ADP_SOURCE)
# 似乎要下一个库
# aux_source_directory(${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/mp3dec/src/ 3516_MP3DEC_SOURCE)
aux_source_directory(${3516_SDK_PATH}/smp/a7_linux/mpp/sample/vio 3516_VIO_SOURCE)
aux_source_directory(${3516_SDK_PATH}/smp/a7_linux/mpp/sample/vio/smp 3516_VIO_SMP_SOURCE)

add_executable(
    sample_vio
    ${3516_COMMON_SOURCE}
    # ${3516_ADP_SOURCE}
    # ${3516_MP3DEC_SOURCE}
    ${3516_VIO_SOURCE}
    ${3516_VIO_SMP_SOURCE}
)

set(
    MPP_LIBA
    libmpi.a
    libhdmi.a
    libsecurec.a
    libisp.a
    lib_hiir_auto.a             
    lib_hildci.a
    lib_hiae.a               
    lib_hiawb.a              
    libive.a            
    libsvpruntime.a
    lib_hiawb_natura.a       
    libmd.a             
    libtde.a
    lib_hicalcflicker.a      
    libmpi.a            
    libhi_cipher.a           
    libnnie.a           
    lib_hidehaze.a           
    lib_hidrc.a              
    libhifisheyecalibrate.a  
)

set(
    AUDIO_LIBA
    libVoiceEngine.a
    libupvqe.a
    libdnvqe.a
)

set(
    SENSOR_LIBA
    libsns_imx327.a
    libsns_imx327_2l.a
    libsns_imx307.a
    libsns_imx307_2l.a
    libsns_imx335.a
    libsns_gc2053.a             
    libsns_os04b10.a
    libsns_imx335_forcar.a  
    libsns_os05a.a
    libsns_imx415.a         
    libsns_os08a10.a
    libsns_imx458.a         
    libsns_ov12870.a
    libsns_mn34220.a        
    libsns_sc4210.a
)

target_link_libraries(
    sample_vio
    m dl pthread
    ${MPP_LIBA}
    ${AUDIO_LIBA}
    ${SENSOR_LIBA}
)



