#pragma once

#include <sys/types.h>

// Data directory for storing file contents (will be set to absolute path)
extern char g_data_dir_path[512];

// Helper functions for file I/O operations
// These functions handle reading/writing file data to the host filesystem
// using inode numbers as filenames

/**
 * Get the path to the data file for a given inode
 * @param inode The inode number
 * @param path_buf Buffer to store the constructed path
 * @param buf_size Size of the buffer
 */
void get_data_file_path(int inode, char* path_buf, size_t buf_size);

/**
 * Read data from the host filesystem for a given inode
 * @param inode The inode number
 * @param buf Buffer to read data into
 * @param size Number of bytes to read
 * @param offset Offset to start reading from
 * @return Number of bytes read, or -1 on error
 */
ssize_t read_inode_data(int inode, char* buf, size_t size, off_t offset);

/**
 * Write data to the host filesystem for a given inode
 * @param inode The inode number
 * @param buf Buffer containing data to write
 * @param size Number of bytes to write
 * @param offset Offset to start writing at
 * @return Number of bytes written, or -1 on error
 */
ssize_t write_inode_data(int inode, const char* buf, size_t size, off_t offset);

/**
 * Truncate the data file for a given inode
 * @param inode The inode number
 * @param new_size New size for the file
 * @return 0 on success, -1 on error
 */
int truncate_inode_data(int inode, off_t new_size);

/**
 * Delete the data file for a given inode
 * @param inode The inode number
 * @return 0 on success, -1 on error
 */
int delete_inode_data(int inode);

/**
 * Initialize the data directory
 * @return 0 on success, -1 on error
 */
int init_data_dir();
