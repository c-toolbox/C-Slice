#
# Copyright: 2026 Erik Sundén
# SPDX-License-Identifier: GPL-3.0-or-later
#

set(TARGET_NAME C-Slice)
set(CSLICE_VERSION_MAJOR 1)
set(CSLICE_VERSION_MINOR 0)
set(CSLICE_VERSION_PATCH 0)
#set(CSLICE_VERSION_PR_NAME "Beta")
#set(CSLICE_VERSION_PR_VERS 6)

qt_add_executable(${TARGET_NAME})
set_compile_options(${TARGET_NAME})

set(CSLICE_SOURCE_FILES
    utils/domegrid.cpp
    utils/domegrid.h
    utils/imagesequenceutils.cpp
    utils/imagesequenceutils.h
    utils/planegrid.cpp
    utils/planegrid.h
    utils/spheregrid.cpp
    utils/spheregrid.h
    main.cpp
    ffmpegprobe.cpp
    ffmpegprobe.h
    mpvvideo.cpp
    mpvvideo.h
    mpvpreviewitem.cpp
    mpvpreviewitem.h
    sliceapplication.cpp
    sliceapplication.h
    slicecaptureoutput.cpp
    slicecaptureoutput.h
    slicecontroller.cpp
    slicecontroller.h
    sliceencoder.cpp
    sliceencoder.h
    sliceimageloader.cpp
    sliceimageloader.h
    slicemedialoader.h
    slicenode.cpp
    slicenode.h
    slicerenderer.cpp
    slicerenderer.h
    slicevideoloader.cpp
    slicevideoloader.h
    slicetypes.h
    slicewuffs.cpp
    slicewuffs.h
    settings/slicesettings.kcfg
    settings/slicesettings.kcfgc
    jobqueue/jobqueuemodel.cpp
    jobqueue/jobqueuemodel.h
    jobqueue/slicequeuemodel.cpp
    jobqueue/slicequeuemodel.h
)

set(CSLICE_WUFFS_ROOT "D:/Google/wuffs" CACHE PATH "Path to the Wuffs source checkout used by C-Slice")
set(CSLICE_WUFFS_RELEASE_DIR "${CSLICE_WUFFS_ROOT}/release/c")
if(NOT EXISTS "${CSLICE_WUFFS_RELEASE_DIR}/wuffs-unsupported-snapshot.c")
    message(FATAL_ERROR "C-Slice requires Wuffs at CSLICE_WUFFS_ROOT; expected ${CSLICE_WUFFS_RELEASE_DIR}/wuffs-unsupported-snapshot.c")
endif()

target_sources(${TARGET_NAME}
    PRIVATE
    ${CSLICE_SOURCE_FILES}
    ../data/presets/default.conf
    ../data/images/images.qrc
    ../data/images/icon.rc
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Cpp" FILES ${CSLICE_SOURCE_FILES})

if(BUILD_WITH_VCPKG_SUPPORT)
    option(SGCT_DEP_INCLUDE_LIBPNG "Include LibPNG library" OFF)
    option(SGCT_DEP_INCLUDE_ZLIB "Include ZLIB library" OFF)
    option(SGCT_DEP_INCLUDE_TINYXML "Include TinyXML library" OFF)
else()
    option(SGCT_DEP_INCLUDE_LIBPNG "Include LibPNG library" ON)
    option(SGCT_DEP_INCLUDE_ZLIB "Include ZLIB library" ON)
    option(SGCT_DEP_INCLUDE_ZLIB "Include ZLIB library" ON)
endif()

find_package(QT NAMES Qt6)
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6 AND ${QT_VERSION_MINOR} GREATER_EQUAL 8)
    qt_standard_project_setup(REQUIRES 6.8)
else()
    qt_standard_project_setup(REQUIRES 6.5)
endif()
qt_policy(SET QTP0001 NEW)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8.0)
    qt_policy(SET QTP0004 NEW)
endif()

set(CSLICE_QML_FILES
    qml/AudioMuxerDialog.qml
    qml/Main.qml
    qml/SettingsDialog.qml
)

if(CSLICE_FAST_QML_BUILD)
    foreach(QML_FILE ${CSLICE_QML_FILES})
        set_source_files_properties(${QML_FILE} PROPERTIES
            QT_QML_SKIP_QMLLINT TRUE
            QT_QML_SKIP_CACHEGEN TRUE
        )
    endforeach()
endif()

if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8.0)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_qml_module(${TARGET_NAME}
    URI org.ctoolbox.cslice
    QML_FILES
    ${CSLICE_QML_FILES}
)
get_target_property(_qt_core_target_type Qt6::Core TYPE)
if(_qt_core_target_type STREQUAL "STATIC_LIBRARY")
    qt_import_qml_plugins(${TARGET_NAME})
