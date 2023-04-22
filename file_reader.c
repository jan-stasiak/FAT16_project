#include "file_reader.h"

#define SET_ERRNO(x) errno = x


struct disk_t *disk_open_from_file(const char *volume_file_name) {
    if (volume_file_name == NULL) {
        SET_ERRNO(EFAULT);
        return NULL;
    }
    FILE *fp = fopen(volume_file_name, "rb");
    if (fp == NULL) {
        SET_ERRNO(ENOENT);
        return NULL;
    }

    struct disk_t *disk = malloc(1 * sizeof(struct disk_t));
    if (disk == NULL) {
        fclose(fp);
        SET_ERRNO(ENOMEM);
        return NULL;
    }

    disk->disk = fp;
    return disk;
}

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read) {
    if (pdisk == NULL || pdisk->disk == NULL || buffer == NULL) {
        SET_ERRNO(EFAULT);
        return -1;
    }

    fseek(pdisk->disk, first_sector * 512, SEEK_SET);
    unsigned long readed_sectors = fread(buffer, 512, sectors_to_read, pdisk->disk);
    fseek(pdisk->disk, 0, SEEK_SET);

    if (readed_sectors != (unsigned int) sectors_to_read) {
        SET_ERRNO(ERANGE);
        return -1;
    }

    return sectors_to_read;
}

int disk_close(struct disk_t *pdisk) {
    if (pdisk == NULL) {
        SET_ERRNO(EFAULT);
        return -1;
    }

    fclose(pdisk->disk);
    free(pdisk);

    return 0;
}


struct volume_t *fat_open(struct disk_t *pdisk, uint32_t first_sector) {
    if (pdisk == NULL || pdisk->disk == NULL) {
        SET_ERRNO(EFAULT);
        return NULL;
    }
    struct volume_t *volume = malloc(1 * sizeof(struct volume_t));
    if (volume == NULL) {
        SET_ERRNO(ENOMEM);
        return NULL;
    }

    int res = disk_read(pdisk, first_sector, &volume->super, 1);
    if (res != 1) {
        free(volume);
        SET_ERRNO(EINVAL);
        return NULL;
    }

    if (volume->super.signature != 0xAA55) {
        free(volume);
        SET_ERRNO(EINVAL);
        return NULL;
    }

    volume->total_sectors = (volume->super.number_of_sectors == 0) ? volume->super.number_of_sectors_in_filesystem
                                                                   : volume->super.number_of_sectors;
    volume->fat_size = volume->super.size_of_fat;
    volume->root_dir_sectors = ((volume->super.maximum_number_of_files * 32) + (volume->super.bytes_per_sector - 1)) /
                               volume->super.bytes_per_sector;
    volume->first_data_sector =
            volume->super.size_of_reserved_area + (volume->super.number_of_fats * volume->super.size_of_fat) +
            volume->root_dir_sectors;
    volume->root_dir_capacity = volume->root_dir_sectors * volume->super.bytes_per_sector;
    volume->first_fat_sector = volume->super.size_of_reserved_area;
    volume->data_sectors = volume->total_sectors -
                           (volume->super.size_of_reserved_area +
                            (volume->super.number_of_fats * volume->super.size_of_fat) +
                            volume->root_dir_sectors);
    volume->total_clusters = volume->total_sectors / volume->super.sectors_per_clusters;
    volume->disk = pdisk;

    return volume;
}

int fat_close(struct volume_t *pvolume) {
    if (pvolume == NULL) {
        SET_ERRNO(EFAULT);
        return -1;
    }

    free(pvolume);

    return 0;
}


struct clusters_chain_t *get_chain_fat16(struct volume_t *volume, size_t size, uint16_t first_cluster) {
    if (!volume || size == 0) return NULL;

    struct clusters_chain_t *temp = malloc(sizeof(struct clusters_chain_t));
    void *buffer = calloc(volume->super.size_of_fat, volume->super.bytes_per_sector);
    if (!buffer) {
        free(temp);
        return NULL;
    }

    disk_read(volume->disk, volume->first_fat_sector, buffer, volume->super.size_of_fat);

    size_t how_many_clusters = 0;
    uint16_t next_index = first_cluster;
    while (1) {
        if (next_index >= EOC_FAT_16) {
            break;
        } else if (next_index == BAD_CLUSTER_FAT_16) {
            free(buffer);
            free(temp->clusters);
            free(temp);
            return NULL;
        }
        next_index = ((uint16_t *) buffer)[(uint16_t) next_index];
        how_many_clusters++;
    }


