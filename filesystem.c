#define _XOPEN_SOURCE 700

#include "filesystem.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

// Buffer size used when copying file contents (read()/write() in chunks).
#define COPY_BUFFER_SIZE 65536

int path_exists(const char *path){
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISDIR(st.st_mode);
}

int file_exists(const char *path){
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISREG(st.st_mode);
}

int absolute_path(const char *path, char *buffer, size_t size){
  char *resolved = realpath(path, NULL);

  if (resolved == NULL)
    return 1;

  if (strlen(resolved) >= size) {
    free(resolved);
    return 1;
  }

  strcpy(buffer, resolved);

  free(resolved);
  return 0;
}

int make_directories(const char *path){
  char buffer[MAX_PATH_SIZE];
  size_t len = strlen(path);

  if (len == 0 || len >= sizeof(buffer))
    return 1;

  strcpy(buffer, path);

  // Create every intermediate directory ("/a/b/c" -> "/a", "/a/b", "/a/b/c").
  for (size_t i = 1; i < len; i++) {
    if (buffer[i] != '/')
      continue;

    buffer[i] = '\0';

    if (mkdir(buffer, 0700) != 0 && errno != EEXIST)
      return 1;

    buffer[i] = '/';
  }

  if (mkdir(buffer, 0700) != 0 && errno != EEXIST)
    return 1;

  return 0;
}

/**
 * Copies the contents of a single regular file, using only read()/write().
 *
 * @param src_path Path of the file being copied.
 * @param dst_path Path of the destination file (created/truncated).
 *
 * @return 0 on success.
 * @return 1 on error.
 */
static int copy_file(const char *src_path, const char *dst_path){
  int src_fd = open(src_path, O_RDONLY);

  if (src_fd < 0)
    return 1;

  int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  if (dst_fd < 0) {
    close(src_fd);
    return 1;
  }

  char buffer[COPY_BUFFER_SIZE];
  ssize_t bytes_read;
  int result = 0;

  while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
    size_t total_written = 0;
    size_t to_write = (size_t)bytes_read;

    while (total_written < to_write) {
      ssize_t written = write(dst_fd, buffer + total_written, to_write - total_written);

      if (written < 0) {
        result = 1;
        break;
      }

      total_written += (size_t)written;
    }

    if (result != 0)
      break;
  }

  if (bytes_read < 0)
    result = 1;

  close(src_fd);
  close(dst_fd);

  return result;
}

int copy_directory_recursive(const char *src_dir, const char *dst_dir){
  if (make_directories(dst_dir) != 0)
    return 1;

  DIR *dir = opendir(src_dir);

  if (dir == NULL)
    return 1;

  struct dirent *entry;
  int result = 0;

  while (result == 0 && (entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    char src_path[MAX_PATH_SIZE];
    char dst_path[MAX_PATH_SIZE];

    int src_len = snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, entry->d_name);
    int dst_len = snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, entry->d_name);

    if (src_len < 0 || dst_len < 0 ||
        (size_t)src_len >= sizeof(src_path) || (size_t)dst_len >= sizeof(dst_path)) {
      result = 1;
      break;
    }

    struct stat st;

    if (lstat(src_path, &st) != 0) {
      result = 1;
      break;
    }

    if (S_ISDIR(st.st_mode)) {
      result = copy_directory_recursive(src_path, dst_path);
    } else if (S_ISREG(st.st_mode)) {
      result = copy_file(src_path, dst_path);
    }
    // Other file types (symlinks, devices, sockets, ...) are skipped.
  }

  closedir(dir);

  return result;
}

/**
 * Checks whether a file name ends with the ".conf" extension.
 *
 * @param name Null-terminated file name to check.
 *
 * @return 1 if it ends with ".conf", 0 otherwise.
 */
static int has_conf_extension(const char *name){
  const char *dot = strrchr(name, '.');

  if (dot == NULL)
    return 0;

  return strcmp(dot, ".conf") == 0;
}

/**
 * Comparator used to sort file paths alphabetically with qsort. Each
 * "element" is a fixed-size char array (a row of list->paths), compared
 * directly as a string.
 */
static int compare_paths(const void *a, const void *b){
  return strcmp((const char *)a, (const char *)b);
}

int list_conf_files(const char *dir_path, ConfFileList *list){
  list->count = 0;

  DIR *dir = opendir(dir_path);

  if (dir == NULL)
    return 1;

  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {
    if (!has_conf_extension(entry->d_name))
      continue;

    if (list->count >= MAX_CONF_FILES) {
      fprintf(stderr, "Too many .conf files in %s (max %d).\n", dir_path, MAX_CONF_FILES);
      break;
    }

    snprintf(list->paths[list->count], MAX_PATH_SIZE, "%s/%s", dir_path, entry->d_name);
    list->count++;
  }

  closedir(dir);

  // organiza alfabeticamente com
  qsort(list->paths, list->count, MAX_PATH_SIZE, compare_paths);

  return 0;
}