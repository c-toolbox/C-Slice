# SPDX-FileCopyrightText: 2026 Erik Sundén
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)
project(COpyRegressionTests LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Network Test)
find_package(KF6Config 6.0 REQUIRED)

enable_testing()
add_executable(copycontroller_tests
    copycontroller_tests.cpp
    ../src/copycontroller.cpp
    ../src/copycontroller.h
    ../src/copycredentials.cpp
    ../src/copytargets.cpp
    ../src/robocopyrunner.cpp
    ../src/robocopyrunner.h
)
kconfig_add_kcfg_files(copycontroller_tests GENERATE_MOC ../src/settings/copysettings.kcfgc)
target_include_directories(copycontroller_tests PRIVATE ../src)
target_compile_definitions(copycontroller_tests PRIVATE QT_USE_QSTRINGBUILDER)
target_link_libraries(copycontroller_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test KF6::ConfigGui)
if(WIN32)
    target_link_libraries(copycontroller_tests PRIVATE advapi32)
endif()
if(MSVC)
    target_compile_options(copycontroller_tests PRIVATE /utf-8)
endif()
add_test(NAME copycontroller COMMAND copycontroller_tests)
