cmake_minimum_required(VERSION 3.16)

project(coalumine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(Imath CONFIG REQUIRED)
find_package(Alembic CONFIG REQUIRED)

set(REACTIVE_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(reactive)

file(GLOB CODES "code/*.cpp" "code/*.hpp")
source_group("Code" FILES ${CODES})

file(GLOB CODES_LOADER "code/loader/*.cpp" "code/loader/*.hpp")
source_group("Code/Loader" FILES ${CODES_LOADER})

file(GLOB CODES_SCENE "code/scene/*.cpp" "code/scene/*.hpp")
source_group("Code/Scene" FILES ${CODES_SCENE})

file(GLOB CODES_APP "code/app/*.cpp" "code/app/*.hpp")
source_group("Code/App" FILES ${CODES_APP})

file(GLOB SHADERS shader/*) # exclude spv files
source_group("Shader Files" FILES ${SHADERS})

add_executable(${PROJECT_NAME} ${SHADERS} ${CODES}
               ${CODES_LOADER} ${CODES_SCENE} ${CODES_APP})

find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")

target_link_libraries(${PROJECT_NAME} PUBLIC 
    reactive
    Imath::Imath Imath::ImathConfig
    Alembic::Alembic
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/code
    ${PROJECT_SOURCE_DIR}/reactive/include
    ${TINYGLTF_INCLUDE_DIRS}
)

file(COPY ${PROJECT_SOURCE_DIR}/asset DESTINATION ${PROJECT_BINARY_DIR}/Debug)
file(COPY ${PROJECT_SOURCE_DIR}/asset DESTINATION ${PROJECT_BINARY_DIR}/Release)
file(COPY ${CMAKE_SOURCE_DIR}/scripts/run.ps1 DESTINATION ${PROJECT_BINARY_DIR}/Release)
file(COPY ${CMAKE_SOURCE_DIR}/scripts/fps.txt DESTINATION ${PROJECT_BINARY_DIR}/Release)
