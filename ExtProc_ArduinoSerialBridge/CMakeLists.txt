cmake_minimum_required(VERSION 3.20)

# Arduino <-> PC serial bridge (reads the pot over USB, drives AcceleratorPosition).
project(ExtProc_ArduinoSerialBridge CXX)

# -------- Settings you may tweak ----------
# Where to install the sample under CMAKE_INSTALL_PREFIX
set(SampleOutputFolder "samples" CACHE STRING "Install subfolder for samples")
# Path to the openxilenv source tree (Src/ExternalProcess is used from here).
# Defaults to a sibling checkout, e.g. dev/src/openxiledu + dev/src/openxilenv.
set(OPENXILENV_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../openxilenv" CACHE PATH "Path to the openxilenv source tree")
# -----------------------------------------

# Executable name
set(ExtProc_TargetName ExtProc_ArduinoSerialBridge)

add_executable(${ExtProc_TargetName}
  ExtProc_ArduinoSerialBridge.cpp
)

# Require a modern C++ standard (Boost.Asio & friends are fine with 17)
target_compile_features(${ExtProc_TargetName} PRIVATE cxx_std_17)

target_include_directories(${ExtProc_TargetName} PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${OPENXILENV_SRC_DIR}/Src/ExternalProcess
)

# Threads (POSIX) and Boost.System for Boost.Asio serial_port
find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
target_link_libraries(${ExtProc_TargetName} PRIVATE
  Boost::system
  Threads::Threads
)

# Boost.Asio needs Winsock (WSAStartup/WSACleanup) on Windows.
if(WIN32)
  target_link_libraries(${ExtProc_TargetName} PRIVATE ws2_32 mswsock)
endif()

# Install (keep paths relative to CMAKE_INSTALL_PREFIX)
install(TARGETS ${ExtProc_TargetName}
  RUNTIME DESTINATION ${SampleOutputFolder}/ExtProc_ArduinoSerialBridge
)
