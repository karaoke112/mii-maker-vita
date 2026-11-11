cmake_minimum_required(VERSION 3.13)

# Mii editor for PS Vita (and desktop, for fast UI iteration).
#
#   Vita:    cmake -S app -B build-vita -DPLATFORM_PSV=ON -DUSE_GXM=ON && cmake --build build-vita
#   Desktop: cmake -S app -B build-desktop -DPLATFORM_DESKTOP=ON && cmake --build build-desktop
#
# See docs/BUILDING.md.

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(BOREALIS_DIR ${REPO_ROOT}/third_party/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(mii_maker_vita C CXX)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_ALTER "0")
set(PSN_TITLE_ID "MIIV00001")
set(PSN_VERSION "00.10")
set(APP_TITLE "Mii Maker Vita")
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

add_subdirectory(${BOREALIS_LIBRARY} borealis)
add_subdirectory(${REPO_ROOT}/libmii libmii)
add_subdirectory(${REPO_ROOT}/libmii3d libmii3d)

file(GLOB_RECURSE APP_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
program_target(${PROJECT_NAME} "${APP_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE borealis mii mii3d)

if (PLATFORM_PSV)
    target_compile_definitions(${PROJECT_NAME} PRIVATE __PSV__)
    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")
    vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
    vita_create_vpk(${PROJECT_NAME}.vpk ${PSN_TITLE_ID} ${PROJECT_NAME}.self
        VERSION ${PSN_VERSION}
        NAME ${APP_TITLE}
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys sce_sys
             ${PROJECT_RESOURCES} resources
    )
elseif (PLATFORM_DESKTOP)
    add_custom_target(${PROJECT_NAME}.data
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_CURRENT_BINARY_DIR}/resources)
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data)
endif ()
