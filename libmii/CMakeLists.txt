cmake_minimum_required(VERSION 3.13)
project(libmii C)

add_library(mii STATIC
    src/mii_common.c
    src/mii_switch.c
    src/mii_ver3.c
    src/mii_wii.c
    src/mii_qr.c
    src/mii_nfdb.c
    src/mii_editor.c
)
target_include_directories(mii PUBLIC include)
set_target_properties(mii PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mii PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Host-only bits: CLI tool and tests. Skipped when cross-compiling for Vita.
option(LIBMII_BUILD_TOOLS "Build miitool and tests" ${PROJECT_IS_TOP_LEVEL})
if(LIBMII_BUILD_TOOLS AND NOT VITA)
    add_executable(miitool tools/miitool.c)
    target_link_libraries(miitool PRIVATE mii)

    enable_testing()
    add_executable(test_mii tests/test_mii.c)
    target_link_libraries(test_mii PRIVATE mii)
    target_compile_definitions(test_mii PRIVATE
        VECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors")
    add_test(NAME test_mii COMMAND test_mii)
endif()
