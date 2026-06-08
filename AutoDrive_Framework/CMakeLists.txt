cmake_minimum_required(VERSION 3.14)
project(AutonomousDrivingSystem)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
add_compile_options("/utf-8")

# ================= 1. OpenCV 配置 =================
set(OpenCV_DIR "D:/opencv/build/x64/vc16/lib") 
find_package(OpenCV REQUIRED)
include_directories(include "D:/opencv/build/include" ${OpenCV_INCLUDE_DIRS})

# ================= 2. ⚡ 纯手工物理引入 yaml-cpp =================
# 强行关闭它的连网测试包下载！
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
# 告诉 CMake 源码就在本地叫 yaml-cpp 的文件夹里
add_subdirectory(yaml-cpp)

# ================= 3. 收集并编译你的代码 =================
file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "include/*.h")
add_executable(main_video ${SOURCES} ${HEADERS})

# ================= 4. 链接两大神器 =================
# ⚡ 注意这里把 yaml-cpp 加上了
target_link_libraries(main_video ${OpenCV_LIBS}
                         yaml-cpp
                         ws2_32  
                         )