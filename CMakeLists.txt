cmake_minimum_required(VERSION 3.16)

project(rtcamp9 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

set(REACTIVE_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(reactive)

file(GLOB_RECURSE SOURCES "src/*.cpp" "source/*.hpp")
add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PUBLIC 
    reactive
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/reactive/source
)
