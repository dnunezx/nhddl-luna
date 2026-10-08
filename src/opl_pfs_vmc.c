// LUNA: map a PFS memory-card file before rebooting into the game environment.
// APA and PFS on-disk layouts follow ps2sdk's libapa.h and libpfs.h (AFL 2.0).
#include "opl_pfs_vmc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef PFS_VMC_HOST_TEST
#include <hdd-ioctl.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#endif

#define PFS_ZONE_SECTORS 16U
#define PFS_INODE_MAGIC 0x53454744U
#define APA_MAGIC 0x00415041U
#define APA_SUBPARTS 64U
#define PFS_DIRECT_BLOCKS 114U

typedef struct { uint32_t start, length; } ApaPart;
typedef struct { uint32_t number; uint16_t subpart, count; } PfsBlock;
typedef struct {
    uint32_t checksum, magic, next, prev;
    char id[32], rpwd[8], fpwd[8];
    uint32_t start, length;
    uint16_t type, flags;
    uint32_t nsub;
    uint8_t created[8];
    uint32_t main, number, modver, padding1[7];
    uint8_t padding2[128], mbr[256];
    ApaPart subs[APA_SUBPARTS];
} ApaHeader;
typedef struct {
    uint32_t checksum, magic;
    PfsBlock inode_block, next_segment, last_segment, unused;
    PfsBlock data[PFS_DIRECT_BLOCKS];
    uint16_t mode, attr, uid, gid;
    uint8_t atime[8], ctime[8], mtime[8];
    uint64_t size;
    uint32_t number_blocks, number_data, number_segdesg, subpart, reserved[4];
} PfsInode;

_Static_assert(sizeof(ApaHeader) == 1024, "APA header layout changed");
_Static_assert(sizeof(PfsInode) == 1024, "PFS inode layout changed");

static int read_hdd_sectors(uint32_t lba, uint32_t count, void *buffer)
{
    hddAtaTransfer_t request = { lba, count };
    return fileXioDevctl("hdd0:", HDIOC_READSECTOR, &request,
                         sizeof(request), buffer, count * 512) == 0 ? 0 : -1;
}

static int verify_sector(int fd, uint32_t file_sector, uint32_t disk_sector)
{
    uint8_t file_data[512], disk_data[512];
    if (lseek(fd, (off_t)file_sector * 512, SEEK_SET) < 0 ||
        read(fd, file_data, sizeof(file_data)) != sizeof(file_data) ||
        read_hdd_sectors(disk_sector, 1, disk_data) < 0)
        return -1;
    return memcmp(file_data, disk_data, sizeof(file_data)) == 0 ? 0 : -1;
}

int pfs_vmc_map_file(const char *path, const char *partition,
                     bd_fragment_t *extents, uint32_t *fragment_count, uint64_t *file_size)
{
    iox_stat_t partition_stat, file_stat;
    ApaHeader apa;
    PfsInode inode;
    ApaPart parts[APA_SUBPARTS + 1];

    uint64_t total_sectors = 0, first_file_sector = 0;
    unsigned int count, i, part_count;
    uint32_t checksum = 0;
    int fd = -1, zone_size, result = -1;

    if (!path || !partition || !extents || !fragment_count || !file_size ||

        strncmp(path, "pfs0:", 5) != 0 ||
        strncmp(partition, "hdd0:", 5) != 0)
        return -1;
    zone_size = fileXioDevctl("pfs0:", PDIOC_ZONESZ, NULL, 0, NULL, 0);
    if (zone_size != PFS_ZONE_SECTORS * 512) {
        printf("ERROR: APA VMC requires 8 KiB PFS zones (got %d)\n", zone_size);
        return -1;
    }
    if (fileXioGetStat(partition, &partition_stat) < 0 ||
        partition_stat.mode != APA_TYPE_PFS ||
        read_hdd_sectors(partition_stat.private_5, 2, &apa) < 0 ||
        apa.magic != APA_MAGIC || apa.type != APA_TYPE_PFS ||
        apa.start != partition_stat.private_5 || apa.nsub > APA_SUBPARTS)
        return -1;
    parts[0] = (ApaPart){ apa.start, apa.length };
    for (i = 0; i < apa.nsub; i++)
        parts[i + 1] = apa.subs[i];
    part_count = apa.nsub + 1;
    for (i = 0; i < part_count; i++) {
        if (parts[i].length < 2 ||
            (uint64_t)parts[i].start + parts[i].length > UINT32_MAX)
            return -1;
    }
    if (fileXioGetStat(path, &file_stat) < 0 ||
        file_stat.private_4 >= part_count || file_stat.size <= 0 ||
        file_stat.private_5 > parts[file_stat.private_4].length - 2 ||
        read_hdd_sectors(parts[file_stat.private_4].start +
                         file_stat.private_5, 2, &inode) < 0 ||
        inode.magic != PFS_INODE_MAGIC || inode.size != (uint32_t)file_stat.size ||
        (inode.size & 511) != 0 || inode.number_data < 2 ||
        inode.number_data > PFS_DIRECT_BLOCKS)
        return -1;
    for (i = 1; i < 256; i++)
        checksum += ((const uint32_t *)&inode)[i];
    if (checksum != inode.checksum)
        return -1;

    count = inode.number_data - 1;
    if (count > 64)
        return -1;

    for (i = 0; i < count; i++) {
        PfsBlock block = inode.data[i + 1];
        uint64_t sector, length, part_end;
        if (block.subpart >= part_count || block.count == 0)
            return -1;
        sector = (uint64_t)parts[block.subpart].start +
                 (uint64_t)block.number * PFS_ZONE_SECTORS;
        length = (uint64_t)block.count * PFS_ZONE_SECTORS;
        part_end = (uint64_t)parts[block.subpart].start +
                   parts[block.subpart].length;
        if (sector < parts[block.subpart].start || sector + length > part_end ||
            sector + length > UINT32_MAX)
            return -1;
        extents[i].sector = (uint32_t)sector;
        extents[i].count = (uint32_t)length;
        total_sectors += length;
    }
    if (total_sectors < inode.size / 512 ||
        total_sectors - inode.size / 512 >= PFS_ZONE_SECTORS)
        return -1;

    // Check both ends of every mapped extent against PFS before raw writes
    // become possible. This catches wrong partition and block-unit mappings.
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    for (i = 0; i < count; i++) {
        uint32_t extent_sectors = extents[i].count;
        uint32_t remaining = inode.size / 512 - first_file_sector;
        uint32_t used = extent_sectors < remaining ? extent_sectors : remaining;
        if (used == 0 ||
            verify_sector(fd, first_file_sector, extents[i].sector) < 0 ||
            verify_sector(fd, first_file_sector + used - 1,
                          extents[i].sector + used - 1) < 0)
            goto done;
        first_file_sector += used;
    }
    if (first_file_sector != inode.size / 512)
        goto done;

    *fragment_count = count;
    *file_size = inode.size;
    printf("APA VMC mapped: %s (%u extents)\n", path, count);
    result = 0;
done:
    close(fd);
    return result;
}
