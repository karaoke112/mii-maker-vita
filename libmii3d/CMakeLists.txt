cmake_minimum_required(VERSION 3.13)
project(libmii3d C)

# libmii (formats) is needed; add it unless the parent project already did.
if (NOT TARGET mii)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libmii ${CMAKE_CURRENT_BINARY_DIR}/libmii)
endif ()
find_package(ZLIB REQUIRED)

add_library(mii3d STATIC
    src/res.c
    src/gx2_untile.c
    src/raster.c
    src/charmodel.c
)
target_include_directories(mii3d PUBLIC include)
target_link_libraries(mii3d PUBLIC mii ZLIB::ZLIB)
set_target_properties(mii3d PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mii3d PRIVATE -Wall -Wextra -Wpedantic -O2)
    if (NOT WIN32)
        target_link_libraries(mii3d PUBLIC m)
    endif ()
endif ()

option(LIBMII3D_BUILD_TOOLS "Build miirender" ${PROJECT_IS_TOP_LEVEL})
if (LIBMII3D_BUILD_TOOLS AND NOT VITA)
    add_executable(miirender tools/miirender.c)
    target_link_libraries(miirender PRIVATE mii3d)

    enable_testing()
    add_executable(test_mii3d tests/test_mii3d.c)
    target_link_libraries(test_mii3d PRIVATE mii3d)
    add_test(NAME test_mii3d COMMAND test_mii3d)
endif ()
