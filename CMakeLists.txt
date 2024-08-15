cmake_minimum_required(VERSION 3.16)

project(coalumine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

set(REACTIVE_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(reactive)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.hpp")
file(GLOB SHADERS shader/*) # exclude spv files

source_group("Shader Files" FILES ${SHADERS})
add_executable(${PROJECT_NAME} ${SOURCES} ${SHADERS})

find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")

target_link_libraries(${PROJECT_NAME} PUBLIC 
    reactive
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/reactive/include
    ${TINYGLTF_INCLUDE_DIRS}
)

file(COPY ${PROJECT_SOURCE_DIR}/asset DESTINATION ${PROJECT_BINARY_DIR}/Debug)
file(COPY ${PROJECT_SOURCE_DIR}/asset DESTINATION ${PROJECT_BINARY_DIR}/Release)
file(COPY ${CMAKE_SOURCE_DIR}/run.ps1 DESTINATION ${PROJECT_BINARY_DIR}/Release)
file(COPY ${CMAKE_SOURCE_DIR}/fps.txt DESTINATION ${PROJECT_BINARY_DIR}/Release)
