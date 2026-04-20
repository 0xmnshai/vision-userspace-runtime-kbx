cmake_minimum_required(VERSION 3.31.6)
project(kbx_vision_runtime CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER clang++-18)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-D_GNU_SOURCE -Wall -Wextra -Wpedantic -Werror)

# Find Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(GBM REQUIRED gbm)
pkg_check_modules(LIBURING REQUIRED liburing)
pkg_check_modules(LIBBPF REQUIRED libbpf)
pkg_check_modules(NUMA REQUIRED numa)
find_package(OpenCL REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(OpenVINO_DIR "${CMAKE_SOURCE_DIR}/openvino/runtime/cmake")
    find_package(OpenVINO QUIET)
else()
    # Try to find system OpenVINO on non-x86 architectures
    find_package(OpenVINO QUIET)
endif()

if(OpenVINO_FOUND)
    message(STATUS "OpenVINO found, enabling inference components")
    add_definitions(-DUSE_OPENVINO)
else()
    message(WARNING "OpenVINO not found or incompatible architecture. Inference components will be disabled.")
endif()


# find_library(ZE_LOADER ze_loader REQUIRED) # Removed for universal architecture

include_directories(include ${DRM_INCLUDE_DIRS} ${GBM_INCLUDE_DIRS} ${LIBURING_INCLUDE_DIRS} ${LIBBPF_INCLUDE_DIRS} ${NUMA_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})

file(GLOB_RECURSE SRC_FILES "src/compute/*.cc" "src/gfx/*.cc" "src/io/*.cc" "src/mem/*.cc" "src/telemetry/*.cc")
if(OpenVINO_FOUND)
    file(GLOB_RECURSE INFER_FILES "src/infer/*.cc")
    list(APPEND SRC_FILES ${INFER_FILES})
endif()

add_executable(${PROJECT_NAME} main.cc ${SRC_FILES})


target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
    ${LIBURING_LIBRARIES}
    ${LIBBPF_LIBRARIES}
    ${NUMA_LIBRARIES}
    ${Vulkan_LIBRARIES}
    # ${ZE_LOADER} # Removed for universal architecture
    Vulkan::Vulkan
    OpenCL::OpenCL
    Threads::Threads)

if(OpenVINO_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE openvino::runtime)
endif()

# Compile OpenCL/Vulkan kernels offline (Generic SPIR-V)
# Use a universal compiler like glslangValidator or clspv in the future


# # Compile eBPF program
# add_custom_command(
#     TARGET ${PROJECT_NAME} PRE_BUILD
#     COMMAND clang -O2 -g -target bpf -c ${CMAKE_SOURCE_DIR}/src/telemetry/i915_trace.bpf.cc -o ${CMAKE_SOURCE_DIR}/src/telemetry/i915_trace.bpf.o
#     COMMAND bpftool gen skeleton ${CMAKE_SOURCE_DIR}/src/telemetry/i915_trace.bpf.o
# )


# sed -i 's/find_package(OpenVINO REQUIRED)/set(OpenVINO_DIR "${CMAKE_SOURCE_DIR}\/openvino\/runtime\/cmake")\nfind_package(OpenVINO REQUIRED)/' CMakeLists.txt && cd build && cmake .. && make && ./kbx_vision_runtime