cmake_minimum_required(VERSION 3.22)
project(agent_pal3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DEFAULT_OPENPAL4_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../OpenPAL4")
if(DEFINED ENV{OPENPAL4_ROOT} AND NOT "$ENV{OPENPAL4_ROOT}" STREQUAL "")
    set(DEFAULT_OPENPAL4_ROOT "$ENV{OPENPAL4_ROOT}")
endif()
set(OPENPAL4_ROOT "${DEFAULT_OPENPAL4_ROOT}" CACHE PATH "Path to OpenPAL4 repository")
if(NOT OPENPAL4_ROOT)
    set(OPENPAL4_ROOT "${DEFAULT_OPENPAL4_ROOT}")
endif()

option(PAL3_HEADLESS_GLES "Use built-in headless GLES3 state/raster shim" ON)
set(PAL4_ENABLE_GLES ON CACHE BOOL "Enable shared OpenPAL4 GLES3 renderer" FORCE)
set(PAL4_MODERN_GAMEPAD ON CACHE BOOL "Enable camera-relative analog controls" FORCE)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-inconsistent-missing-override)
endif()

find_package(ZLIB REQUIRED)
find_package(Iconv REQUIRED)

if(NOT TARGET GLESv2)
    add_library(GLESv2 STATIC src/headless_gles3.cpp)
    target_include_directories(GLESv2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

if(NOT TARGET pal4_portable_gles)
    add_subdirectory(${OPENPAL4_ROOT}/fresh/ports/runtime ${CMAKE_CURRENT_BINARY_DIR}/openpal4_runtime EXCLUDE_FROM_ALL)
endif()

# 1. PAL3 .cpk Archive Reader & Builder (RST\x1A header, 28-byte unencrypted table, GBK CRC32 tree, raw/miniLZO)
add_library(pal3_portable_cpk STATIC src/cpk_package.cpp)
target_include_directories(pal3_portable_cpk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENPAL4_ROOT}/fresh/third_party/minilzo-2.09)
target_link_libraries(pal3_portable_cpk PUBLIC pal4_package_lzo ZLIB::ZLIB)
target_compile_features(pal3_portable_cpk PUBLIC cxx_std_20)

# 2. PAL3 .scn Scene, .nav Dual-Layer Tile Navigation & A* Pathfinder, and .pol / .cvd / .mv3 3D Models
add_library(pal3_portable_scn_nav_model STATIC src/scn_nav_model.cpp)
target_include_directories(pal3_portable_scn_nav_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pal3_portable_scn_nav_model PUBLIC pal4_portable_scene pal4_portable_world)
target_compile_features(pal3_portable_scn_nav_model PUBLIC cxx_std_20)

# 3. PAL3 .sce Bytecode Script Assembler & Virtual Machine (SceVm, FOP accumulator, serial/parallel run_mode)
add_library(pal3_portable_sce_vm STATIC src/sce_vm.cpp)
target_include_directories(pal3_portable_sce_vm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pal3_portable_sce_vm PUBLIC Iconv::Iconv)
target_compile_features(pal3_portable_sce_vm PUBLIC cxx_std_20)

# 4. PAL3 Database, Favorability, Demon Sword & Semi-Realtime ATB + Five-Element Taiji Formation Combat
add_library(pal3_portable_database_combat STATIC src/database_combat.cpp
    ${OPENPAL4_ROOT}/fresh/src/random_number_generator.cpp)
target_include_directories(pal3_portable_database_combat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENPAL4_ROOT}/fresh/include)
target_compile_features(pal3_portable_database_combat PUBLIC cxx_std_20)

# 5. PAL3 Signature Minigames (Antique Appraisal APPREntry, Night Encampment ENCAMP_Entry, Skiing SKEE_Entry, YuJian)
add_library(pal3_portable_minigames STATIC src/minigames.cpp)
target_include_directories(pal3_portable_minigames PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pal3_portable_minigames PUBLIC pal3_portable_database_combat)
target_compile_features(pal3_portable_minigames PUBLIC cxx_std_20)

# 6. Unified PAL3 Portable Runtime Host (`pal3::portable::ChapterHost`)
add_library(pal3_portable_chapter_host STATIC src/chapter_host.cpp)
target_include_directories(pal3_portable_chapter_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENPAL4_ROOT}/fresh/include)
target_link_libraries(pal3_portable_chapter_host PUBLIC
    pal3_portable_cpk
    pal3_portable_scn_nav_model
    pal3_portable_sce_vm
    pal3_portable_database_combat
    pal3_portable_minigames
    pal4_portable_gles
    pal4_portable_world
    pal4_portable_movement
    GLESv2)
target_compile_features(pal3_portable_chapter_host PUBLIC cxx_std_20)

# 7. Headless JSON-Lines Agent Host (`agent-pal3-host`)
add_executable(agent-pal3-host src/agent_pal3_host.cpp)
target_link_libraries(agent-pal3-host PRIVATE pal3_portable_chapter_host)
target_compile_features(agent-pal3-host PRIVATE cxx_std_20)

include(CTest)
if(BUILD_TESTING)
    add_executable(pal3_portable_cpk_package_tests tests/cpk_package_tests.cpp)
    target_link_libraries(pal3_portable_cpk_package_tests PRIVATE pal3_portable_cpk)
    add_test(NAME pal3_portable_cpk_package_contract COMMAND pal3_portable_cpk_package_tests)

    add_executable(pal3_portable_scn_nav_model_tests tests/scn_nav_model_tests.cpp)
    target_link_libraries(pal3_portable_scn_nav_model_tests PRIVATE pal3_portable_scn_nav_model)
    add_test(NAME pal3_portable_scn_nav_model_contract COMMAND pal3_portable_scn_nav_model_tests)

    add_executable(pal3_portable_sce_vm_tests tests/sce_vm_tests.cpp)
    target_link_libraries(pal3_portable_sce_vm_tests PRIVATE pal3_portable_sce_vm)
    add_test(NAME pal3_portable_sce_vm_contract COMMAND pal3_portable_sce_vm_tests)

    add_executable(pal3_portable_combat_minigame_tests tests/combat_minigame_tests.cpp)
    target_link_libraries(pal3_portable_combat_minigame_tests PRIVATE
        pal3_portable_database_combat pal3_portable_minigames)
    add_test(NAME pal3_portable_combat_minigame_contract COMMAND pal3_portable_combat_minigame_tests)

    add_executable(pal3_chapter_host_integration_tests tests/chapter_host_integration_tests.cpp)
    target_link_libraries(pal3_chapter_host_integration_tests PRIVATE pal3_portable_chapter_host)
    add_test(NAME pal3_chapter_host_integration_contract COMMAND pal3_chapter_host_integration_tests)

    find_program(NODE_EXECUTABLE node)
    if(NODE_EXECUTABLE)
        add_test(
            NAME agent_pal3_headless_bench_normal
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-headless-bench.mjs $<TARGET_FILE:agent-pal3-host> --mode normal
        )
        add_test(
            NAME agent_pal3_headless_bench_pure
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-headless-bench.mjs $<TARGET_FILE:agent-pal3-host> --mode pure
        )
    endif()
endif()
