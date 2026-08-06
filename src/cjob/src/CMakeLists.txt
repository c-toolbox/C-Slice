#
# Copyright: 2026 Erik Sundén
# SPDX-License-Identifier: GPL-3.0-or-later
#

# --- C-Job (Job Queue Server Application) ---
qt_add_executable(C-Job)

set(CJOB_SOURCE_FILES
    cjobmain.cpp
    cjobapplication.cpp
    cjobapplication.h
    jobqueue/cjobserver.cpp
    jobqueue/cjobserver.h
    jobqueue/cjobsocket.cpp
    jobqueue/cjobsocket.h
    jobqueue/jobqueuemodel.cpp
    jobqueue/jobqueuemodel.h
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

target_sources(C-Job
    PRIVATE
    ${CJOB_SOURCE_FILES}
    cjob.qrc
)

if(_icon_rc_file)
    target_sources(C-Job PRIVATE ${_icon_rc_file})
endif()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Cpp" FILES ${CJOB_SOURCE_FILES})

set(CJOB_QML_FILES
    qml/CJobMain.qml
)

qt_add_qml_module(C-Job
    URI org.ctoolbox.cjob
    QML_FILES
    ${CJOB_QML_FILES}
)
get_target_property(_qt_core_target_type Qt6::Core TYPE)
if(_qt_core_target_type STREQUAL "STATIC_LIBRARY")
    qt_import_qml_plugins(C-Job)
else()
    set_target_properties(C-Job PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
endif()

source_group(Qml FILES ${CJOB_QML_FILES})

set(CJOB_LIBRARIES
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2

    KF6::ColorScheme
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::CoreAddons
    KF6::I18n
    KF6::IconThemes
    KF6::Kirigami
)

if(MSVC)
    target_compile_options(C-Job PRIVATE
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
    set_target_properties(C-Job PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(C-Job PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(C-Job PRIVATE ${CJOB_LIBRARIES})
target_compile_features(C-Job PRIVATE cxx_std_23)

install(TARGETS C-Job DESTINATION ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})