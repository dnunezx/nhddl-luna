# Adapt MMCE directory completion to iomanX EOF in the private build copy.
# Keep the upstream checkout and all actual read-error paths unchanged.
if(NOT DEFINED DRIVER_DIR OR DRIVER_DIR STREQUAL "")
    message(FATAL_ERROR "DRIVER_DIR is required")
endif()
set(path "${DRIVER_DIR}/src/mmce_fs.c")
file(READ "${path}" source)
string(REPLACE "\r\n" "\n" source "${source}")
string(FIND "${source}" "int mmce_fs_dread(" start)
string(FIND "${source}" "int mmce_fs_getstat(" end)
if(start LESS 0 OR end LESS start)
    message(FATAL_ERROR "MMCE directory-read implementation changed; review the EOF fix")
endif()
math(EXPR length "${end} - ${start}")
string(SUBSTRING "${source}" ${start} ${length} dread)
set(fix [=[    // MMCE status 1 ends directory iteration. iomanX expects EOF as 0,
    // otherwise POSIX readdir reports an error after a successful scan.
    if (rdbuf[0x4] == 1) {
        mmce_sio2_unlock();
        return 0;
    }

]=])
string(FIND "${dread}" "${fix}" fixed)
if(NOT fixed LESS 0)
    return()
endif()
set(anchor "    if (rdbuf[0x4] != 0) {")
string(FIND "${dread}" "${anchor}" found)
if(found LESS 0)
    message(FATAL_ERROR "MMCE directory status handling changed; review the EOF fix")
endif()
string(REPLACE "${anchor}" "${fix}${anchor}" patched_dread "${dread}")
string(REPLACE "${dread}" "${patched_dread}" patched_source "${source}")
file(WRITE "${path}" "${patched_source}")
