# CMakeList.txt: CMake 项目，在此处包括源代码并定义项目特定的逻辑。

# 查找当前目录下的所有源文件并将名称保存到 DIR_LIB_SRCS 变量
aux_source_directory(. DIR_LIB_SRCS_DBSOURCE)

# 添加链接库
add_executable(dbsinitialer ${DIR_LIB_SRCS_SIG} dbsinitialer.cpp)
