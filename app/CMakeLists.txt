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

# Small fixes to borealis, applied to the submodule's working tree (once;
# a patch that is already in is detected by applying it in reverse).
foreach (patch ${CMAKE_CURRENT_SOURCE_DIR}/patches/borealis-psv-sticks.patch
               ${CMAKE_CURRENT_SOURCE_DIR}/patches/borealis-crisp-text.patch
               ${CMAKE_CURRENT_SOURCE_DIR}/patches/borealis-psv-time-only.patch
               ${CMAKE_CURRENT_SOURCE_DIR}/patches/borealis-hint-touch.patch)
    execute_process(COMMAND git apply --reverse --check ${patch}
        WORKING_DIRECTORY ${BOREALIS_DIR} RESULT_VARIABLE already OUTPUT_QUIET ERROR_QUIET)
    if (NOT already EQUAL 0)
        execute_process(COMMAND git apply ${patch} WORKING_DIRECTORY ${BOREALIS_DIR} RESULT_VARIABLE failed)
        if (failed)
            message(FATAL_ERROR "Could not apply ${patch} to borealis")
        endif ()
        message(STATUS "Applied ${patch}")
    endif ()
endforeach ()

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

# QR codes: Project Nayuki's generator (MIT), Daniel Beer's quirc (ISC),
# stb_image_write (public domain); see THIRD_PARTY_NOTICES.md
set(QR_DIR ${REPO_ROOT}/third_party)
add_library(qrlibs STATIC
    ${QR_DIR}/qrcodegen/qrcodegen.c
    ${QR_DIR}/quirc/quirc.c ${QR_DIR}/quirc/decode.c ${QR_DIR}/quirc/identify.c ${QR_DIR}/quirc/version_db.c)
target_include_directories(qrlibs PUBLIC ${QR_DIR}/qrcodegen ${QR_DIR}/quirc ${QR_DIR}/stb)
set_target_properties(qrlibs PROPERTIES C_STANDARD 99)

file(GLOB_RECURSE APP_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
program_target(${PROJECT_NAME} "${APP_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE borealis mii mii3d qrlibs)

if (PLATFORM_PSV)
    target_compile_definitions(${PROJECT_NAME} PRIVATE __PSV__)
    # GPU previews: the console's shader compiler (libshacccg.suprx), loaded
    # at run time when present, so the import is weak
    target_link_libraries(${PROJECT_NAME} PRIVATE SceShaccCg_stub_weak)
    # QR code scanning with the back camera
    target_link_libraries(${PROJECT_NAME} PRIVATE SceCamera_stub)
    # sending Miis to another Vita (ad-hoc wireless)
    target_link_libraries(${PROJECT_NAME} PRIVATE ScePspnetAdhoc_stub SceNet_stub SceSysmodule_stub SceAppUtil_stub
        SceCommonDialog_stub)
    # background music
    target_link_libraries(${PROJECT_NAME} PRIVATE SceAudio_stub)
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