else()
    set_target_properties(${TARGET_NAME} PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
endif()
ecm_find_qmlmodule(org.kde.kirigami REQUIRED)

source_group(Qml FILES ${CSLICE_QML_FILES})

kconfig_add_kcfg_files(${TARGET_NAME} GENERATE_MOC settings/SliceSettings.kcfgc)

target_include_directories(${TARGET_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})

find_package(glm REQUIRED)
find_package(OpenGL REQUIRED)

find_package(Libmpv REQUIRED MODULE)

set(TARGET_LIBRARIES
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

    sgct::sgct
    glm::glm
    OpenGL::GL

    ${FFMPEG_LIBRARIES}
    Libmpv::Libmpv
)

if(WIN32)
    list(APPEND TARGET_LIBRARIES opengl32)
endif()

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE
        /utf-8
        /wd26439
        /wd26444
        /wd26495
        /wd26813
        /wd4702
        /wd4828
        /experimental:external /external:anglebrackets /external:templates- /external:W0
    )

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6 AND ${QT_VERSION_MINOR} GREATER_EQUAL 8)
    #set_target_properties(all_aotstats PROPERTIES FOLDER ${AUTO_TARGET_NAME})
    set_target_properties(${TARGET_NAME}_copy_qml PROPERTIES FOLDER ${AUTO_TARGET_NAME})
    set_target_properties(${TARGET_NAME}_copy_res PROPERTIES FOLDER ${AUTO_TARGET_NAME})
endif()
    set_target_properties(all_qmltyperegistrations PROPERTIES FOLDER ${AUTO_TARGET_NAME})
    set_target_properties(${TARGET_NAME}_automoc_json_extraction PROPERTIES FOLDER ${AUTO_TARGET_NAME})
    #set_target_properties(${TARGET_NAME}_other_files PROPERTIES FOLDER ${AUTO_TARGET_NAME})
    set_target_properties(${TARGET_NAME}_qmltyperegistration PROPERTIES FOLDER ${AUTO_TARGET_NAME})
    set_target_properties(${TARGET_NAME}_tooling PROPERTIES FOLDER ${AUTO_TARGET_NAME})
    set_target_properties(fetch-translations PROPERTIES FOLDER "External")

endif()