    temp->clusters = malloc((how_many_clusters + 2) * sizeof(uint32_t));

    temp->clusters[0] = first_cluster;
    temp->size = 0;


    while (1) {
        if (temp->clusters[temp->size] >= EOC_FAT_16) {
            free(buffer);
            return temp;
        } else if (temp->clusters[temp->size] == BAD_CLUSTER_FAT_16) {
            free(buffer);
            free(temp->clusters);
            free(temp);
            return NULL;
        }
        temp->clusters[temp->size + 1] = ((uint16_t *) buffer)[(uint16_t) temp->clusters[(uint16_t) temp->size]];
        temp->size++;
    }
}


struct file_t *file_open(struct volume_t *pvolume, const char *file_name) {
    if (pvolume == NULL || file_name == NULL) {
        SET_ERRNO(EFAULT);
        return NULL;
    }
    struct file_t *file = calloc(1, sizeof(struct file_t));
    if (file == NULL) {
        SET_ERRNO(ENOMEM);
        return NULL;
    }

    uint8_t *buffer = (uint8_t *) calloc(1, sizeof(uint8_t) * pvolume->root_dir_capacity *
                                            pvolume->super.bytes_per_sector);
    if (buffer == NULL) {
        SET_ERRNO(ENOMEM);
        free(file);
        return NULL;
    }

    int root_dir = pvolume->super.size_of_reserved_area + (pvolume->super.size_of_fat) * pvolume->super.number_of_fats;
    if (disk_read(pvolume->disk, root_dir, buffer, pvolume->root_dir_sectors) !=
        pvolume->root_dir_sectors) {
        free(file);
        free(buffer);
        SET_ERRNO(EINVAL);
        return NULL;
    }

    unsigned char name[14] = {'\0'};


    for (int i = 0; i < pvolume->super.maximum_number_of_files; i++) {
        struct file_t *temp = (struct file_t *) ((char *) buffer + i * 32);
        for (int j = 0; j < 14; ++j) name[j] = 0;
        memcpy(name, temp->filename, 8);
        if (name[0] == 0x0 || name[0] == 0xE5 || name[0] == 0x2E) {
            continue;
        }

        int counter = -1;
        for (int j = 0; j < 13; ++j) {
            if (!isalpha(name[j])) {
                if (counter == -1) {
                    name[j] = '.';
                    counter++;
                } else {
                    name[j] = temp->extension[counter++];
                }
            }
            if (counter == 3) {
                name[j + 1] = '\0';
                break;
            }
        }
        if (temp->extension[0] == ' ') {
            name[8] = '\0';
        }

        char *ptr = (char *) name;
        bool is_sim = true;
        for (int j = 0; j < (int) strlen(file_name); ++j) {
            if (*(ptr + j) != *(file_name + j)) {
                is_sim = false;
                break;
            }
        }


        if (is_sim == true) {
            if ((temp->file_attributes & 0x10) == 0x10 || (temp->file_attributes & 0x08) == 0x08) {
                SET_ERRNO(EISDIR);
                free(buffer);
                free(file);
                return NULL;
            }
            memcpy(file, temp, sizeof(struct file_t));
            file->chain = get_chain_fat16(pvolume, file->size, file->low_order_address_of_first_cluster);
            file->volume = pvolume;
            file->disk = pvolume->disk;
            file->file_offset = 0;
            file->cluster_offset = 0;
            file->end_of_file = false;
            free(buffer);
            return file;
        }
    }

    SET_ERRNO(ENOENT);
    free(buffer);
    free(file);
    return NULL;
}

