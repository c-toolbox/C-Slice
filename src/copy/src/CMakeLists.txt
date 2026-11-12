#
# Copyright: 2026 Erik Sundén
# SPDX-License-Identifier: GPL-3.0-or-later
#

# --- C-Opy (robocopy front-end for C-Slice outputs) ---
qt_add_executable(C-Opy)

find_package(Qt6 COMPONENTS Network REQUIRED)

set(COPY_SOURCE_FILES
    copymain.cpp
    copyapplication.cpp
    copyapplication.h
    copycontroller.cpp
    copycontroller.h
    copycredentials.cpp
    copycredentials.h
    copytargets.cpp
    copytargets.h
    robocopyrunner.cpp
    robocopyrunner.h
    settings/copysettings.kcfg
    settings/copysettings.kcfgc
)

# Try to find icon.rc in possible locations (for standalone and parent project setups)
set(_icon_rc_candidates
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/images/icon.rc
    ${CMAKE_SOURCE_DIR}/data/images/icon.rc
    data/images/icon.rc
)

set(_icon_rc_file "")
foreach(_candidate IN LISTS _icon_rc_candidates)
    if(EXISTS "${_candidate}")
        set(_icon_rc_file "${_candidate}")
        break()
    endif()
endforeach()

target_sources(C-Opy
    PRIVATE
    ${COPY_SOURCE_FILES}
    copy.qrc
)

if(_icon_rc_file)
    target_sources(C-Opy PRIVATE ${_icon_rc_file})
endif()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Cpp" FILES ${COPY_SOURCE_FILES})

set(COPY_QML_FILES
    qml/COpyMain.qml
    qml/COpySettingsDialog.qml
)

qt_add_qml_module(C-Opy
    URI org.ctoolbox.copy
    QML_FILES
    ${COPY_QML_FILES}
)
get_target_property(_qt_core_target_type Qt6::Core TYPE)
if(_qt_core_target_type STREQUAL "STATIC_LIBRARY")
    qt_import_qml_plugins(C-Opy)
else()
    set_target_properties(C-Opy PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
endif()

source_group(Qml FILES ${COPY_QML_FILES})

kconfig_add_kcfg_files(C-Opy GENERATE_MOC settings/copysettings.kcfgc)

set(COPY_LIBRARIES
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2

    KF6::ColorScheme
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::Kirigami
)

if(WIN32)
    # Credential Manager (CredReadW/CredWriteW) and CreateProcessWithLogonW
    list(APPEND COPY_LIBRARIES advapi32)
    # WNetGetConnectionW for mapped network drives
    list(APPEND COPY_LIBRARIES mpr)
endif()

if(MSVC)
    target_compile_options(C-Opy PRIVATE
        /utf-8
        /wd26439
        /wd26444
        /wd26495
        /wd26813
        /wd4702
        /experimental:external /external:anglebrackets /external:templates- /external:W0
    )
endif()

if(WIN32)
    set_target_properties(C-Opy PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(C-Opy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(C-Opy PRIVATE ${COPY_LIBRARIES})
target_compile_features(C-Opy PRIVATE cxx_std_23)

install(TARGETS C-Opy DESTINATION ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
