
cmake_minimum_required(VERSION 3.0)

# 获取当前文件夹路径
get_filename_component(CURRENT_DIR ${CMAKE_CURRENT_LIST_FILE} DIRECTORY)
 
# 获取文件夹名称
get_filename_component(FOLDER_NAME ${CURRENT_DIR} NAME)
 
# 打印文件夹名称
# message("当前文件夹名称: ${FOLDER_NAME}")

project(${FOLDER_NAME})

set(CMAKE_C_COMPILER arm-himix200-linux-gcc)
set(CMAKE_CXX_COMPILER arm-himix200-linux-g++)
set(CMAKE_CXX_FLAGS "-fpermissive")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os -DNDEBUG")  # 优化大小和速度
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--strip-all")  # 去除调试信息

include_directories(
    ./chassis
    ../../third_party/c-periphery
)

link_directories(
    ../../bin/
)


aux_source_directory(./  SRC)
aux_source_directory(./chassis  SRC)
aux_source_directory(../../third_party/c-periphery  SRC)
add_executable(${PROJECT_NAME} ${SRC})


target_link_libraries(
    ${PROJECT_NAME}
    m dl pthread
    common
    librtsp_server.a
    ${MPP_LIBA}
    ${AUDIO_LIBA}
    ${SENSOR_LIBA}
    ${OpenCV_LIBRARIES}
)
