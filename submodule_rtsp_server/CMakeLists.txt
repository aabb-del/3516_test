cmake_minimum_required(VERSION 3.0)
project(rtsp_server)


set(CMAKE_C_COMPILER arm-himix200-linux-gcc)
set(CMAKE_CXX_COMPILER arm-himix200-linux-g++)

include_directories(
    ./
    ./RtspServer/src/
    ./RtspServer/src/3rdpart/
)


aux_source_directory(./ RTSP_SERVER)
aux_source_directory(./RtspServer/src/xop RTSP_SERVER_XOP)
aux_source_directory(./RtspServer/src/net RTSP_SERVER_NET)


add_library(
    ${PROJECT_NAME}
    ${RTSP_SERVER}
    ${RTSP_SERVER_XOP}
    ${RTSP_SERVER_NET}
)
