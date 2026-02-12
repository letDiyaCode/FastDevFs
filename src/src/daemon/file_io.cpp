#include "daemon/file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <openssl/sha.h>

// Global variable to store absolute path to data directory
char g_data_dir_path[512] = {0};

// SHA-256 hash function for file paths
static void hash_path_sha256(const char* path, char* output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)path, strlen(path), hash);
    
    // Convert to hex string
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[SHA256_DIGEST_LENGTH * 2] = '\0';
}

void get_data_file_path(const char* file_path, char* path_buf, size_t buf_size) {
    char hash[SHA256_DIGEST_LENGTH * 2 + 1];
    hash_path_sha256(file_path, hash);
    snprintf(path_buf, buf_size, "%s/%s", g_data_dir_path, hash);
}

int init_data_dir() {
    struct stat st = {0};
    
    // Get current working directory and construct absolute path
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        return -1;
    }
    
    // Store absolute path to data directory
    snprintf(g_data_dir_path, sizeof(g_data_dir_path), "%s/data", cwd);
    
    // Create data directory if it doesn't exist
    if (stat(g_data_dir_path, &st) == -1) {
        if (mkdir(g_data_dir_path, 0755) == -1) {
            perror("mkdir data directory");
            return -1;
        }
    }
    
    return 0;
}

ssize_t read_inode_data(const char* file_path, char* buf, size_t size, off_t offset) {
    char path[256];
    get_data_file_path(file_path, path, sizeof(path));
    
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

ssize_t write_inode_data(const char* file_path, const char* buf, size_t size, off_t offset) {
    char path[256];
    get_data_file_path(file_path, path, sizeof(path));
    
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

int truncate_inode_data(const char* file_path, off_t new_size) {
    char path[256];
    get_data_file_path(file_path, path, sizeof(path));
    
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

int delete_inode_data(const char* file_path) {
    char path[256];
    get_data_file_path(file_path, path, sizeof(path));
    
    // Delete the data file
    if (unlink(path) < 0 && errno != ENOENT) {
        return -1;
    }
    
    return 0;
}
