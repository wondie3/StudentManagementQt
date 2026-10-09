cmake_minimum_required(VERSION 3.21)
project(StudentManagementQt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Sql)

qt_add_executable(StudentManagementQt
    main.cpp
    assets/resources.qrc
)

target_link_libraries(StudentManagementQt PRIVATE Qt6::Widgets Qt6::Sql)

if(WIN32)
    set_target_properties(StudentManagementQt PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
