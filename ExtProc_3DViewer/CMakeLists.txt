cmake_minimum_required(VERSION 3.20)

project(ExtProc_3DViewer CXX)

# -------- Settings you may tweak ----------
# Where to install the sample under CMAKE_INSTALL_PREFIX
set(SampleOutputFolder "samples" CACHE STRING "Install subfolder for samples")
# Path to the openxilenv source tree (Src/ExternalProcess is used from here).
# Defaults to a sibling checkout, e.g. dev/src/openxiledu + dev/src/openxilenv.
set(OPENXILENV_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../openxilenv" CACHE PATH "Path to the openxilenv source tree")
# Path to an esmini release/build (must contain bin/libesminiLib.so or bin/esminiLib.lib
# and EnvironmentSimulator/Libraries/esminiLib/esminiLib.hpp)
set(ESMINI_LIBRARY_PATH "Default" CACHE PATH "path to esmini library")
# -----------------------------------------

if(UNIX)
    # Kept as the historical name - matches the prebuilt binary already
    # committed to the repo. Rename together if that binary is ever rebuilt.
    set(ExtProc_TargetName ExtProc_OpenScenario.EXE)
else()
    set(ExtProc_TargetName ExtProc_3DViewer)
endif()

add_executable(${ExtProc_TargetName}
    ExtProc_3DViewer.cpp
)

target_compile_features(${ExtProc_TargetName} PRIVATE cxx_std_17)

target_include_directories(${ExtProc_TargetName} PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${OPENXILENV_SRC_DIR}/Src/ExternalProcess
    ${ESMINI_LIBRARY_PATH}/EnvironmentSimulator/Libraries/esminiLib
)

if(WIN32)
    target_link_libraries(${ExtProc_TargetName} PRIVATE ${ESMINI_LIBRARY_PATH}/bin/esminiLib.lib)
else()
    target_link_libraries(${ExtProc_TargetName} PRIVATE ${ESMINI_LIBRARY_PATH}/bin/libesminiLib.so)
endif()

install(TARGETS ${ExtProc_TargetName}
    RUNTIME DESTINATION ${SampleOutputFolder}/ExtProc_3DViewer
)
if(WIN32)
    install(DIRECTORY "${ESMINI_LIBRARY_PATH}/bin/"
            DESTINATION ${SampleOutputFolder}/ExtProc_3DViewer
            FILES_MATCHING PATTERN "*.dll")
endif()

# Scenario assets actually referenced by the shipped .xosc scenarios.
# Listed explicitly (rather than globbed) so unused sample .xodr files don't
# silently end up in the distributed package.
install(FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/VehicleCatalog.xosc
            ${CMAKE_CURRENT_SOURCE_DIR}/default.xosc
            ${CMAKE_CURRENT_SOURCE_DIR}/cut-in.xosc
            ${CMAKE_CURRENT_SOURCE_DIR}/oval_track.xodr
            ${CMAKE_CURRENT_SOURCE_DIR}/e6mini.xodr
        DESTINATION ${SampleOutputFolder}/ExtProc_3DViewer)

# 3D vehicle models (referenced by VehicleCatalog.xosc) and ground textures
# (asphalt.jpg/grass.jpg, used by esmini's auto-generated road visualization
# whenever a .xodr has no dedicated 3D scene file - both of ours don't) via
# relative "../models/..." paths, so they must land as a sibling of
# ExtProc_3DViewer/.
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../models/
        DESTINATION ${SampleOutputFolder}/models
        FILES_MATCHING PATTERN "*.osgb" PATTERN "*.jpg")
