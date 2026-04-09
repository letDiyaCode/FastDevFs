# FastDevFS CLI Tool Guide

## Overview

The **FastDevFS CLI tool** is a command-line interface for managing and mounting the FastDevFS filesystem. The tool creates a deduplication-enabled FUSE filesystem that automatically compresses and deduplicates data, providing significant storage savings.

## Building the CLI Tool

### Prerequisites

Before building FastDevFS, ensure you have the following dependencies installed:

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libfuse3-dev \
    pkg-config \
    g++

# For Python support (if needed)
python3 -m venv myenv
source myenv/bin/activate
```

### Build Steps

1. **Clone and navigate to the project**:
   ```bash
   git clone https://github.com/devlup-labs/FastDevFs.git
   cd FastDevFs
   ```

2. **Create and enter build directory**:
   ```bash
   mkdir -p build
   cd build
   ```

3. **Generate build files with CMake**:
   ```bash
   cmake ..
   ```

4. **Compile the project**:
   ```bash
   make
   ```

   Or use parallel compilation for faster builds:
   ```bash
   make -j$(nproc)
   ```

5. **Verify successful build**:
   ```bash
   ls -la FastDevFS
   ```

The compiled CLI binary will be located at `build/FastDevFS`.

## Using the CLI Tool

### Basic Syntax

```bash
sudo ./FastDevFS [FUSE_OPTIONS] <mountpoint>
```

**Note**: Root privileges (`sudo`) are required to mount FUSE filesystems.

### Basic Usage Examples

#### 1. Mount in Foreground Mode (Debugging)

```bash
mkdir -p ~/test_mount_dir
sudo ./FastDevFS ~/test_mount_dir -f
```

**What happens**:
- Mounts FastDevFS at `~/test_mount_dir`
- `-f` flag runs in foreground mode (logs printed to terminal)
- Process continues running; press `Ctrl+C` to unmount and exit

#### 2. Mount in Background (Daemon Mode)

```bash
mkdir -p ~/test_mount_dir
sudo ./FastDevFS ~/test_mount_dir
```

**What happens**:
- Mounts FastDevFS and daemonizes (runs in background)
- The process forks and returns to the terminal
- Filesystem remains mounted until explicitly unmounted

#### 3. Unmount the Filesystem

```bash
sudo umount ~/test_mount_dir
```

Or use FUSE's umount tool:
```bash
fusermount -u ~/test_mount_dir
```

### CLI Options

The FastDevFS CLI supports standard FUSE command-line options:

| Option | Description |
|--------|-------------|
| `-f`, `--foreground` | Run in foreground mode (don't daemonize); useful for debugging |
| `--help` | Display help message with all available options |
| `--version` | Display FUSE library version information |
| `-o opt[=val]` | Pass options to libfuse3; examples below |
| `-d` | Enable debug mode (equivalent to `-o debug`) |
| `-s` | Single-threaded operation |

### Advanced Options (via `-o flag`)

Pass multiple options separated by commas:

```bash
sudo ./FastDevFS ~/test_mount_dir -o debug,entry_timeout=3600
```

Common `-o` options:

| Option | Description |
|--------|-------------|
| `debug` | Enable FUSE debug output |
| `allow_other` | Allow other users to access the mounted filesystem |
| `entry_timeout=N` | Cache timeout for directory entries (seconds) |
| `attr_timeout=N` | Cache timeout for file attributes (seconds) |
| `negative_timeout=N` | Cache timeout for negative lookups (seconds) |

### Help and Version

```bash
# Display full help message
sudo ./FastDevFS --help

# Show FUSE library version
sudo ./FastDevFS --version
```

## Workflow and Data Flow

### File Operations Workflow

When you interact with FastDevFS, the following processes occur:

```
User Action (Create/Delete/Read/Write File)
    ↓
FUSE Kernel Module intercepts the operation
    ↓
FastDevFS CLI tool routes to appropriate handler
    ↓
Tree Structure updated (directory tree/adt.cpp)
    ↓
Data Deduplication (dedup_server.cpp)
    ↓
Hash Calculation (sha256.cpp)
    ↓
File operations on host filesystem
    ↓
