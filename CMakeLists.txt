cmake_minimum_required(VERSION 3.25 FATAL_ERROR)
cmake_policy(VERSION 3.25)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
get_filename_component(C_SLICE_SOURCE_PARENT ${CMAKE_SOURCE_DIR} DIRECTORY)
set(CMAKE_INSTALL_PREFIX "${C_SLICE_SOURCE_PARENT}/install" CACHE PATH "")

option(BUILD_WITH_VCPKG_SUPPORT "Global On/Off for Vcpkg usage" ON)
if(BUILD_WITH_VCPKG_SUPPORT)
    file(TO_CMAKE_PATH $ENV{VCPKG_ROOT} VCPKG_ROOT)
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    set(VCPKG_TARGET_ARCHITECTURE x64)
    set(VCPKG_LIBRARY_LINKAGE dynamic)
    set(VCPKG_CRT_LINKAGE dynamic)
    include(${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
    set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel RelWithDebInfo Release Debug)
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel Debug)
    set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release RelWithDebInfo MinSizeRel Debug)
endif()

project(c-slice LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(AUTO_TARGET_NAME "Generated Targets")
set(AUTO_SOURCE_NAME "Generated Files")
set_property(GLOBAL PROPERTY AUTOMOC_TARGETS_FOLDER ${AUTO_TARGET_NAME})
set_property(GLOBAL PROPERTY AUTORCC_TARGETS_FOLDER ${AUTO_TARGET_NAME})
set_property(GLOBAL PROPERTY AUTOGEN_TARGETS_FOLDER ${AUTO_TARGET_NAME})
set_property(GLOBAL PROPERTY AUTOGEN_SOURCE_GROUP ${AUTO_SOURCE_NAME})
set_property(GLOBAL PROPERTY AUTOMOC_SOURCE_GROUP ${AUTO_SOURCE_NAME})

set(QT_MIN_VERSION 6.6.0)
set(KF6_MIN_VERSION 6.0.0)

include(FeatureSummary)

find_package(ECM ${KF6_MIN_VERSION} NO_MODULE)
set_package_properties(ECM PROPERTIES TYPE REQUIRED
    URL "https://api.kde.org/ecm/"
    DESCRIPTION "extra cmake modules")
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})

if (MSVC)
include(${PROJECT_SOURCE_DIR}/src/sgct/support/cmake/common-compile-settings/platforms/msvc.cmake)
endif()

include(${PROJECT_SOURCE_DIR}/src/sgct/support/cmake/common-compile-settings/common-compile-settings.cmake)
include(${PROJECT_SOURCE_DIR}/src/sgct/support/cmake/set_compile_options.cmake)

find_package(Qt6Core ${QT_MIN_VERSION})
set_package_properties(Qt6Core PROPERTIES TYPE REQUIRED)

find_package(Qt6Gui ${QT_MIN_VERSION})
set_package_properties(Qt6Gui PROPERTIES TYPE REQUIRED)

find_package(Qt6Qml ${QT_MIN_VERSION})
set_package_properties(Qt6Qml PROPERTIES TYPE REQUIRED)

find_package(Qt6Quick ${QT_MIN_VERSION})
set_package_properties(Qt6Quick PROPERTIES TYPE REQUIRED)

find_package(Qt6QuickControls2 ${QT_MIN_VERSION})
set_package_properties(Qt6QuickControls2 PROPERTIES TYPE REQUIRED)

find_package(Breeze)
set_package_properties(Breeze PROPERTIES TYPE RECOMMENDED
    URL "https://api.kde.org/frameworks/breeze-icons/html/index.html")

find_package(KF6ColorScheme ${KF6_MIN_VERSION})
set_package_properties(KF6ColorScheme PROPERTIES TYPE REQUIRED
    URL "https://invent.kde.org/frameworks/kcolorscheme")

find_package(KF6Config ${KF6_MIN_VERSION})
set_package_properties(KF6Config PROPERTIES TYPE REQUIRED
    URL "https://api.kde.org/frameworks/kconfig/html/index.html")

find_package(KF6CoreAddons ${KF6_MIN_VERSION})
set_package_properties(KF6CoreAddons PROPERTIES TYPE REQUIRED
    URL "https://api.kde.org/frameworks/kcoreaddons/html/index.html")

