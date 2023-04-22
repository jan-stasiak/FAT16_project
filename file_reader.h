#ifndef FAT_DANTE_FILE_READER_H
#define FAT_DANTE_FILE_READER_H

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#define EOC_FAT_16 0xFFF8

#define BAD_CLUSTER_FAT_16 0xFFF7

struct clusters_chain_t {
    uint32_t *clusters;
    size_t size;
};

struct date_t {
    uint16_t day: 5;
    uint16_t month: 4;
    uint16_t year: 7;
};

struct my_time_t {
    uint16_t seconds: 5;
    uint16_t minutes: 6;
    uint16_t hours: 5;
};


struct __attribute__((__packed__)) SFN {
    unsigned char filename[8];
    unsigned char extension[3];
    uint8_t file_attributes;
    uint8_t reserved;
    uint8_t file_creation_time;
    struct my_time_t creation_time;
    struct date_t creation_date;
    uint16_t access_date;
    uint16_t high_order_address_of_first_cluster;
    struct my_time_t modified_time;
    struct date_t modified_date;
    uint16_t low_order_address_of_first_cluster;
    uint32_t size;
};

struct time_t {
    uint8_t hour: 5;
    uint8_t minutes: 6;
    uint8_t seconds: 5;
};


struct fat_super_t {
    char unused[3]; //Assembly code instructions to jump to boot code (mandatory in bootable partition)
    char name[8]; //OEM name in ASCII
    uint16_t bytes_per_sector; //Bytes per sector (512, 1024, 2048, or 4096)
    uint8_t sectors_per_clusters; //Sectors per cluster (Must be a power of 2 and cluster size must be <=32 KB)
    uint16_t size_of_reserved_area; //Size of reserved area, in sectors
    uint8_t number_of_fats; //Number of FATs (usually 2)
    uint16_t maximum_number_of_files; //Maximum number of files in the root directory (FAT12/16; 0 for FAT32)
    uint16_t number_of_sectors; //Number of sectors in the file system; if 2 B is not large enough, set to 0 and use 4 B value in bytes 32-35 below
    uint8_t media_type; //Media type (0xf0=removable disk, 0xf8=fixed disk)
    uint16_t size_of_fat; //Size of each FAT, in sectors, for FAT12/16; 0 for FAT32
    uint16_t sectors_per_track; //Sectors per track in storage device
    uint16_t number_of_heads; //Number of heads in storage device
    uint32_t number_of_sectors_before_partition; //Number of sectors before the start partition
    uint32_t number_of_sectors_in_filesystem; //Number of sectors in the file system; this field will be 0 if the 2B field above(bytes 19 - 20) is non - zero
    uint8_t drive_number; //BIOS INT 13h(low level disk services) drive number
    uint8_t unused_1; //Not used
    uint8_t boot_signature; //Extended boot signature to validate next three fields (0x29)
    uint32_t serial_number; //Volume serial number
    char label[11];  //Volume label, in ASCII
    char type[8];  //File system type level, in ASCII. (Generally "FAT", "FAT12", or "FAT16")
    uint8_t unused_2[448]; //Not used
    uint16_t signature; //Signature value (0xaa55)
}__attribute__ (( packed ));

struct disk_t {
    FILE *disk;
};

struct disk_t *disk_open_from_file(const char *volume_file_name);

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read);

int disk_close(struct disk_t *pdisk);


struct volume_t {
    struct disk_t *disk;
    struct fat_super_t super;
    int16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t sectors_per_fat;
    uint16_t root_dir_capacity;

    uint16_t total_sectors;
    uint32_t fat_size;
    uint16_t root_dir_sectors;
    uint16_t first_data_sector;
    uint16_t first_fat_sector;
    uint32_t data_sectors;
    uint32_t total_clusters;
    uint16_t bytes_per_cluster;
};

struct volume_t *fat_open(struct disk_t *pdisk, uint32_t first_sector);

int fat_close(struct volume_t *pvolume);


struct file_t {
    unsigned char filename[8];
    unsigned char extension[3];
    uint8_t file_attributes;
    uint8_t reserved;
    uint8_t file_creation_time;
    struct my_time_t creation_time;
    struct date_t creation_date;
    uint16_t access_date;
    uint16_t high_order_address_of_first_cluster;
    struct my_time_t modified_time;
    struct date_t modified_date;
    uint16_t low_order_address_of_first_cluster;
    uint32_t size;

    struct clusters_chain_t *chain;
    struct volume_t *volume;
    struct disk_t *disk;
    uint32_t file_offset;
    uint32_t cluster_offset;
    bool end_of_file;
};

struct file_t *file_open(struct volume_t *pvolume, const char *file_name);

int file_close(struct file_t *stream);

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);

int32_t file_seek(struct file_t *stream, int32_t offset, int whence);


struct dir_t {
    unsigned char filename[8];
    unsigned char extension[3];
    uint8_t file_attributes;
    uint8_t reserved;
    uint8_t file_creation_time;
    struct my_time_t creation_time;
    struct date_t creation_date;
    uint16_t access_date;
    uint16_t high_order_address_of_first_cluster;
    struct my_time_t modified_time;
    struct date_t modified_date;
    uint16_t low_order_address_of_first_cluster;
    uint32_t size;

    struct clusters_chain_t *chain;
    struct volume_t *volume;
    struct disk_t *disk;
    uint32_t file_offset;
    uint32_t cluster_offset;
    uint32_t dir_offset;
    bool end_of_file;
};
struct dir_entry_t {
    char name[20];
    size_t size;
    uint8_t is_archived: 1;
    uint8_t is_readonly: 1;
    uint8_t is_system: 1;
    uint8_t is_hidden: 1;
    uint8_t is_directory: 1;

    struct fat_super_t super;
};

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path);

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry);

int dir_close(struct dir_t *pdir);

#endif //FAT_DANTE_FILE_READER_H
