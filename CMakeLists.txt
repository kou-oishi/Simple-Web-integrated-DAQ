cmake_minimum_required(VERSION 3.16)
project(SimpleDAQ LANGUAGES CXX)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZMQ REQUIRED libzmq)
find_package(ROOT QUIET COMPONENTS Core RIO Tree)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(daq_core_drivers
  src/drivers/tcp_device_driver.cpp
  src/concrete_modules/kc705_tof/kc705_tof_validator.cpp
  src/concrete_modules/kc705_tof/kc705_tof_frontend.cpp
  src/core/device_frontend_registry.cpp
  src/core/daq_cli.cpp
  src/core/daq_runtime.cpp
)
target_include_directories(daq_core_drivers PUBLIC src)

add_library(daq_control_api
  src/control_api/zmq_control_client.cpp
  src/control_api/zmq_status_subscriber.cpp
)
target_link_libraries(daq_control_api PRIVATE ${LIBZMQ_LIBRARIES})
target_include_directories(daq_control_api PUBLIC src ${LIBZMQ_INCLUDE_DIRS})
target_compile_options(daq_control_api PRIVATE ${LIBZMQ_CFLAGS_OTHER})

add_executable(daq_core src/core/daq_core.cpp)
target_link_libraries(daq_core PRIVATE daq_core_drivers)

add_executable(daqd src/control/daqd.cpp)
target_link_libraries(daqd PRIVATE daq_core_drivers ${LIBZMQ_LIBRARIES})
target_include_directories(daqd PRIVATE ${LIBZMQ_INCLUDE_DIRS})
target_compile_options(daqd PRIVATE ${LIBZMQ_CFLAGS_OTHER})

add_executable(daqctl src/control/daqctl.cpp)
target_link_libraries(daqctl PRIVATE daq_control_api)
target_include_directories(daqctl PRIVATE src)

add_executable(daqmon src/control/daqmon.cpp)
target_link_libraries(daqmon PRIVATE daq_control_api)
target_include_directories(daqmon PRIVATE src)

add_library(daq_monitor
  src/monitor/frame_source.cpp
  src/monitor/decoder_registry.cpp
  src/monitor/sink_text.cpp
  src/monitor/pipeline.cpp
  src/monitor/zmq_frame_source.cpp
  src/concrete_modules/kc705_tof/kc705_tof_decoder.cpp
)
target_include_directories(daq_monitor PUBLIC src ${LIBZMQ_INCLUDE_DIRS})
target_compile_options(daq_monitor PRIVATE ${LIBZMQ_CFLAGS_OTHER})
target_link_libraries(daq_monitor PRIVATE ${LIBZMQ_LIBRARIES})

if(ROOT_FOUND)
  target_sources(daq_monitor PRIVATE src/monitor/sink_root.cpp)
  target_link_libraries(daq_monitor PRIVATE ROOT::Core ROOT::RIO ROOT::Tree)
  target_compile_definitions(daq_monitor PRIVATE SIMPLEDAQ_HAS_ROOT=1)
else()
  target_compile_definitions(daq_monitor PRIVATE SIMPLEDAQ_HAS_ROOT=0)
endif()

add_executable(datamon src/control/datamon.cpp)
target_link_libraries(datamon PRIVATE daq_monitor)
target_include_directories(datamon PRIVATE src)
if(ROOT_FOUND)
  target_compile_definitions(datamon PRIVATE SIMPLEDAQ_HAS_ROOT=1)
else()
  target_compile_definitions(datamon PRIVATE SIMPLEDAQ_HAS_ROOT=0)
endif()

add_executable(kc705_tof_dummy_device src/concrete_modules/kc705_tof/kc705_tof_dummy_device.cpp)

install(TARGETS daq_core
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS daqd
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS daqctl
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS daqmon
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS datamon
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS kc705_tof_dummy_device
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