int file_close(struct file_t *stream) {
    if (stream == NULL) {
        SET_ERRNO(EFAULT);
        return -1;
    }

    if (stream != NULL) {
        free(stream->chain->clusters);
        free(stream->chain);
        free(stream);
    }

    return 0;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
    if (ptr == NULL || size == 0 || nmemb == 0 || stream == NULL) {
        SET_ERRNO(EFAULT);
        return -1;
    }

    if (stream->end_of_file == true) {
        return 0;
    }
    size_t how_many_elements = 0;
    uint8_t *buffer = calloc(stream->volume->super.sectors_per_clusters, 512);
    uint32_t first_sector =
            ((stream->chain->clusters[stream->file_offset] - 2) * stream->volume->super.sectors_per_clusters) +
            stream->volume->first_data_sector;
    if (disk_read(stream->volume->disk, first_sector, buffer, stream->volume->super.sectors_per_clusters) == -1) {
        free(buffer);
        return how_many_elements;
    }

    size_t file_offset = stream->file_offset;
    size_t cluster_offset = stream->cluster_offset;
    size_t buffer_offset = 0;

    for (size_t i = 0; i < nmemb; ++i) {
        if (stream->file_offset * stream->volume->super.sectors_per_clusters * stream->volume->super.bytes_per_sector +
            stream->cluster_offset >= stream->size) {
            free(buffer);
            return 0;
        }
        if (stream->size >=
            ((file_offset * stream->volume->super.sectors_per_clusters * stream->volume->super.bytes_per_sector) +
             cluster_offset + size)) {
            if (cluster_offset < stream->volume->super.sectors_per_clusters * stream->volume->super.bytes_per_sector) {
                uint8_t how_much_to_copy = 0;
                if ((cluster_offset + size >
                     stream->volume->super.sectors_per_clusters * stream->volume->super.bytes_per_sector)) {
                    how_much_to_copy =
                            (stream->volume->super.sectors_per_clusters * stream->volume->super.bytes_per_sector) -
                            cluster_offset;
                    memcpy((char *) ptr + buffer_offset, buffer + cluster_offset, how_much_to_copy);
                    cluster_offset += how_much_to_copy;
                    buffer_offset += how_much_to_copy;
                    cluster_offset = 0;
                    file_offset++;

                    first_sector =
                            ((stream->chain->clusters[file_offset] - 2) * stream->volume->super.sectors_per_clusters) +
                            stream->volume->first_data_sector;
                    if (disk_read(stream->volume->disk, first_sector, buffer,
                                  stream->volume->super.sectors_per_clusters) ==
                        -1) {
                        free(buffer);
                        return nmemb;
                    }
                    memcpy((char *) ptr + buffer_offset, buffer, size - how_much_to_copy);
                    how_many_elements++;
                    cluster_offset += size - how_much_to_copy;
                    buffer_offset += size - how_much_to_copy;
                } else {
                    memcpy((char *) ptr + buffer_offset, buffer + cluster_offset, size);
                    how_many_elements++;
                    cluster_offset += size;
                    buffer_offset += size;
                }
            } else {
                file_offset++;
                cluster_offset = 0;
                first_sector =
                        ((stream->chain->clusters[file_offset] - 2) * stream->volume->super.sectors_per_clusters) +
                        stream->volume->first_data_sector;
                if (disk_read(stream->volume->disk, first_sector, buffer, stream->volume->super.sectors_per_clusters) ==
                    -1) {
                    free(buffer);
                    return nmemb;
                }
                memcpy((char *) ptr + buffer_offset, buffer + cluster_offset, size);
                how_many_elements++;
                cluster_offset += size;
                buffer_offset += size;
            }
        } else if (stream->size ==
                   ((file_offset * stream->volume->super.sectors_per_clusters *
                     stream->volume->super.bytes_per_sector) +
                    cluster_offset + size)) {
            memcpy((char *) ptr + buffer_offset, buffer + cluster_offset, size);
            how_many_elements++;
            cluster_offset += size;
            buffer_offset += size;
            stream->end_of_file = true;
            break;
        } else if ((stream->size - ((file_offset * stream->volume->super.sectors_per_clusters *
                                     stream->volume->super.bytes_per_sector) +
                                    cluster_offset)) < size) {
            int how_many = stream->size - ((file_offset * stream->volume->super.sectors_per_clusters *
                                            stream->volume->super.bytes_per_sector) +
                                           cluster_offset);
            memcpy((char *) ptr + buffer_offset, buffer + cluster_offset, how_many);
            cluster_offset += how_many;
            stream->end_of_file = true;

        } else {
            stream->end_of_file = true;
            break;
        }

    }

    stream->file_offset = file_offset;
    stream->cluster_offset = cluster_offset;

    free(buffer);
    return how_many_elements;
}


