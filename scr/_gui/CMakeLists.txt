cmake_minimum_required(VERSION 3.21)

project(SixMagManipulatorGui VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt checks for optional Vulkan headers while loading Gui. Keep optional
# package probes quiet; REQUIRED Qt modules still fail if any are missing.
# If this project starts using Vulkan, add find_package(Vulkan REQUIRED).
find_package(Qt6 6.5 QUIET REQUIRED COMPONENTS
    Widgets Multimedia Network SerialPort)

qt_standard_project_setup()

set(IMAGE_PROCESSING_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../_imgproc")
set(MOTOR_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../_motors")
set(SENSOR_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../_sensors")
set(CONTROL_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../_control")
add_subdirectory("${CONTROL_DIRECTORY}" "${CMAKE_CURRENT_BINARY_DIR}/control")
set(VIMBA_X_ROOT "" CACHE PATH "Allied Vision Vimba X installation root")
if(NOT EXISTS "${VIMBA_X_ROOT}/api/include/VmbC/VmbC.h"
   OR NOT EXISTS "${VIMBA_X_ROOT}/api/lib/VmbC.lib")
    message(FATAL_ERROR "Vimba X C API was not found at VIMBA_X_ROOT='${VIMBA_X_ROOT}'")
endif()

qt_add_executable(SixMagManipulatorGui WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ControlPanelNavigation.cpp
    src/ControlPanelNavigation.h
    src/ManipulatorView.cpp
    src/ManipulatorView.h
    ${IMAGE_PROCESSING_DIRECTORY}/ImageTracker.cpp
    ${IMAGE_PROCESSING_DIRECTORY}/ImageTracker.h
    ${IMAGE_PROCESSING_DIRECTORY}/VimbaCameraSource.cpp
    ${IMAGE_PROCESSING_DIRECTORY}/VimbaCameraSource.h
    ${MOTOR_DIRECTORY}/DynamixelProtocol.cpp
    ${MOTOR_DIRECTORY}/DynamixelProtocol.h
    ${MOTOR_DIRECTORY}/MotorController.cpp
    ${MOTOR_DIRECTORY}/MotorController.h
    ${SENSOR_DIRECTORY}/HallSensorController.cpp
    ${SENSOR_DIRECTORY}/HallSensorController.h
)

target_include_directories(SixMagManipulatorGui PRIVATE
    ${IMAGE_PROCESSING_DIRECTORY}
    ${MOTOR_DIRECTORY}
    ${SENSOR_DIRECTORY}
    "${VIMBA_X_ROOT}/api/include"
)

target_link_libraries(SixMagManipulatorGui PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Network
    Qt6::SerialPort
    SixMag::Control
    "${VIMBA_X_ROOT}/api/lib/VmbC.lib"
)

set(VIMBA_RUNTIME_FILES
    VmbC.dll
    VmbC.xml
    GCBase_MD_VC142_v3_2_AVT.dll
    GenApi_MD_VC142_v3_2_AVT.dll
    Log_MD_VC142_v3_2_AVT.dll
    log4cpp_MD_VC142_v3_2_AVT.dll
    MathParser_MD_VC142_v3_2_AVT.dll
    NodeMapData_MD_VC142_v3_2_AVT.dll
    XmlParser_MD_VC142_v3_2_AVT.dll
)
foreach(VIMBA_RUNTIME_FILE IN LISTS VIMBA_RUNTIME_FILES)
    add_custom_command(TARGET SixMagManipulatorGui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VIMBA_X_ROOT}/api/bin/${VIMBA_RUNTIME_FILE}"
            "$<TARGET_FILE_DIR:SixMagManipulatorGui>/${VIMBA_RUNTIME_FILE}")
endforeach()

qt_add_executable(SixMagMotorEmulator
    ${MOTOR_DIRECTORY}/MotorEmulator.cpp
    ${MOTOR_DIRECTORY}/DynamixelProtocol.cpp
    ${MOTOR_DIRECTORY}/DynamixelProtocol.h
)

target_include_directories(SixMagMotorEmulator PRIVATE
    ${MOTOR_DIRECTORY}
)

target_link_libraries(SixMagMotorEmulator PRIVATE
    Qt6::Core
    Qt6::Network
)

set_target_properties(SixMagManipulatorGui PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

install(TARGETS SixMagManipulatorGui
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

option(SIXMAG_GUI_BUILD_TESTS "Build isolated GUI navigation tests" OFF)
if(SIXMAG_GUI_BUILD_TESTS)
    enable_testing()
    qt_add_executable(ControlPanelNavigationTests
        tests/ControlPanelNavigationTests.cpp
        src/ControlPanelNavigation.cpp
        src/ControlPanelNavigation.h)
    target_include_directories(ControlPanelNavigationTests PRIVATE src)
    target_link_libraries(ControlPanelNavigationTests PRIVATE Qt6::Widgets)
    add_test(NAME ControlPanelNavigation COMMAND ControlPanelNavigationTests -platform offscreen)
    qt_add_executable(SimulatorControlIntegrationTests
        tests/SimulatorControlIntegrationTests.cpp
        ${IMAGE_PROCESSING_DIRECTORY}/ImageTracker.cpp
        ${IMAGE_PROCESSING_DIRECTORY}/ImageTracker.h
        ${MOTOR_DIRECTORY}/MotorController.cpp
        ${MOTOR_DIRECTORY}/MotorController.h
        ${MOTOR_DIRECTORY}/DynamixelProtocol.cpp
        ${MOTOR_DIRECTORY}/DynamixelProtocol.h)
    target_include_directories(SimulatorControlIntegrationTests PRIVATE
        ${IMAGE_PROCESSING_DIRECTORY} ${MOTOR_DIRECTORY})
    target_link_libraries(SimulatorControlIntegrationTests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Network Qt6::SerialPort SixMag::Control)
    add_dependencies(SimulatorControlIntegrationTests SixMagMotorEmulator)
    add_test(NAME SimulatorControlIntegration COMMAND SimulatorControlIntegrationTests)
endif()
