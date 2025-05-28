/* File System Driver Module
 * Code taken from zephyr/samples/subsys/fs/littlefs
 * Outputs are logged to lfs directory
 */

#include "filesys.h"
#include "sensors.h"

double sensor_log[DOUBLE_TEST_FILE_SIZE];

// #include <zephyr/logging/log.h>
// // LOG_MODULE_REGISTER(filesys, LOG_LEVEL_DBG);

int lsdir(const char *path)
{
	int res;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;

	fs_dir_t_init(&dirp);

	/* Verify fs_opendir() */
	res = fs_opendir(&dirp, path);
	if (res) {
		printk("Error opening dir %s [%d]\n", path, res);
        return res;
	}

	printk("\nListing dir %s ...\n", path);

    for (;;) {
		/* Verify fs_readdir() */
		res = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (res || entry.name[0] == 0) {
			if (res < 0) {
				printk("Error reading dir [%d]\n", res);
            }
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR) {
			printk("[DIR ] %s\n", entry.name);
		} else {
			printk("[FILE] %s (size = %zu)\n",
				   entry.name, entry.size);
		}
	}

	/* Verify fs_closedir() */
	fs_closedir(&dirp);

	return res;
}

int littlefs_increase_infile_value(char *fname)
{
	uint8_t boot_count = 0;
	struct fs_file_t file;
	int rc, ret;

	fs_file_t_init(&file);
	rc = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
	if (rc < 0) {
		printk("FAIL: open %s: %d", fname, rc);
		return rc;
	}

	rc = fs_read(&file, &boot_count, sizeof(boot_count));
	if (rc < 0) {
		printk("FAIL: read %s: [rd:%d]", fname, rc);
		goto out;
	}
	printk("%s read count:%u (bytes: %d)\n", fname, boot_count, rc);

	rc = fs_seek(&file, 0, FS_SEEK_SET);
	if (rc < 0) {
		printk("FAIL: seek %s: %d", fname, rc);
		goto out;
	}

	boot_count += 1;
	rc = fs_write(&file, &boot_count, sizeof(boot_count));
	if (rc < 0) {
		printk("FAIL: write %s: %d", fname, rc);
		goto out;
	}

	printk("%s write new boot count %u: [wr:%d]\n", fname,
		   boot_count, rc);

 out:
	ret = fs_close(&file);
	if (ret < 0) {
		printk("FAIL: close %s: %d", fname, ret);
		return ret;
	}

	return (rc < 0 ? rc : 0);
}

int write_sensor_value(char *fname, char *value)
{
    struct fs_file_t file;
    int rc, ret;

    fs_file_t_init(&file);
    rc = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
    if (rc < 0) {
        printk("FAIL: open %s: %d", fname, rc);
        return rc;
    }

    /* Seek to beginning to overwrite */
    rc = fs_seek(&file, 0, FS_SEEK_SET);
    if (rc < 0) {
        printk("FAIL: seek %s: %d", fname, rc);
        goto out;
    }

    /* Write the string value */
    rc = fs_write(&file, value, strlen(value));
    if (rc < 0) {
        printk("FAIL: write %s: %d", fname, rc);
    } else {
        printk("Stored string value: %s in %s\n", value, fname);
    }

out:
    ret = fs_close(&file);
    if (ret < 0) {
        printk("FAIL: close %s: %d", fname, ret);
        return ret;
    }

    return (rc < 0 ? rc : 0);
}

int read_sensor_value(char *fname, char *value, size_t max_len)
{
    struct fs_file_t file;
    int rc, ret;

    fs_file_t_init(&file);
    rc = fs_open(&file, fname, FS_O_READ);
    if (rc < 0) {
        printk("FAIL: open %s: %d", fname, rc);
        return rc;
    }

    /* Read up to max_len - 1 bytes to leave space for null terminator */
    rc = fs_read(&file, value, max_len - 1);
    if (rc < 0) {
        printk("FAIL: read %s: %d", fname, rc);
        goto out;
    }

    /* Null-terminate the string */
    value[rc] = '\0';

    printk("Read string value: %s from %s\n", value, fname);

out:
    ret = fs_close(&file);
    if (ret < 0) {
        printk("FAIL: close %s: %d", fname, ret);
        return ret;
    }

    return (rc < 0 ? rc : 0);
}

#ifdef CONFIG_APP_LITTLEFS_STORAGE_FLASH
static int littlefs_flash_erase(unsigned int id)
{
	const struct flash_area *pfa;
	int rc;

	rc = flash_area_open(id, &pfa);
	if (rc < 0) {
		printk("FAIL: unable to find flash area %u: %d\n",
			id, rc);
		return rc;
	}

	printk("Area %u at 0x%x on %s for %u bytes\n",
		   id, (unsigned int)pfa->fa_off, pfa->fa_dev->name,
		   (unsigned int)pfa->fa_size);

	/* Optional wipe flash contents */
	if (IS_ENABLED(CONFIG_APP_WIPE_STORAGE)) {
		rc = flash_area_flatten(pfa, 0, pfa->fa_size);
		printk("Erasing flash area ... %d", rc);
	}

	flash_area_close(pfa);
	return rc;
}

#define PARTITION_NODE DT_NODELABEL(lfs1)

#if DT_NODE_EXISTS(PARTITION_NODE)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);
#else /* PARTITION_NODE */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &storage,
	.storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
	.mnt_point = "/lfs",
};
#endif /* PARTITION_NODE */

	struct fs_mount_t *mountpoint =
#if DT_NODE_EXISTS(PARTITION_NODE)
		&FS_FSTAB_ENTRY(PARTITION_NODE)
#else
		&lfs_storage_mnt
#endif
		;

int littlefs_mount(struct fs_mount_t *mp)
{
	int rc;

	rc = littlefs_flash_erase((uintptr_t)mp->storage_dev);
	if (rc < 0) {
		return rc;
	}

	/* Do not mount if auto-mount has been enabled */
#if !DT_NODE_EXISTS(PARTITION_NODE) ||						\
	!(FSTAB_ENTRY_DT_MOUNT_FLAGS(PARTITION_NODE) & FS_MOUNT_FLAG_AUTOMOUNT)
	rc = fs_mount(mp);
	if (rc < 0) {
		printk("FAIL: mount id %" PRIuPTR " at %s: %d\n",
		       (uintptr_t)mp->storage_dev, mp->mnt_point, rc);
		return rc;
	}
	printk("%s mount: %d\n", mp->mnt_point, rc);
#else
	printk("%s automounted\n", mp->mnt_point);
#endif

	return 0;
}
#endif /* CONFIG_APP_LITTLEFS_STORAGE_FLASH */
