cmake_minimum_required(VERSION 3.21)

project(SixMagManipulatorGui VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia)

qt_standard_project_setup()

set(IMAGE_PROCESSING_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../_imgproc")

qt_add_executable(SixMagManipulatorGui WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ManipulatorView.cpp
    src/ManipulatorView.h
    ${IMAGE_PROCESSING_DIRECTORY}/ImageTracker.cpp
    ${IMAGE_PROCESSING_DIRECTORY}/ImageTracker.h
)

target_include_directories(SixMagManipulatorGui PRIVATE
    ${IMAGE_PROCESSING_DIRECTORY}
)

target_link_libraries(SixMagManipulatorGui PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
)

set_target_properties(SixMagManipulatorGui PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

install(TARGETS SixMagManipulatorGui
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

