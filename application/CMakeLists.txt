cmake_minimum_required(VERSION 3.0)

set(CMAKE_C_COMPILER arm-himix200-linux-gcc)
set(CMAKE_CXX_COMPILER arm-himix200-linux-g++)

project(ai_camera)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)


set(3516_SDK_PATH
    ~/work/3516/Hi3516CV500R001C02SPC020/01.software/board/Hi3516CV500_SDK_V2.0.2.0/
)

include_directories(
    ${3516_SDK_PATH}/smp/a7_linux/mpp/include/
    ../common
    ../common/svp/common
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/adp/
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/mp3dec/include/
    ../modules/
)


aux_source_directory(../common 3516_COMMON_SOURCE)
aux_source_directory(../modules 3516_COMMON_SOURCE)
aux_source_directory(../common/svp/common 3516_COMMON_SOURCE)


# RTSP

include_directories(
    ../submodule_rtsp_server/RtspServer/src/
    ../submodule_rtsp_server/RtspServer/src/3rdpart/
)

aux_source_directory(../submodule_rtsp_server/RtspServer/src/xop RTSP_SERVER_SOURCE)
aux_source_directory(../submodule_rtsp_server/RtspServer/src/net RTSP_SERVER_SOURCE)


include_directories(
    ../namespace/hi/rtsp/
    ../namespace/hi/venc/
    ../namespace/hi/vi/
    ../namespace/hi/storage/
)

aux_source_directory(../namespace/hi/rtsp/ 3516_COMMON_SOURCE)
aux_source_directory(../namespace/hi/venc/ 3516_COMMON_SOURCE)
aux_source_directory(../namespace/hi/vi/ 3516_COMMON_SOURCE)
aux_source_directory(../namespace/hi/storage/ 3516_COMMON_SOURCE)



# 传感器类型
add_definitions(-DSENSOR0_TYPE=GC2053_TEST)
add_definitions(-DSENSOR1_TYPE=GC2053_TEST)

set(MPP_LIBA
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

set(AUDIO_LIBA
    libVoiceEngine.a
    libupvqe.a
    libdnvqe.a
)

set(SENSOR_LIBA
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


link_directories(
    ${3516_SDK_PATH}/smp/a7_linux/mpp/lib/
)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/ai_camera AI_CAMERA)
add_executable(
    ${PROJECT_NAME}
    ${3516_COMMON_SOURCE}
    ${RTSP_SERVER_SOURCE}
    ${AI_CAMERA}
)
target_link_libraries(
    ${PROJECT_NAME}
    m dl pthread
    ${MPP_LIBA}
    ${AUDIO_LIBA}
    ${SENSOR_LIBA}
    ${FFMPEG_LIBA}
)