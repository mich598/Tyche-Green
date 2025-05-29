#ifndef FILESYS_H
#define FILESYS_H

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/shell/shell.h>

/* Matches LFS_NAME_MAX */
#define MAX_PATH_LEN 255
#define MAX_STRING_SIZE 255
#define MAX_SENSOR_VAL_LEN 64
#define DOUBLE_TEST_FILE_SIZE  (547 / sizeof(double)) 

extern struct fs_mount_t *mountpoint;

int lsdir(const char *path);
int littlefs_increase_infile_value(char *fname);

int write_sensor_value(char *fname, char* value);
int read_sensor_value(char *fname, char* value, size_t max_len);

int littlefs_mount(struct fs_mount_t *mp);

#endif