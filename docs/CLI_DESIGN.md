# FastDevFs Command Line Interface (CLI) Approach

This document outlines the architectural approach for designing and implementing a dedicated CLI tool (`fdfs`) for FastDevFs. 

## 1. Objectives

Currently, FastDevFs runs primarily as an executable FUSE daemon with some debug output. A dedicated CLI tool should provide an intuitive developer experience including:
- **Mount Management**: Ease of starting, stopping, and unmounting virtual file systems.
- **Daemon Control**: Checking daemon health and background library deduplication worker status.
- **Deduplication Insights**: Viewing real-time deduplication metrics (saved space, hard-linked files, unique canonical library footprints).
- **Dynamic Configuration**: Adjusting thresholds (e.g., settlement timeouts, library classification heuristics) at runtime without needing to reboot the file system.

## 2. Interaction Model (IPC Strategy)

Because FastDevFs operates as a background process (FUSE backend), the CLI needs a secure, fast protocol to communicate with it without disrupting ongoing I/O operations.

### Approach A: Unix Domain Sockets (UDS) [Recommended]
Since FastDevFs already leverages Unix Domain Sockets for inter-process communication (e.g., the existing `/tmp/fastdevfs_dedup.sock` between FUSE and the deduplication backend), standardizing on UDS is optimal.
- **Control Socket**: The FUSE main thread spawns a lightweight `std::thread` bounding a listener to `/tmp/fastdevfs_ctrl.sock`.
- **Message Format**: Use JSON or direct C++ struct binary serialization to query states and send control definitions. 
- *Pros*: Completely decoupled from the actual filesystem mounts. If FUSE mount is stuck/hanging, the control socket can still respond to health checks and force-kill commands.

### Approach B: Virtual File Interface (.fastdevfs)
Expose a hidden control directory directly inside the mounted filesystem (e.g., `<mountpoint>/.fdfs/stats.json`). 
- **Reading stats**: The CLI simply reads from this virtual path. The FUSE `read` callback dynamically intercepts it and returns JSON data.
- **Writing config**: The CLI writes to `<mountpoint>/.fdfs/config`.
- *Pros*: Cross-platform native behavior, can be accessed using standard built-in tools (`cat`, `echo`, `jq`).
- *Cons*: Demands knowing where the drive is actively mounted at runtime to administer it. Slower interface, reliant on FUSE availability.

## 3. Language & Tooling

- **Language**: **C++**  
  Developing the CLI within the main repository alongside FastDevFs ensures we can statically link and directly share C++ structures (like `DedupStats`, IPC struct definitions).
- **Argument Parsing**: Standard POSIX `getopt_long` to keep dependencies minimal (no external headers required), or a single-header like `CLI11`.
- **Target**: A new executable target defined in `CMakeLists.txt` named `fdfs`.

## 4. Comprehensive CLI Command Reference

The `fdfs` tool will provide a sub-command structure similar to modern CLI tools (like `git` or `docker`) to group functionalities.

### 4.1 Mount Operations Target
Start and stop the FUSE filesystem safely.
```bash
# Start the FastDevFs daemon and mount the filesystem.
fdfs mount <mountpoint> [options]
  --data <dir>      # Override host-backing data layer (default: /tmp/fastdevfs_data)
  --foreground, -f  # Run in foreground (for debugging)
  --log <level>     # Set verbosity (DEBUG, INFO, WARN, ERROR)

# Unmount the filesystem and sync metadata (.mmap)
fdfs unmount <mountpoint>
  --force           # Force lazy unmount if busy (umount -l)
```

### 4.2 Query and Monitoring
Query active metrics directly from the daemon via the socket.
```bash
# Retrieve deduplication statistics and performance
fdfs stats 
  --json            # Output pure JSON for machine readability (jq, dashboards)
  --verbose, -v     # Output exhaustive breakdown (space saved, deduped inodes, canonical roots)

# Check daemon health status
fdfs status
  # Output: Running (PID: 4321), Uptime: 4h 3m, Queue Size: 0
```

### 4.3 Deduplication Control
Take manual control over the `DedupServer` background worker.
```bash
fdfs dedup run      # Skip settlement debounce and force an immediate evaluation sweep
fdfs dedup queue    # Print how many paths are currently waiting to be hashed
fdfs dedup flush    # Drop all pending dedup queue items safely
```

### 4.4 Dynamic Configuration
Adjust FastDevFs behaviors on-the-fly without an unmount/reboot penalty. 
```bash
fdfs config get <key>        # Query active config (e.g., fdfs config get SETTLEMENT_TIMEOUT_MS)
fdfs config set <key> <val>  # Mutate active config
  # Available keys:
  # - SETTLEMENT_TIMEOUT_MS : Debounce timeout for mass-directory writes 
  # - POLICY                : Switch between ALL, NONE, or LIBRARIES_ONLY
```

## 5. Implementation Roadmap

1. **Phase 1: CLI Scaffolding & CMake**
   - Set up `src/cli.cpp` or `src/cli/main.cpp`.
   - Update `CMakeLists.txt` to compile standard `fdfs` executable.
2. **Phase 2: Socket Command Listener**
   - Add a detached control listener thread to `fastdevfs` upon startup.
   - Define a shared header `fdfs_ipc_ctrl.h` handling the struct conversions between `fdfs` and `fastdevfs`.
3. **Phase 3: Daemon Control Implementation**
   - Hook `fdfs mount` (which will simply `exec()` the daemon binary in the background) and `fdfs unmount` (calling `fusermount -u`).
4. **Phase 4: Stats & Config Realization**
   - Bind the metric aggregators in the data/dedup pipelines to the IPC socket reply to support the `fdfs stats` printout.