int32_t file_seek(struct file_t *stream, int32_t offset, int whence) {
    if (stream == NULL) {
        SET_ERRNO(EFAULT);
        return -1;
    }

    if (whence == SEEK_SET) {
        if (offset > (int32_t) stream->size) {
            SET_ERRNO(ENXIO);
            return -1;
        }
        stream->file_offset = (int) (offset / (stream->volume->super.sectors_per_clusters *
                                               stream->volume->super.bytes_per_sector));
        if (stream->file_offset != 0) {
            stream->cluster_offset = offset % (stream->file_offset * (stream->volume->super.sectors_per_clusters *
                                                                      stream->volume->super.bytes_per_sector));
        } else {
            stream->cluster_offset = offset;
        }

    } else if (whence == SEEK_END) {
        if ((uint32_t) abs(offset) > stream->size) {
            SET_ERRNO(ENXIO);
            return -1;
        }
        int bytes_from_beginning = stream->size + offset;
        stream->file_offset = (int) (bytes_from_beginning / (stream->volume->super.sectors_per_clusters *
                                                             stream->volume->super.bytes_per_sector));
        stream->cluster_offset =
                bytes_from_beginning % (stream->file_offset * (stream->volume->super.sectors_per_clusters *
                                                               stream->volume->super.bytes_per_sector));
    } else if (whence == SEEK_CUR) {
        uint32_t current_bytes = (stream->volume->super.sectors_per_clusters * stream->volume->super.bytes_per_sector) *
                                 stream->file_offset + stream->cluster_offset;
        if ((current_bytes + offset) <= 0 || current_bytes + offset > stream->size) {
            SET_ERRNO(ENXIO);
            return -1;
        }
        stream->file_offset = (int) (offset + current_bytes) / (stream->volume->super.sectors_per_clusters *
                                                                stream->volume->super.bytes_per_sector);
        stream->cluster_offset =
                (offset + current_bytes) % (stream->file_offset * (stream->volume->super.sectors_per_clusters *
                                                                   stream->volume->super.bytes_per_sector));
    }

    return 0;
}

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path) {
    if (!pvolume) {
        SET_ERRNO(EFAULT);
        return NULL;
    }
    if (!dir_path) {
        SET_ERRNO(ENOENT);
        return NULL;
    }

    struct dir_t *dir = malloc(sizeof(struct dir_t));
    if (dir == NULL) {
        SET_ERRNO(ENOMEM);
        return NULL;
    }
    if (dir_path[0] == '\\') {
        dir->disk = pvolume->disk;
        dir->dir_offset = 0;
        return dir;
    }

    free(dir);
    return NULL;
}

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry) {
    if (pdir == NULL || pentry == NULL) {
        SET_ERRNO(EFAULT);
        return -1;
    }


    uint8_t *buffer = (uint8_t *) calloc(1, sizeof(uint8_t) * pdir->volume->root_dir_capacity *
                                            pdir->volume->super.bytes_per_sector);
    if (buffer == NULL) {
        SET_ERRNO(ENOMEM);
        return -1;
    }

    int root_dir = pdir->volume->super.size_of_reserved_area +
                   (pdir->volume->super.size_of_fat) * pdir->volume->super.number_of_fats;
    if (disk_read(pdir->volume->disk, root_dir, buffer, pdir->volume->root_dir_sectors) !=
        pdir->volume->root_dir_sectors) {
        free(buffer);
        SET_ERRNO(EINVAL);
        return -1;
    }

    unsigned char name[14] = {'\0'};

    for (; pdir->dir_offset < pdir->volume->super.maximum_number_of_files;) {
        printf("OFFSET: %d\n", pdir->dir_offset);
        struct dir_t *temp = (struct dir_t *) ((char *) buffer + pdir->dir_offset * 32);
        for (int j = 0; j < 14; ++j) name[j] = 0;
        memcpy(name, temp->filename, 8);
        if (name[0] == 0x0 || name[0] == 0xE5 || name[0] == 0x2E) {
            pdir->dir_offset++;
            continue;
        }

        uint8_t dot_bit = 0;
        int counter = -1;
        for (int j = 0; j < 13; ++j) {
            if (!isalpha(name[j])) {
                if (counter == -1) {
                    name[j] = '.';
                    counter++;
                } else {
                    name[j] = temp->extension[counter++];
                }
            } else {
                dot_bit++;
            }
            if (counter == 3) {
                name[j + 1] = '\0';
                break;
            }
        }
        if (temp->extension[0] == ' ') {
            name[dot_bit] = '\0';
        }

        printf("FILE: %s\n", name);
        strcpy(pentry->name, (char *) name);
        pdir->dir_offset++;
        free(buffer);
        return 0;
    }

    free(buffer);
    pdir->dir_offset = 0;
    return 1;
}

int dir_close(struct dir_t *pdir) {
    if (pdir == NULL) {
        SET_ERRNO(EFAULT);
        return -1;
    }

    if (pdir != NULL) {
        free(pdir);
    }

    return 0;
}
