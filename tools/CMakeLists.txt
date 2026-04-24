cmake_minimum_required(VERSION 3.0)

# 必须在 project() 之前设置编译器
set(CMAKE_C_COMPILER arm-himix200-linux-gcc)
set(CMAKE_CXX_COMPILER arm-himix200-linux-g++)

project(tools)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# 使用绝对路径或 $ENV{HOME} 避免 ~ 问题
set(3516_SDK_PATH $ENV{HOME}/work/3516/Hi3516CV500R001C02SPC020/01.software/board/Hi3516CV500_SDK_V2.0.2.0/)
set(PQ_EXT_API_PATH ${3516_SDK_PATH}/Hi3516CV500_ext_api_V2.0.2.0)

include_directories(
    ${3516_SDK_PATH}/smp/a7_linux/mpp/include/
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/adp/
    ${3516_SDK_PATH}/smp/a7_linux/mpp/sample/audio/mp3dec/include/
    ${PQ_EXT_API_PATH}/include   # 根据实际调整，可能需要 libbin/include 等
)

# 库列表
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

set(PQ_LIBA
    libbin.a
    lib_pqcontrol.a
)

link_directories(
    ${3516_SDK_PATH}/smp/a7_linux/mpp/lib/
    ${PQ_EXT_API_PATH}/libbin/release
    ${PQ_EXT_API_PATH}/libcontrol/release
)

add_executable(pq_control_main pq_control_main/pq_control_main.c)

target_link_libraries(
    pq_control_main
    ${PQ_LIBA}
    ${SENSOR_LIBA}
    ${MPP_LIBA}
    ${AUDIO_LIBA}
    m dl pthread
)