cmake_minimum_required(VERSION 3.16)
project(SimpleDAQ LANGUAGES CXX)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZMQ REQUIRED IMPORTED_TARGET libzmq)
find_package(ROOT QUIET COMPONENTS Core RIO Tree Hist Graf Gpad Rint)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Concrete module sources
# ---------------------------------------------------------------------------
set(KC705_TOF_FRONTEND_SOURCES
  src/concrete_modules/kc705_tof/kc705_tof_validator.cpp
  src/concrete_modules/kc705_tof/kc705_tof_frontend.cpp
)

set(KC705_TOF_MONITOR_SOURCES
  src/concrete_modules/kc705_tof/kc705_tof_decoder.cpp
)

set(KC705_TOF_MONITOR_ROOT_SOURCES
  src/concrete_modules/kc705_tof/kc705_tof_overview_analysis.cpp
)

# ---------------------------------------------------------------------------
# Shared sources
# ---------------------------------------------------------------------------
set(CORE_DRIVER_SOURCES
  src/drivers/tcp_device_driver.cpp
  src/core/daq_cli.cpp
  src/core/daq_runtime.cpp
  src/core/mysql_logger.cpp
  src/concrete_modules/module_registry.cpp
  ${KC705_TOF_FRONTEND_SOURCES}
)

set(MONITOR_SOURCES
  src/monitor/frame_source.cpp
  src/monitor/pipeline.cpp
  src/monitor/realtime_analysis.cpp
  src/monitor/sink_realtime.cpp
  src/monitor/sink_text.cpp
  src/monitor/zmq_frame_source.cpp
  src/concrete_modules/module_registry.cpp
  ${KC705_TOF_MONITOR_SOURCES}
)

set(CONTROL_API_SOURCES
  src/control_api/zmq_control_client.cpp
  src/control_api/zmq_status_subscriber.cpp
)

set(SIMPLEDAQ_HAS_ROOT_VALUE 0)
set(DAQ_MONITOR_ROOT_SOURCES)
set(DAQ_MONITOR_ROOT_LIBRARIES)
if(ROOT_FOUND)
  set(SIMPLEDAQ_HAS_ROOT_VALUE 1)
  list(APPEND DAQ_MONITOR_ROOT_SOURCES
    src/monitor/sink_root.cpp
    ${KC705_TOF_MONITOR_ROOT_SOURCES}
  )
  list(APPEND DAQ_MONITOR_ROOT_LIBRARIES
    ROOT::Core
    ROOT::RIO
    ROOT::Tree
    ROOT::Hist
    ROOT::Graf
    ROOT::Gpad
    ROOT::Rint
  )
endif()

function(link_whole_archive_if_supported target lib)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${target} PRIVATE "-Wl,--whole-archive" ${lib} "-Wl,--no-whole-archive")
  elseif(APPLE)
    target_link_libraries(${target} PRIVATE ${lib})
    target_link_options(${target} PRIVATE "LINKER:-force_load,$<TARGET_FILE:${lib}>")
  else()
    target_link_libraries(${target} PRIVATE ${lib})
  endif()
endfunction()

function(apply_simpledaq_root_define target)
  target_compile_definitions(${target} PRIVATE SIMPLEDAQ_HAS_ROOT=${SIMPLEDAQ_HAS_ROOT_VALUE})
endfunction()

# ---------------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------------
add_library(daq_core_drivers ${CORE_DRIVER_SOURCES})
target_include_directories(daq_core_drivers PUBLIC src)

add_library(daq_control_api ${CONTROL_API_SOURCES})
target_link_libraries(daq_control_api PUBLIC PkgConfig::LIBZMQ)
target_include_directories(daq_control_api PUBLIC src)

add_library(daq_monitor ${MONITOR_SOURCES})
target_link_libraries(daq_monitor PUBLIC daq_core_drivers PRIVATE PkgConfig::LIBZMQ)
target_include_directories(daq_monitor PUBLIC src)
if(DAQ_MONITOR_ROOT_SOURCES)
  target_sources(daq_monitor PRIVATE ${DAQ_MONITOR_ROOT_SOURCES})
  target_link_libraries(daq_monitor PRIVATE ${DAQ_MONITOR_ROOT_LIBRARIES})
endif()
apply_simpledaq_root_define(daq_monitor)

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------
add_executable(daq_core src/core/daq_core.cpp)
link_whole_archive_if_supported(daq_core daq_core_drivers)

add_executable(daqd src/control/daqd.cpp)
link_whole_archive_if_supported(daqd daq_core_drivers)
target_link_libraries(daqd PRIVATE PkgConfig::LIBZMQ)

add_executable(daqctl src/control/daqctl.cpp)
target_link_libraries(daqctl PRIVATE daq_control_api)
link_whole_archive_if_supported(daqctl daq_core_drivers)
target_include_directories(daqctl PRIVATE src)

add_executable(daqmon src/control/daqmon.cpp)
target_link_libraries(daqmon PRIVATE daq_control_api)
target_include_directories(daqmon PRIVATE src)

add_executable(datamon src/control/datamon.cpp)
target_link_libraries(datamon PRIVATE daq_control_api)
link_whole_archive_if_supported(datamon daq_monitor)
target_include_directories(datamon PRIVATE src)
apply_simpledaq_root_define(datamon)

add_executable(kc705_tof_dummy_device
  src/concrete_modules/kc705_tof/kc705_tof_dummy_device.cpp
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS
  daq_core
  daqd
  daqctl
  daqmon
  datamon
  kc705_tof_dummy_device
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
