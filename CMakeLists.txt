cmake_minimum_required(VERSION 3.16)

project(rtcamp9 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

set(REACTIVE_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(reactive)

find_package(openvdb CONFIG REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp" "source/*.hpp")
file(GLOB SHADERS shader/*) # exclude spv files

source_group("Shader Files" FILES ${SHADERS})
add_executable(${PROJECT_NAME} ${SOURCES} ${SHADERS})

target_link_libraries(${PROJECT_NAME} PUBLIC 
    reactive
    OpenVDB::openvdb
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/reactive/source
)
