
cmake_minimum_required(VERSION 3.22)

set(PROJ "wayland-sycl-client")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_COMPILER "icx")
set(CMAKE_C_FLAGS "-std=c2x")
set(CMAKE_CXX_COMPILER "icpx")
set(CMAKE_CXX_FLAGS "-std=c++2b -sycl-std=2020")
set(PROTOCOL_DIR "/usr/share/wayland-protocols/unstable/xdg-shell/")

project(${PROJ})
find_package(IntelDPCPP REQUIRED)

add_custom_command(
  OUTPUT xdg-shell-private.c
  COMMAND wayland-scanner client-header ${PROTOCOL_DIR}/xdg-shell-unstable-v6.xml xdg-shell-client.h
  COMMAND wayland-scanner private-code  ${PROTOCOL_DIR}/xdg-shell-unstable-v6.xml xdg-shell-private.c)

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
  /opt/intel/oneapi/compiler/2022.2.0/linux/include/sycl/)

add_executable(${PROJ}
  main.cc
  ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-private.c)

target_compile_options(${PROJ}
  PRIVATE
  -Wall
  -O3)

target_link_libraries(${PROJ}
  PRIVATE
  EGL
  OpenGL
  OpenCL
  wayland-egl
  wayland-client)

add_custom_target(run
  DEPENDS ${PROJ}
  COMMAND WAYLAND_DEBUG=1 ./${PROJ})
