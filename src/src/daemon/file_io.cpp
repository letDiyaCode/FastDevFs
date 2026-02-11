#include "daemon/file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

void get_data_file_path(int inode, char* path_buf, size_t buf_size) {
    snprintf(path_buf, buf_size, "%s/%d", DATA_DIR, inode);
}

int init_data_dir() {
    struct stat st = {0};
    
    // Create data directory if it doesn't exist
    if (stat(DATA_DIR, &st) == -1) {
        if (mkdir(DATA_DIR, 0755) == -1) {
            perror("mkdir data directory");
            return -1;
        }
    }
    
    return 0;
}

ssize_t read_inode_data(int inode, char* buf, size_t size, off_t offset) {
    char path[256];
    get_data_file_path(inode, path, sizeof(path));
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        // File doesn't exist yet - return all zeros
        if (errno == ENOENT) {
            memset(buf, 0, size);
            return size;
        }
        return -1;
    }
    
    // Seek to the offset
    if (lseek(fd, offset, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }
    
    // Read the data
    ssize_t bytes_read = read(fd, buf, size);
    close(fd);
    
    return bytes_read;
}

ssize_t write_inode_data(int inode, const char* buf, size_t size, off_t offset) {
    char path[256];
    get_data_file_path(inode, path, sizeof(path));
    
    // Open with O_CREAT to create file if it doesn't exist
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        perror("open for write");
        return -1;
    }
    
    // Seek to the offset
    if (lseek(fd, offset, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }
    
    // Write the data
    ssize_t bytes_written = write(fd, buf, size);
    close(fd);
    
    return bytes_written;
}

int truncate_inode_data(int inode, off_t new_size) {
    char path[256];
    get_data_file_path(inode, path, sizeof(path));
    
    // If truncating to 0, we can just delete the file
    if (new_size == 0) {
        unlink(path); // Ignore errors if file doesn't exist
        return 0;
    }
    
    // Otherwise, truncate it
    if (truncate(path, new_size) < 0 && errno != ENOENT) {
        return -1;
    }
    
    return 0;
}

int delete_inode_data(int inode) {
    char path[256];
    get_data_file_path(inode, path, sizeof(path));
    
    // Delete the data file
    if (unlink(path) < 0 && errno != ENOENT) {
        return -1;
    }
    
    return 0;
}