find_package(KF6DocTools ${KF6_MIN_VERSION})
set_package_properties(KF6DocTools PROPERTIES
    DESCRIPTION "https://api.kde.org/frameworks/kdoctools/html/index.html")

find_package(KF6FileMetaData ${KF6_MIN_VERSION})
set_package_properties(KF6FileMetaData PROPERTIES TYPE REQUIRED
    URL "https://api.kde.org/frameworks/kfilemetadata/html/index.html")

find_package(KF6I18n ${KF6_MIN_VERSION})
set_package_properties(KF6I18n PROPERTIES TYPE REQUIRED
    URL "https://api.kde.org/frameworks/ki18n/html/index.html")

find_package(KF6IconThemes ${KF6_MIN_VERSION})
set_package_properties(KF6IconThemes PROPERTIES TYPE REQUIRED
    URL "https://api.kde.org/frameworks/kiconthemes/html/index.html")

find_package(KF6KIO ${KF6_MIN_VERSION})
set_package_properties(KF6KIO PROPERTIES TYPE REQUIRED
    URL "https://api.kde.org/frameworks/kio/html/index.html")

find_package(KF6Kirigami ${KF6_MIN_VERSION})
set_package_properties(KF6Kirigami PROPERTIES TYPE REQUIRED
    URL "https://api.kde.org/frameworks/kirigami/html/index.html")

find_package(KF6WindowSystem ${KF6_MIN_VERSION})
set_package_properties(KF6WindowSystem PROPERTIES TYPE REQUIRED
    URL "https://api.kde.org/frameworks/kwindowsystem/html/index.html")
    
find_package(KF6XmlGui ${KF6_MIN_VERSION})
set_package_properties(KF6XmlGui PROPERTIES TYPE REQUIRED
    URL "https://api.kde.org/frameworks/kxmlgui/html/index.html")

include(KDEInstallDirs)
include(ECMInstallIcons)
include(ECMAddAppIcon)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMFindQmlModule)
include(ECMQmlModule)

find_package(FFmpeg REQUIRED)

option(BUILD_CSLICE_WITH_WUFFS "Global On/Off for Wuffs image decoding support" ON)
set(CSLICE_WUFFS_ROOT "D:/Google/wuffs" CACHE PATH "Path to the Wuffs source")
set(CSLICE_WUFFS_RELEASE_DIR "${CSLICE_WUFFS_ROOT}/release/c")
if(BUILD_CSLICE_WITH_WUFFS AND NOT EXISTS "${CSLICE_WUFFS_RELEASE_DIR}/wuffs-unsupported-snapshot.c")
    message(FATAL_ERROR "Wuffs support requires CSLICE_WUFFS_ROOT; expected ${CSLICE_WUFFS_RELEASE_DIR}/wuffs-unsupported-snapshot.c")
endif()

if(BUILD_WITH_VCPKG_SUPPORT)
    message(STATUS "Using vcpkg toolchain from: ${CMAKE_TOOLCHAIN_FILE} to build C-Play")
    message(STATUS "Remember to run/install this: vcpkg install minizip libpng tinyxml2\n")
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

option(CSLICE_INSTALL_DEPENDENCIES "Global On/Off for all CSLICE_INSTALL_* dependencies" OFF)

option(CSLICE_INSTALL_BUILD_BY_DEFAULT "Include INSTALL during default build" ON)
if(CSLICE_INSTALL_BUILD_BY_DEFAULT)
    set(CMAKE_VS_INCLUDE_INSTALL_TO_DEFAULT_BUILD 1)
else()
    set(CMAKE_VS_INCLUDE_INSTALL_TO_DEFAULT_BUILD 0)
endif()

add_subdirectory(data)
add_subdirectory(help)

option(BUILD_CRAFTKIT "Create combined C-Slice + C-Stitch package" OFF)
if(BUILD_CRAFTKIT)
    set(C_STITCH_BIN_DIR "" CACHE PATH "Path to C-Stitch binary install directory")
    mark_as_advanced(C_STITCH_BIN_DIR)
endif()

add_subdirectory(src)
