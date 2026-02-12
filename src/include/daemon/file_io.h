#pragma once

#include <sys/types.h>

// Data directory for storing file contents (will be set to absolute path)
extern char g_data_dir_path[512];

// Helper functions for file I/O operations
// These functions handle reading/writing file data to the host filesystem
// using inode numbers as filenames

/**
 * Get the path to the data file for a given file path (using hash)
 * @param file_path The file path to hash
 * @param path_buf Buffer to store the constructed path
 * @param buf_size Size of the buffer
 */
void get_data_file_path(const char* file_path, char* path_buf, size_t buf_size);

/**
 * Read data from the host filesystem for a given file path
 * @param file_path The file path (will be hashed)
 * @param buf Buffer to read data into
 * @param size Number of bytes to read
 * @param offset Offset to start reading from
 * @return Number of bytes read, or -1 on error
 */
ssize_t read_inode_data(const char* file_path, char* buf, size_t size, off_t offset);

/**
 * Write data to the host filesystem for a given file path
 * @param file_path The file path (will be hashed)
 * @param buf Buffer containing data to write
 * @param size Number of bytes to write
 * @param offset Offset to start writing at
 * @return Number of bytes written, or -1 on error
 */
ssize_t write_inode_data(const char* file_path, const char* buf, size_t size, off_t offset);

/**
 * Truncate the data file for a given file path
 * @param file_path The file path (will be hashed)
 * @param new_size New size for the file
 * @return 0 on success, -1 on error
 */
int truncate_inode_data(const char* file_path, off_t new_size);

/**
 * Delete the data file for a given file path
 * @param file_path The file path (will be hashed)
 * @return 0 on success, -1 on error
 */
int delete_inode_data(const char* file_path);

/**
 * Initialize the data directory
 * @return 0 on success, -1 on error
 */
int init_data_dir();
