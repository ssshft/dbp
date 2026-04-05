# CMakeList.txt: Thresh 的 CMake 项目，在此处包括源代码并定义项目特定的逻辑。
cmake_minimum_required(VERSION 3.8)
project(sig VERSION 1.0.0)

# 设置编译器 c++11 required 
set(CMAKE_CXX_STANDARD 17) 
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_BUILD_TYPE "Release")
#set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -fPIC -Wall -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -fPIC -Wall")

# 设置build输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)


file(READ /inc/version.inc COMPILE_VERSION LIMIT 8 OFFSET 33)
message(${COMPILE_VERSION})
set(COMPILE_PATH ${COMPILE_VERSION})

set(3RD_PATH /opt/version/3rd)

#include
set(INC_DIR              ${CMAKE_CURRENT_SOURCE_DIR}/sig)
set(SYSTEM_INC_DIR          ${COMPILE_PATH}/include)
set(FMT_INCLUDE_DIR          ${3RD_PATH}/fmt/include)


#link
set(FMT_LIBRARY        ${3RD_PATH}/fmt/lib)


include_directories(
        ${3RD_PATH}
        ${INC_DIR}
        ${SYSTEM_INC_DIR}
        ${FMT_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

link_directories(
        ${FMT_LIBRARY}
)

link_libraries(
        pthread
        boost_system 
        crypto 
        ssl 
        cpprest 
        z 
        rt  
        dl 
        cpp_redis 
        tacopie 
        tbb 
        stdc++fs 
        fmt
        zmq
        uv
)

aux_source_directory(${INC_DIR}/src DIR_SIG_SRCS)

# 设置子目录
add_subdirectory(dbprocess)