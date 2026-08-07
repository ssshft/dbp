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


option(USE_INFO_SHM "publish instrument into via share memory instead of redis" OFF)
if(USE_INFO_SHM)
    add_definitions(-DUSE_INFO_SHM)
    message(STATUS "USE_IFNO_SHM = ON (SHM writer enabled)")
else()
    message(STATUS "USE_IFNO_SHM = OFF (redis writer)")
endif()


# 读取整个文件
file(READ /inc/version.inc FILE_CONTENT)

# 查找 COMPILE_VERSION= 之后的内容
string(FIND "${FILE_CONTENT}" "COMPILE_VERSION=" COMPILE_POS)
if(COMPILE_POS GREATER -1)
    math(EXPR VALUE_START "${COMPILE_POS} + 16")  # "COMPILE_VERSION=" 的长度是 16
    string(SUBSTRING "${FILE_CONTENT}" ${VALUE_START} -1 COMPILE_VERSION)
    
    # 去除换行符
    string(REGEX REPLACE "\n.*$" "" COMPILE_VERSION "${COMPILE_VERSION}")
    message("COMPILE_VERSION = ${COMPILE_VERSION}")
endif()
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