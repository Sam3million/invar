set(APP_NAME invar)
cmake_minimum_required(VERSION 3.24)
project(${APP_NAME} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Third-party libraries built from source ----
# Build SDL as a static lib so there's no .dylib to ship next to the exe.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)
add_subdirectory(third_party/meshoptimizer EXCLUDE_FROM_ALL)

# ---- Your executable ----
file(GLOB_RECURSE ENGINE_SRCS CONFIGURE_DEPENDS src/*.cpp)
add_executable(${APP_NAME} ${ENGINE_SRCS}
        src/metal_imp.cpp
        src/main.cpp
        shaders/shared_types.h
        src/shared_types_assertions.cpp
        src/Camera.h
        src/Transform.h
        src/scene/Scene.h
        src/scene/GltfLoader.cpp
        src/scene/CameraController.h
        src/scene/CameraController.cpp
        src/PerformanceCounter.h
        src/PerformanceCounter.cpp
)

target_include_directories(${APP_NAME} PRIVATE
    src
    shaders                        # for shared_types.h
    third_party/metal-cpp
    third_party/cgltf
    third_party/stb)

target_link_libraries(${APP_NAME} PRIVATE
    SDL3::SDL3
    meshoptimizer
    "-framework Metal"
    "-framework Foundation"
    "-framework QuartzCore")

# Tell the program where things are during development.
target_compile_definitions(${APP_NAME} PRIVATE
    SHADER_LIB_PATH="${CMAKE_BINARY_DIR}/shaders.metallib"
    ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")

# ---- Shader compilation ----
file(GLOB SHADER_SRCS CONFIGURE_DEPENDS shaders/*.metal)
file(GLOB SHADER_HDRS CONFIGURE_DEPENDS shaders/*.h)

if(SHADER_SRCS)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders.metallib
        COMMAND xcrun -sdk macosx metal -std=metal3.1
                -I ${CMAKE_SOURCE_DIR}/shaders
                -gline-tables-only -frecord-sources
                ${SHADER_SRCS} -o ${CMAKE_BINARY_DIR}/shaders.metallib
        DEPENDS ${SHADER_SRCS} ${SHADER_HDRS}
        COMMENT "Compiling Metal shaders")
    add_custom_target(shaders ALL DEPENDS ${CMAKE_BINARY_DIR}/shaders.metallib)
    add_dependencies(${APP_NAME} shaders)
else()
    message(STATUS "No .metal files found in shaders/, skipping shader build")
endif()