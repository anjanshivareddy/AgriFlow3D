cmake_minimum_required(VERSION 3.16)
project(AgriFlow3D LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find packages
find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)

# glad as a library
add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Main executable
add_executable(AgriFlow3D src/main.cpp)
target_include_directories(AgriFlow3D PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(AgriFlow3D PRIVATE glad OpenGL::GL glfw glm::glm)

# Platform-specific
if(UNIX AND NOT APPLE)
    target_link_libraries(AgriFlow3D PRIVATE dl m)
endif()