Persistence maintained (mmap'd storage)
```

### File Creation Process

```
1. User creates file in mounted directory
2. FastDevFS calculates SHA-256 hash of filename
3. File created on host filesystem with hash as name
4. Inode added to directory tree
5. Directory tree metadata updated in mmap'd persistence
6. Operation returned to user via FUSE
```

### Deduplication Process

```
1. Data written to file
2. SHA-256 hash of data content calculated
3. Hash compared against existing dedup index
4. If hash exists: Link to existing data block
5. If hash new: Create new data block
6. Dedup metadata persisted to mmap'd storage
```

## Persistence

FastDevFS uses **memory-mapped files (mmap)** for data persistence:

- **Tree Persistence**: `/tmp/fastdevfs.mmap`
  - Stores directory tree structure
  - Maintains all inode information
  - Survives filesystem unmounting

- **Dedup Persistence**: `/tmp/fastdevfs_dedup.mmap`
  - Stores deduplication index
  - Maintains hash-to-data mappings
  - Ensures dedup works across sessions

**Note**: Files in `/tmp` may be cleared on system reboot depending on OS configuration. For production use, consider moving these to persistent storage directories.

## Project Structure Overview

```
FastDevFS/
├── src/
│   ├── main.cpp                    # CLI entry point
│   ├── dedup_server.cpp            # Deduplication logic
│   ├── sha256.cpp                  # Hash calculation
│   ├── dedup_index.cpp             # Hash indexing
│   └── daemon/
│       ├── fuse_lowlevel_ops.cpp   # FUSE operations
│       └── directory tree/
│           ├── adt.cpp             # Data structure
│           └── hash.cpp            # Hash functions
├── include/                        # Header files
├── test/                           # Test suite
├── docs/                           # Documentation
└── CMakeLists.txt                  # Build configuration
```

## Troubleshooting

### Error: "no mountpoint specified"
```bash
# Fix: Provide a mountpoint path
sudo ./FastDevFS /path/to/mountpoint -f
```

### Error: "fuse_session_mount() failed"
```bash
# Possible causes:
# 1. Mountpoint doesn't exist
mkdir -p /path/to/mountpoint

# 2. Already mounted
sudo umount /path/to/mountpoint

# 3. Permission issues
sudo ./FastDevFS /path/to/mountpoint -f
```

### Warning: "Cannot connect to /dev/fuse"
```bash
# FUSE device not available; ensure FUSE is properly installed:
sudo apt-get install libfuse3-dev fuse3
```

### Slow Performance
```bash
# Try single-threaded mode for debugging:
sudo ./FastDevFS ~/test_mount_dir -s -f

# Or disable debug mode for production:
sudo ./FastDevFS ~/test_mount_dir -o noentry_timeout
```

## Testing the CLI Tool

### Basic Functionality Test

After mounting the filesystem:

```bash
# Create a test file
echo "Hello, FastDevFS!" > ~/test_mount_dir/test.txt

# Read the file
cat ~/test_mount_dir/test.txt

# Create a directory
mkdir ~/test_mount_dir/subdir

# Create another file with same content (tests deduplication)
echo "Hello, FastDevFS!" > ~/test_mount_dir/subdir/test2.txt

# List files
ls -la ~/test_mount_dir/
```

### Data Persistence Test

```bash
# With filesystem mounted, create a file
echo "persistent data" > ~/test_mount_dir/persistent.txt

# Unmount the filesystem
sudo umount ~/test_mount_dir

# Remount the filesystem
sudo ./FastDevFS ~/test_mount_dir -f

# Verify file still exists
cat ~/test_mount_dir/persistent.txt
# Should output: persistent data
```

### Deduplication Test

```bash
# Create first file
echo "duplicate content" > ~/test_mount_dir/file1.txt

# Create second file with identical content
echo "duplicate content" > ~/test_mount_dir/file2.txt

# Check inode numbers (should point to same data block for optimized storage)
ls -i ~/test_mount_dir/file*.txt
```

## Performance Considerations

1. **Deduplication**: Saves storage for duplicate content
2. **SHA-256 Hashing**: Slight performance overhead for data integrity
3. **Memory Mapping**: Fast I/O operations with persistence
4. **Single-threaded**: Default behavior; use `-s` flag for explicit single-threading

## Building Extended CLI Commands

To add new CLI features to the FastDevFS tool:

### 1. Modify `src/main.cpp`

Add command parsing before FUSE initialization:
```cpp
if (strcmp(argv[1], "stats") == 0) {
    // Print dedup statistics
    print_dedup_stats();
    return 0;
}
```

### 2. Add New Functions

Create handler functions in new source files:
```cpp
// src/cli_commands.cpp
void handle_stats_command() {
    // Implementation
}
```

### 3. Update `CMakeLists.txt`

Add new source files to build:
```cmake
add_executable(fastdevfs 
    src/main.cpp 
    src/cli_commands.cpp
    # ... other files
)
```

### 4. Rebuild

```bash
cd build
cmake ..
make
```

## Summary

| Task | Command |
|------|---------|
| Build CLI tool | `cd build && make` |
| Mount filesystem | `sudo ./FastDevFS ~/mnt -f` |
| Unmount filesystem | `sudo umount ~/mnt` |
| Get help | `sudo ./FastDevFS --help` |
| Rebuild after changes | `cd build && cmake .. && make` |

## Additional Resources

- See [ALGORITHMS.md](ALGORITHMS.md) for data structure details
- See [CONCEPTS.md](CONCEPTS.md) for system architecture
- See [TESTS.md](TESTS.md) for testing procedures
- See [LOCKS.md](LOCKS.md) for concurrency information

---

**Version**: 1.0  
**Last Updated**: 2026-04-07  
**Status**: In Development
