#
# Copyright: 2024-2026 Erik Sunden <eriksunden85@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

if(CSLICE_INSTALL_DEPENDENCIES)
    install(CODE [[
        message("Copying C-Slice Data")
    ]])
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/slice" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data" OPTIONAL)
    option(CSLICE_INSTALL_ITEMS_TO_DATA "Copy specific data items to C-Slice" ON)
    if(CSLICE_INSTALL_ITEMS_TO_DATA)
        get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
        get_filename_component(_qt_dir "${_qt_bin_dir}" DIRECTORY)
        install(CODE "set(PROJECT_SOURCE_DIR \"${PROJECT_SOURCE_DIR}\")")
        install(CODE "set(QT_BIN_DIR \"${_qt_bin_dir}\")")
        install(CODE [[
            message("Copying Color Schemes Icons")
        ]])
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/color-schemes" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data" OPTIONAL)
        install(CODE [[
            message("Copying Breeze Icons")
        ]])
        install(DIRECTORY "${_qt_bin_dir}/data/icons" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data" OPTIONAL)
        install(CODE [[
            message("Copying Configs")
        ]])
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/configs" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data" OPTIONAL)
        install(CODE [[
            message("Copying Parameters")
        ]])
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/parameters" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data" OPTIONAL)
        install(CODE [[
			message("Copying Data Items")
			file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data/images" FILES "${PROJECT_SOURCE_DIR}/data/images/C_transparent.ico")
			file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data/images" FILES "${PROJECT_SOURCE_DIR}/data/images/C_transparent.png")
			file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data/images" FILES "${PROJECT_SOURCE_DIR}/data/images/icon.rc")
            if(EXISTS "${PROJECT_SOURCE_DIR}/data/audio-channel-layouts.json")
                file(INSTALL
                    DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data"
                    FILES "${PROJECT_SOURCE_DIR}/data/audio-channel-layouts.json"
                )
            else()
                message("${PROJECT_SOURCE_DIR}/data/audio-channel-layouts.json was not found")
            endif()
        ]])
		install(DIRECTORY DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data/log")
    endif() 
endif()
