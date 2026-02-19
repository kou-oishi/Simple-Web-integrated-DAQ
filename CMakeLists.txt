cmake_minimum_required(VERSION 3.16)
project(SimpleDAQ LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(tcp_dummy_device src/tcp_dummy_device.cpp)

install(TARGETS tcp_dummy_device
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
