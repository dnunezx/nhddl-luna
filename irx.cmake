# IRX files
set(IRX_FILES
    sio2man
    mcman
    mcserv
    fileXio
    iomanX
    poweroff
    freepad
    ps2dev9
    bdm
    bdmfs_fatfs
    ata_bd
    usbd_mini
    usbmass_bd_mini
    mx4sio_bd_mini
    iLinkman
    IEEE1394_bd_mini
    ps2hdd-bdm
    ps2fs
    audsrv
)

# Local IRX files
set(LOCAL_IRX_FILES
    mmceman
    smap
    ministack
    udpfs_ioman
)

file(GLOB_RECURSE MMCEMAN_BUILD_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/iop/mmceman/mmceman/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/iop/mmceman/mmceman/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/iop/mmceman/mmceman/*.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/iop/mmceman/mmceman/Makefile")

# mmceman
set(MMCEMAN_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/mmceman")
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/mmceman.irx
    DEPENDS ${MMCEMAN_BUILD_INPUTS} ${CMAKE_CURRENT_SOURCE_DIR}/irx.cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/mmceman-eof.cmake
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/mmceman/mmceman ${MMCEMAN_BUILD_DIR}
    COMMAND ${CMAKE_COMMAND} -DDRIVER_DIR=${MMCEMAN_BUILD_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/iop/mmceman-eof.cmake
    COMMAND make -C ${MMCEMAN_BUILD_DIR} clean DEBUG=0 LOCAL_UDPTTY=0
    COMMAND make -C ${MMCEMAN_BUILD_DIR} DEBUG=0 LOCAL_UDPTTY=0
    COMMAND ${CMAKE_COMMAND} -E rename
        ${MMCEMAN_BUILD_DIR}/irx/mmceman.irx
        ${CMAKE_CURRENT_BINARY_DIR}/mmceman.irx
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Building mmceman"
)


# smap, ministack, udpfs_ioman
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/smap.irx
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/smap
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/smap/irx/smap.irx
        ${CMAKE_CURRENT_BINARY_DIR}/smap.irx
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/smap
    COMMENT "Building smap"
)
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/ministack.irx
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/ministack
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/ministack/irx/ministack.irx
        ${CMAKE_CURRENT_BINARY_DIR}/ministack.irx
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/ministack
    COMMENT "Building ministack"
)
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/udpfs_ioman.irx
    COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/udpfs UDPFS_IOMAN=1
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/udpfs/irx/udpfs_ioman.irx
        ${CMAKE_CURRENT_BINARY_DIR}/udpfs_ioman.irx
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/iop/udpfs/udpfs
    COMMENT "Building udpfs"
)

foreach(IRX_FILE ${IRX_FILES})
    string(REPLACE "-" "_" irx_name_clean ${IRX_FILE})
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${irx_name_clean}_irx.c"
        COMMAND ${PS2SDK}/bin/bin2c ${PS2SDK}/iop/irx/${IRX_FILE}.irx
                "${CMAKE_CURRENT_BINARY_DIR}/${irx_name_clean}_irx.c"
                "${irx_name_clean}_irx"
        DEPENDS ${PS2SDK}/iop/irx/${IRX_FILE}.irx
        COMMENT "Converting ${IRX_FILE} with bin2c"
    )

    list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${irx_name_clean}_irx.c")
endforeach()
foreach(IRX_FILE ${LOCAL_IRX_FILES})
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${IRX_FILE}_irx.c"
        COMMAND ${PS2SDK}/bin/bin2c ${CMAKE_CURRENT_BINARY_DIR}/${IRX_FILE}.irx
                "${CMAKE_CURRENT_BINARY_DIR}/${IRX_FILE}_irx.c"
                "${IRX_FILE}_irx"
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${IRX_FILE}.irx
        COMMENT "Converting ${IRX_FILE} with bin2c"
    )

    list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${IRX_FILE}_irx.c")
endforeach()
