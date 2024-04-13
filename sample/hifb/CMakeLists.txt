cmake_minimum_required(VERSION 3.0)
project(sample_hifb)


set(CMAKE_C_COMPILER arm-himix200-linux-gcc)

aux_source_directory(./  SAMPLE_HIFB_SRC)
add_executable(sample_hifb ${SAMPLE_HIFB_SRC})

target_link_libraries(
    ${PROJECT_NAME}
    m dl pthread
    common
    ${MPP_LIBA}
    ${AUDIO_LIBA}
    ${SENSOR_LIBA}
)