if(WIN32)
    set_target_properties(${TARGET_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

option(SGCT_FREETYPE_SUPPORT "Build SGCT with Freetype2" ON)
option(SGCT_SPOUT_SUPPORT "SGCT Spout support" OFF)
option(SGCT_NDI_SUPPORT "SGCT NDI Support" OFF)
option(SGCT_DEP_INCLUDE_FREETYPE "Include FreeType library" OFF)
option(SGCT_BUILD_TESTS "Build SGCT tests" OFF)
if(BUILD_WITH_VCPKG_SUPPORT)
    find_package(unofficial-minizip CONFIG REQUIRED)
    if(TARGET unofficial::minizip::minizip)
        if(NOT TARGET minizip)
            add_library(minizip INTERFACE IMPORTED GLOBAL)
            target_link_libraries(minizip INTERFACE unofficial::minizip::minizip)
        endif()
        if(NOT TARGET minizip::minizip)
            add_library(minizip::minizip INTERFACE IMPORTED GLOBAL)
            target_link_libraries(minizip::minizip INTERFACE unofficial::minizip::minizip)
        endif()
    endif()
endif()
set(SCN_USE_EXTERNAL_FAST_FLOAT OFF CACHE BOOL "Use an external FastFloat package for scnlib" FORCE)
set(SCN_DISABLE_FAST_FLOAT ON CACHE BOOL "Use std::from_chars instead of fetching FastFloat for scnlib" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
if(POLICY CMP0169)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/sgct)
if(POLICY CMP0169)
    cmake_policy(POP)
endif()

# The C-Slice encoder must compile against the same FFmpeg headers that it links to.
# Put the selected FFmpeg include directory before vcpkg/Craft/SGCT transitive includes.
target_include_directories(${TARGET_NAME} BEFORE PRIVATE ${CSLICE_FFMPEG_INCLUDE_DIR})
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CSLICE_WUFFS_RELEASE_DIR} ${Libmpv_INCLUDE_DIRS})
target_link_libraries(${TARGET_NAME} PRIVATE ${TARGET_LIBRARIES})
target_compile_features(${TARGET_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${TARGET_NAME} PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)
if(DEFINED CSLICE_VERSION_PR_NAME AND DEFINED CSLICE_VERSION_PR_VERS)
    target_compile_definitions(${TARGET_NAME} PUBLIC CSLICE_VERSION="${CSLICE_VERSION_MAJOR}.${CSLICE_VERSION_MINOR}.${CSLICE_VERSION_PATCH} ${CSLICE_VERSION_PR_NAME} ${CSLICE_VERSION_PR_VERS}")
    set(CSLICE_PACKNAME "${TARGET_NAME}_${CSLICE_VERSION_MAJOR}_${CSLICE_VERSION_MINOR}_${CSLICE_VERSION_PATCH}_${CSLICE_VERSION_PR_NAME}_${CSLICE_VERSION_PR_VERS}")
else()
    target_compile_definitions(${TARGET_NAME} PUBLIC CSLICE_VERSION="${CSLICE_VERSION_MAJOR}.${CSLICE_VERSION_MINOR}.${CSLICE_VERSION_PATCH}")
    set(CSLICE_PACKNAME "${TARGET_NAME}_${CSLICE_VERSION_MAJOR}_${CSLICE_VERSION_MINOR}_${CSLICE_VERSION_PATCH}")
endif()

configure_file(../help/CSlice_Pack_Installer.nsi.in ../help/CSlice_Pack_Installer.nsi @ONLY)
configure_file(../help/CSlice_Pack_Zip.bat.in ../help/CSlice_Pack_Zip.bat @ONLY)

if(BUILD_CRAFTKIT)
    set(CRAFTKIT_PACKNAME "C-Raftkit_${CSLICE_VERSION_MAJOR}_${CSLICE_VERSION_MINOR}_${CSLICE_VERSION_PATCH}")
    configure_file(../help/Craftkit_Pack_Installer.nsi.in ../help/Craftkit_Pack_Installer.nsi @ONLY)
    configure_file(../help/Craftkit_Pack_Zip.bat.in ../help/Craftkit_Pack_Zip.bat @ONLY)
endif()

target_compile_definitions(${TARGET_NAME} PUBLIC CSLICE_VERSION_MAJOR=${CSLICE_VERSION_MAJOR})
target_compile_definitions(${TARGET_NAME} PUBLIC CSLICE_VERSION_MINOR=${CSLICE_VERSION_MINOR})
target_compile_definitions(${TARGET_NAME} PUBLIC CSLICE_VERSION_PATCH=${CSLICE_VERSION_PATCH})

install(TARGETS ${TARGET_NAME} DESTINATION ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(DIRECTORY ../data/presets DESTINATION ${KDE_INSTALL_DATADIR}/${TARGET_NAME}/data FILES_MATCHING PATTERN "*.conf")

if(CSLICE_INSTALL_DEPENDENCIES)
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)

    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    get_filename_component(_qt_dir "${_qt_bin_dir}" DIRECTORY)
    get_filename_component(_ffmpeg_bin_dir "${AVUTIL_LIBRARIES}" DIRECTORY)

    install(CODE "set(QT_BIN_DIR \"${_qt_bin_dir}\")")

    option(CSLICE_INSTALL_PLUGINS_FROM_CRAFT "Copy complete plugins folder for Qt/KDE" ON)   
    if(CSLICE_INSTALL_PLUGINS_FROM_CRAFT)
        install(CODE [[
            message("Copying Qt/KDE plugins folder")
        ]])
        install(DIRECTORY "${_qt_dir}/plugins" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" FILES_MATCHING PATTERN "*.dll")
    endif()

    option(CSLICE_INSTALL_QML_FROM_CRAFT "Copy complete qml folder for Qt/KDE" ON)
    if(CSLICE_INSTALL_QML_FROM_CRAFT)
        install(CODE [[
            message("Copying KDE qml folder")
        ]])
        #
        install(DIRECTORY "${_qt_dir}/qml" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
            FILES_MATCHING PATTERN "*.dll")
        install(DIRECTORY "${_qt_dir}/qml" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
            FILES_MATCHING PATTERN "*.qmltypes")
        install(DIRECTORY "${_qt_dir}/qml" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
            FILES_MATCHING PATTERN "qmldir")
        install(DIRECTORY "${_qt_dir}/qml/org" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/qml")
    endif()

    set_property(TARGET ${TARGET_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:${TARGET_NAME}>)
    if(WIN32 AND CSLICE_INSTALL_DEPENDENCIES)
        set_property(TARGET ${TARGET_NAME} PROPERTY VS_DEBUGGER_ENVIRONMENT "PATH=${CSLICE_FFMPEG_RUNTIME_DIR};%PATH%")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CSLICE_FFMPEG_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:${TARGET_NAME}>
            COMMENT "Copying selected C-Slice FFmpeg runtime DLLs")
    endif()

    # Check if we are on Windows 11 (Build number 22000 or higher) or Windows 10 (Build number 10240 or higher)
    cmake_host_system_information(RESULT OS_VERSION QUERY OS_VERSION)
    string(REGEX MATCH "[0-9]+" OS_BUILD_NUM "${OS_VERSION}")

    if(${OS_BUILD_NUM} VERSION_GREATER_EQUAL 22000)
        message("Windows 11 or above detected, recommended to copy all DLLS from path instead of using GET_RUNTIME_DEPENDENCIES")
        set(INSTALL_GRP FALSE)
        set(INSTALL_DFP TRUE)
    else()
        message("Windows 10 or below detected, recommended to use GET_RUNTIME_DEPENDENCIES instead of copying all DLLS from path")
        set(INSTALL_GRP TRUE)
        set(INSTALL_DFP FALSE)
    endif()

    option(CSLICE_INSTALL_GET_RUNTIME_DEPENDENCIES "When running INSTALL, look for dependencies with GET_RUNTIME_DEPENDENCIES and copy them" ${INSTALL_GRP})
    if(CSLICE_INSTALL_GET_RUNTIME_DEPENDENCIES)
        if(SGCT_DEP_INCLUDE_SCALABLE)
            set(_scalable_dir ${CMAKE_CURRENT_SOURCE_DIR}/sgct/ext/scalable)
        endif()
        set(MY_DEPENDENCY_PATHS ${_ffmpeg_bin_dir} ${_qt_bin_dir} ${_scalable_dir})
        # Transfer the values into the install script
        install(CODE "set(MY_DEPENDENCY_PATHS \"${MY_DEPENDENCY_PATHS}\")")
        install(CODE "set(target \"$<TARGET_FILE:${TARGET_NAME}>\")")
        # Install dependencies
        install(CODE [[
            message("Looking for direct dependencies for C-Slice.exe (In Qt and MPV build directories)")
            file(GET_RUNTIME_DEPENDENCIES
                EXECUTABLES ${target}
                PRE_EXCLUDE_REGEXES api-ms msvcp vcruntime kernel32
                POST_EXCLUDE_REGEXES C:[/\\]Windows[/\\]
                CONFLICTING_DEPENDENCIES_PREFIX CONFLICTING_DEPENDENCIES
                RESOLVED_DEPENDENCIES_VAR RESOLVED_DEPENDENCIES
                UNRESOLVED_DEPENDENCIES_VAR UNRESOLVED_DEPENDENCIES
                DIRECTORIES ${MY_DEPENDENCY_PATHS}
            )
            foreach(DEPENDENCY ${CONFLICTING_DEPENDENCIES})
                message(WARNING "Conflicting dependency ${DEPENDENCY}")
            endforeach()
            message("Copying detected dependencies")
            foreach(DEPENDENCY ${RESOLVED_DEPENDENCIES})
                message("Copying ${DEPENDENCY}")
                file(INSTALL
                    DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
                    FILES "${DEPENDENCY}"
                )
            endforeach()
        ]])
    endif()

    option(CSLICE_INSTALL_DLLS_FROM_PATHS "When running INSTALL, copy all DLLS from CSLICE_INSTALL_DLLS_FROM_PATH_LIST to the INSTALL/bin folder" ${INSTALL_DFP})
    if(CSLICE_INSTALL_DLLS_FROM_PATHS)
        set(CIDFPL ${_qt_bin_dir} ${_ffmpeg_bin_dir})

        if(BUILD_WITH_VCPKG_SUPPORT)
            list(APPEND CIDFPL "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        endif()
        if(SGCT_DEP_INCLUDE_SCALABLE)
            list(APPEND CIDFPL ${CMAKE_CURRENT_SOURCE_DIR}/sgct/ext/scalable)
        endif()

        set(CSLICE_INSTALL_DLLS_FROM_PATH_LIST ${CIDFPL} CACHE STRING "The paths where to look for DLL:s")

        # Install dependencies
        install(CODE "set(_all_dll_copy_paths \"${CSLICE_INSTALL_DLLS_FROM_PATH_LIST}\")")
        install(CODE [[
                message("Copying all DLL:s from CSLICE_INSTALL_DLLS_FROM_PATH_LIST to install folder")
                foreach(DLL_DIR ${_all_dll_copy_paths})
                    message("Looking for DLL:s in ${DLL_DIR}")
                    if(EXISTS "${DLL_DIR}")
                        file(GLOB DLL_FILES "${DLL_DIR}/*.dll")
                        foreach(DLL_FILE ${DLL_FILES})
                            message("Copying ${DLL_FILE}")
                            file(INSTALL
                                DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
                                FILES "${DLL_FILE}"
                            )
                        endforeach()
                    else()
                        message("Directory ${DLL_DIR} does not exist")
                    endif()
                endforeach()
            ]])
    endif()

endif()

# --- Include C-Job via git subtree ---
add_subdirectory(cjob/src)