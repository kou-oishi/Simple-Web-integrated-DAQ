cmake_minimum_required(VERSION 3.16)
project(SimpleDAQ LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(daq_core_drivers
  src/drivers/tcp_device_driver.cpp
  src/validators/kc705_tof_validator.cpp
  src/frontends/kc705_tof_frontend.cpp
  src/core/device_frontend_registry.cpp
  src/core/daq_cli.cpp
  src/core/daq_runtime.cpp
)
target_include_directories(daq_core_drivers PUBLIC src)

add_executable(daq_core src/core/daq_core.cpp)
target_link_libraries(daq_core PRIVATE daq_core_drivers)

add_executable(tcp_dummy_device src/tcp_dummy_device.cpp)

install(TARGETS daq_core
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS tcp_dummy_device
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
