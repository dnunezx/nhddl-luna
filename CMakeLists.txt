# LUNA 2026
cmake_minimum_required(VERSION 3.13)

include($ENV{PS2SDK}/ps2dev.cmake)

project(luna C ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Executable name
set(EE_BIN "luna_unc.elf")
set(EE_BIN_PKD "luna.elf")
# Diagnostics are restored only through the ignored local development patch.
if(ENABLE_PRINTF)
  message(FATAL_ERROR "Diagnostics are local-only. Configure production with ENABLE_PRINTF=OFF.")
endif()
option(LUNA_EMULATOR_BUILD "Enable behavior used only by emulator test builds" OFF)
option(LUNA_CAMERA_ANIMATIONS "Enable camera animations in library views" ON)
option(LUNA_CAMERA_MENU_PAN "Pan the library scene with the quick menu" ON)
option(LUNA_CAMERA_LIST_TURN "Turn the selected cover from List into Collections" ON)
option(LUNA_CAMERA_ORBIT_TRANSITION "Move Collections covers into Orbit" ON)
option(LUNA_ENABLE_PSXCORE "Build with the experimental PSXCore dependency" OFF)
if(LUNA_PSXCORE_DEVELOPMENT)
  message(FATAL_ERROR "The development launch harness is local-only and is not part of production.")
endif()
set(LUNA_PSXCORE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../psxcore" CACHE PATH
    "Path to the PSXCore source checkout")

if(LUNA_ENABLE_PSXCORE)
  if(NOT EXISTS "${LUNA_PSXCORE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "PSXCore is missing. Initialize the private psxcore submodule, set "
      "LUNA_PSXCORE_SOURCE_DIR, or build with LUNA_ENABLE_PSXCORE=OFF.")
  endif()
  add_subdirectory("${LUNA_PSXCORE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/psxcore")
endif()

# Source files
set(SOURCES
    src/main.c
    src/common.c
    src/runtime_locks.c
    src/options.c
    src/cheats.c
    src/cheat_storage.c
    src/favorites.c
    src/neutrino.c
    src/opl_ata.c
    src/opl_logo.c
    src/opl_pfs_vmc.c
    src/opl_vmc.c
    src/target.c
    src/storage.c
    src/forwarder.c
    src/homebrew_elf.c
    src/homebrew_launch.c
    src/homebrew_transfer.S

    # UI
    src/ui/gui.c
    src/ui/file_manager.c
    src/ui/ring_menu.c
    src/ui/cube_menu.c
    src/ui/appearance_menu.c
    src/ui/file_browser.c
    src/ui/file_operations.c
    src/ui/text_file.c
    src/vmc_create.c
    src/vmc_storage.c
    src/ui/options_menu.c
    src/ui/game_options.c
    src/ui/graphics.c
    src/ui/language.c
    src/ui/texture_budget.c
    src/ui/handoff.c
    src/ui/ambient.c
    src/ui/worker_lifecycle.c
  src/ui/navigation.c
  src/ui/case_page.c
  src/ui/view_state.c
  src/ui/appearance.c
  src/ui/art_cache.c
  src/ui/collection_art.c
    src/ui/view_classic.c
    src/ui/view_collection.c
    src/ui/collection_v2_pose.c
    src/ui/view_collection_v2.c
  src/ui/view_orbit.c
  src/ui/view_3d.c
  src/ui/view_scroll.c
  src/ui/ambient_orbs.c
  src/ui/neo.c
  src/ui/wallpapers.c
  src/ui/space_flight.c
    src/ui/pad.c

    # Devices
    src/devices/init.c
    src/devices/devices.c
    src/devices/mmce.c
    src/devices/udpfs.c
    src/devices/bdm.c
    src/devices/iso.c
    src/devices/hdl.c
    src/devices/title_id.c
)

include(irx.cmake)

# Get git version for build info
find_package(Git QUIET)
if(DEFINED LUNA_RELEASE_VERSION AND NOT LUNA_RELEASE_VERSION STREQUAL "")
  set(GIT_VERSION "${LUNA_RELEASE_VERSION}")
elseif(GIT_FOUND)
  execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags --exclude nightly
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
else()
  set(GIT_VERSION "unknown")
endif()

# Set compiler flags
add_definitions(-DGIT_VERSION=\"${GIT_VERSION}\")

if(LUNA_EMULATOR_BUILD)
  add_definitions(-DLUNA_EMULATOR_BUILD)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/ambient.adp
                 ${CMAKE_CURRENT_BINARY_DIR}/ambient.adp COPYONLY)
endif()

# Create the main executable
add_executable(${EE_BIN} ${SOURCES})
if(LUNA_CAMERA_ANIMATIONS)
  target_sources(${EE_BIN} PRIVATE src/ui/luna_motion.c src/ui/camera_transitions.c)
  target_compile_definitions(${EE_BIN} PRIVATE LUNA_CAMERA_ANIMATIONS=1
    LUNA_CAMERA_MENU_PAN=$<BOOL:${LUNA_CAMERA_MENU_PAN}>
    LUNA_CAMERA_LIST_TURN=$<BOOL:${LUNA_CAMERA_LIST_TURN}>
    LUNA_CAMERA_ORBIT_TRANSITION=$<BOOL:${LUNA_CAMERA_ORBIT_TRANSITION}>)
endif()

if(LUNA_ENABLE_PSXCORE)
  enable_language(ASM)
  target_sources(${EE_BIN} PRIVATE src/psxcore_launch.c src/psxcore_transfer.c src/psxcore_transfer.S src/psxcore_library.c src/psxcore_title.c src/psxcore_vmc.c src/psxcore_discset.c src/psxcore_discset_io.c)
  target_link_libraries(${EE_BIN} PSXCore::core)
  target_compile_definitions(${EE_BIN} PRIVATE LUNA_ENABLE_PSXCORE=1)
  target_link_options(${EE_BIN} PRIVATE -Wl,--defsym=luna_psxcore_heap_limit=1)
endif()

# Include directories
target_include_directories(${EE_BIN}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Custom linker script
separate_arguments(LINKER_FLAGS UNIX_COMMAND ${CMAKE_EXE_LINKER_FLAGS})
list(REMOVE_ITEM LINKER_FLAGS "-T$ENV{PS2SDK}/ee/startup/linkfile")
list(JOIN LINKER_FLAGS " " CMAKE_EXE_LINKER_FLAGS)
target_link_options(${EE_BIN}
    PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/linkfile
)
set_property(TARGET ${EE_BIN} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/linkfile")

# Link libraries
target_link_libraries(${EE_BIN}
    pad
    mc
    poweroff
    patches
    fileXio
    libdebug.a # Fatal startup error display; required on retail hardware.
    gskit_toolkit
    gskit
    dmakit
    png
    z
    m
    tiff
    ps2snd
)

# Set link flags for PS2
set_target_properties(${EE_BIN} PROPERTIES
    LINK_FLAGS "-s"
)

# Create packed binary
add_custom_command(
    TARGET ${EE_BIN}
    POST_BUILD
    COMMAND ${PS2DEV}/bin/ps2-packer
            ${EE_BIN}
            ${EE_BIN_PKD}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Packing ${EE_BIN} as ${EE_BIN_PKD}"
)
