cmake_minimum_required(VERSION 3.0)
# @note 放project后面会报错，不知道为什么
set(CMAKE_C_COMPILER arm-himix200-linux-gcc)
set(CMAKE_CXX_COMPILER arm-himix200-linux-g++)

project(3516_test)


set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

set(3516_SDK_PATH
    ~/work/3516/Hi3516CV500R001C02SPC020/01.software/board/Hi3516CV500_SDK_V2.0.2.0/
)


include_directories(
    ${3516_SDK_PATH}/smp/a7_linux/mpp/include/
    # ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/common/
    ./common
    ./common/svp/common
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/adp/
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/mp3dec/include/
)


add_subdirectory(./common)
add_subdirectory(./submodule_rtsp_server)


link_directories(
    ${3516_SDK_PATH}/smp/a7_linux/mpp/lib/
)


# 需要交叉编译一下
# ffmpeg
set(FFMPEG_PATH
    ../env_3516/ffmpeg-7.0/
)
include_directories(
    ${FFMPEG_PATH}/include
)
link_directories(
    ${FFMPEG_PATH}/lib/
)



# 原本一些宏是Makefile定义的，这里定义一下
# 传感器类型
add_definitions(-DSENSOR0_TYPE=GC2053_TEST)
add_definitions(-DSENSOR1_TYPE=GC2053_TEST)

# 库，使用全名指定链接静态库

set(FFMPEG_LIBA
    libavcodec.a   
    libavfilter.a  
    libavutil.a    
    libswresample.a  
    libavdevice.a  
    libavformat.a  
    libpostproc.a  
    libswscale.a
)


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




set(SAMPLE_1_NAME sample_1)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/sample_1 SAMPLE_1_SOURCE)
add_executable(
    ${SAMPLE_1_NAME}
    ${3516_COMMON_SOURCE}
    ${SAMPLE_1_SOURCE}
)
target_link_libraries(
    ${SAMPLE_1_NAME}
    m dl pthread
    common
    ${MPP_LIBA}
    ${AUDIO_LIBA}
    ${SENSOR_LIBA}
    ${FFMPEG_LIBA}
)


set(SAMPLE_2_NAME sample_2_mjpeg_jpeg)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/sample_2 SAMPLE_2_SOURCE)
add_executable(
    ${SAMPLE_2_NAME}
    ${3516_COMMON_SOURCE}
    ${SAMPLE_2_SOURCE}
)
target_link_libraries(
    ${SAMPLE_2_NAME}
    m dl pthread
    common
    ${MPP_LIBA}
    ${AUDIO_LIBA}
    ${SENSOR_LIBA}
    ${FFMPEG_LIBA}
)


set(SAMPLE_3_NAME sample_3_vb)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/sample_3 SAMPLE_3_SOURCE)
add_executable(
    ${SAMPLE_3_NAME}
    ${3516_COMMON_SOURCE}
    ${SAMPLE_3_SOURCE}
)
target_link_libraries(
    ${SAMPLE_3_NAME}
    m dl pthread
    common
    ${MPP_LIBA}
    ${AUDIO_LIBA}
    ${SENSOR_LIBA}
    ${FFMPEG_LIBA}
)


add_subdirectory(./sample/hifb)
add_subdirectory(./sample_5)
add_subdirectory(./sample_6)
