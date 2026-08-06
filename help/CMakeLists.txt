#
# Copyright: 2024-2026 Erik Sunden <eriksunden85@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

install(CODE "set(PROJECT_SOURCE_DIR \"${PROJECT_SOURCE_DIR}\")")
install(CODE "set(PROJECT_BINARY_DIR \"${PROJECT_BINARY_DIR}\")")
install(CODE [[
	if(EXISTS "${PROJECT_BINARY_DIR}/help/CSlice_Pack_Installer.nsi")
		file(INSTALL
			DESTINATION "${CMAKE_INSTALL_PREFIX}"
			FILES "${PROJECT_BINARY_DIR}/help/CSlice_Pack_Installer.nsi"
		)
	else()
		message("${PROJECT_SOURCE_DIR}/help/CSlice_Pack_Installer.nsi was not found")
	endif()
	if(EXISTS "${PROJECT_BINARY_DIR}/help/CSlice_Pack_Zip.bat")
		file(INSTALL
			DESTINATION "${CMAKE_INSTALL_PREFIX}"
			FILES "${PROJECT_BINARY_DIR}/help/CSlice_Pack_Zip.bat"
		)
	else()
		message("${PROJECT_SOURCE_DIR}/help/CSlice_Pack_Zip.bat was not found")
	endif()
	if(EXISTS "${PROJECT_SOURCE_DIR}/LICENSE.txt")
		file(INSTALL
			DESTINATION "${CMAKE_INSTALL_PREFIX}"
			FILES "${PROJECT_SOURCE_DIR}/LICENSE.txt"
		)
	else()
		message("${PROJECT_SOURCE_DIR}/LICENSE.txt was not found")
	endif()
]])

if(BUILD_CRAFTKIT)
	install(CODE "set(PROJECT_SOURCE_DIR \"${PROJECT_SOURCE_DIR}\")")
	install(CODE "set(PROJECT_BINARY_DIR \"${PROJECT_BINARY_DIR}\")")
	install(CODE [[
		if(EXISTS "${PROJECT_BINARY_DIR}/help/Craftkit_Pack_Installer.nsi")
			file(INSTALL
				DESTINATION "${CMAKE_INSTALL_PREFIX}"
				FILES "${PROJECT_BINARY_DIR}/help/Craftkit_Pack_Installer.nsi"
			)
		else()
			message("${PROJECT_SOURCE_DIR}/help/Craftkit_Pack_Installer.nsi was not found")
		endif()
		if(EXISTS "${PROJECT_BINARY_DIR}/help/Craftkit_Pack_Zip.bat")
			file(INSTALL
				DESTINATION "${CMAKE_INSTALL_PREFIX}"
				FILES "${PROJECT_BINARY_DIR}/help/Craftkit_Pack_Zip.bat"
			)
		else()
			message("${PROJECT_SOURCE_DIR}/help/Craftkit_Pack_Zip.bat was not found")
		endif()
		if(EXISTS "${PROJECT_SOURCE_DIR}/LICENSE.txt")
			file(INSTALL
				DESTINATION "${CMAKE_INSTALL_PREFIX}"
				FILES "${PROJECT_SOURCE_DIR}/LICENSE.txt"
			)
		endif()
	]])
endif()
