#ifndef FILESYSTEM__H
#define FILESYSTEM__H

#include <stddef.h>

#include "constants.h"

/**
 * Checks whether a path exists and is a directory.
 *
 * @param path Directory path.
 *
 * @return 1 if it exists and is a directory, 0 otherwise.
 */
int path_exists(const char *path);

/**
 * Checks whether a path exists and is a regular file.
 *
 * @param path File path.
 *
 * @return 1 if it exists and is a regular file, 0 otherwise.
 */
int file_exists(const char *path);

/**
 * Converts the given path into an absolute path, resolving symbolic links,
 * relative components ('.' and '..'), and redundant separators. The resolved
 * path is copied into the provided buffer.
 *
 * @param path Path to resolve.
 * @param buffer Destination buffer where the absolute path will be stored.
 * @param size Size of the destination buffer, in bytes.
 *
 * @return 0 if the path was successfully resolved and copied to the buffer
 * @return 1 if the path could not be resolved or the buffer is too small.
 */
int absolute_path(const char *path, char *buffer, size_t size);

/**
 * Creates a directory and any missing parent directories, similarly to
 * `mkdir -p`.
 *
 * @param path Directory path to create.
 *
 * @return 0 on success (including when the directory already exists).
 * @return 1 on error.
 */
int make_directories(const char *path);

/**
 * Recursively copies the contents of src_dir into dst_dir, preserving
 * subdirectories. Missing directories (including dst_dir itself) are
 * created as needed. Only regular files and directories are copied; other
 * file types (symlinks, devices, ...) are skipped.
 *
 * @param src_dir Source directory to copy from.
 * @param dst_dir Destination directory to copy into.
 *
 * @return 0 on success.
 * @return 1 on error.
 */
int copy_directory_recursive(const char *src_dir, const char *dst_dir);

/**
 * Fixed-capacity list of .conf file paths (no dynamic memory involved).
 */
typedef struct {
  char paths[MAX_CONF_FILES][MAX_PATH_SIZE];
  size_t count;
} ConfFileList;

/**
 * Lists every file with a ".conf" extension directly inside dir_path,
 * sorted alphabetically, into list->paths. If more than MAX_CONF_FILES are
 * found, the extra ones are ignored (a warning is printed to stderr).
 *
 * @param dir_path Directory to search.
 * @param list Pointer to the ConfFileList to fill in (list->count is reset).
 *
 * @return 0 on success.
 * @return 1 if dir_path could not be opened.
 */
int list_conf_files(const char *dir_path, ConfFileList *list);

#endif // FILESYSTEM__H