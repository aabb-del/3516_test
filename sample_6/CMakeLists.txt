cmake_minimum_required(VERSION 3.0)
project(sample_ivs)

set(CMAKE_C_COMPILER arm-himix200-linux-gcc)
set(CMAKE_CXX_COMPILER arm-himix200-linux-g++)
set(CMAKE_CXX_FLAGS "-fpermissive")

include_directories(
    ../submodule_rtsp_server/
)



link_directories(
    ../bin/
)


aux_source_directory(./  SAMPLE_IVS)
add_executable(${PROJECT_NAME} ${SAMPLE_IVS})

target_link_libraries(
    ${PROJECT_NAME}
    m dl pthread
    common
    librtsp_server.a
    ${MPP_LIBA}
    ${AUDIO_LIBA}
    ${SENSOR_LIBA}
)