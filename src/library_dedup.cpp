#include "../include/library_dedup.h"
#include "../include/dedup_index.h"
#include "../include/sha256.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

std::unordered_map<std::string, int> g_lib_catalog;
std::mutex catalog_mtx;
std::string g_config_path;

extern DedupIndex g_index;
// In FastDevFs the global treefile pointer in main is not easily extern'd if it's static.
// BUT we can access it if we declare it extern or use a getter. Let's declare extern:
struct treefile;
extern treefile* file1; // We must modify main.cpp to not be 'static treefile* file1;'

void init_library_catalog(const std::string& config_file_path) {
    std::lock_guard<std::mutex> lock(catalog_mtx);
    g_config_path = config_file_path;
    std::ifstream infile(config_file_path);
    if (!infile.is_open()) return;
    
    std::string line;
    while (std::getline(infile, line)) {
        size_t comma = line.find(',');
        if (comma != std::string::npos) {
            std::string hash = line.substr(0, comma);
            int canonical_idx = std::stoi(line.substr(comma + 1));
            g_lib_catalog[hash] = canonical_idx;
        }
    }
}

static void save_catalog() {
    std::ofstream outfile(g_config_path);
    for (const auto& pair : g_lib_catalog) {
        outfile << pair.first << "," << pair.second << "\n";
    }
}

bool check_model_is_library(const std::string& folder_path) {
    // Stub: Currently consider folders containing "node_modules" or "vendor" as libraries
    if (folder_path.find("/node_modules") != std::string::npos ||
        folder_path.find("/vendor") != std::string::npos) {
        return true;
    }
    return false;
}

static void get_all_files_in_folder(int folder_idx, std::vector<int>& files, treefile& tf) {
    int child = tf.arr[folder_idx].firstchild;
    while(child != -1 && child < tf.size) {
        treenode& node = tf.arr[child];
        if (!node.isdeleted) {
            if (S_ISDIR(node.metadata.mode)) {
                get_all_files_in_folder(child, files, tf);
            } else if (S_ISREG(node.metadata.mode)) {
                files.push_back(child);
            }
        }
        child = node.nextsibling;
    }
}

// Extremely fast folder signature computation reusing cached size & file hash
std::string compute_folder_signature(int folder_idx) {
    std::vector<int> files;
    std::string folder_path;
    {
        std::lock_guard<std::recursive_mutex> lk(treefile_mtx);
        if (!file1) return "";
        folder_path = file1->arr[folder_idx].metadata.name;
        get_all_files_in_folder(folder_idx, files, *file1);
    }
    
    std::vector<std::string> file_sig_parts;
    for (int file_idx : files) {
        long size = 0;
        std::string rel_path;
        {
            std::lock_guard<std::recursive_mutex> lk(treefile_mtx);
            size = file1->arr[file_idx].metadata.size;
            std::string full_path = file1->arr[file_idx].metadata.name;
            if (full_path.rfind(folder_path, 0) == 0) {
                rel_path = full_path.substr(folder_path.length());
            } else {
                rel_path = full_path;
            }
        }
        std::string file_hash = g_index.get_inode_hash(file_idx);
        
        std::ostringstream ss;
        ss << rel_path << "|" << size << "|" << file_hash;
        file_sig_parts.push_back(ss.str());
    }
    
    std::sort(file_sig_parts.begin(), file_sig_parts.end());
    
    std::ostringstream master;
    for(const auto& s : file_sig_parts) {
        master << s << "\n";
    }
    return sha256_hex(master.str().c_str(), master.str().length());
}

static std::string host_data_path_for(int index) {
    return "/tmp/fastdevfs_data/" + std::to_string(index);
}

static void do_node_share(int target_folder_idx, int canonical_folder_idx) {
    // All we need to do is share the firstchild pointer.
    // dedup_link handles:
    //   • freeing any existing (empty) child nodes under target
    //   • setting target.firstchild = canonical.firstchild
    //   • marking target.is_deduped = true
    //   • incrementing canonical.dedup_refcount
    std::lock_guard<std::recursive_mutex> lk(treefile_mtx);
    if (!file1) return;
    dedup_link(target_folder_idx, canonical_folder_idx, *file1);
}

void evaluate_and_deduplicate_library_folder(int folder_idx) {
    if (!file1) return;
    
    std::string folder_path;
    {
        std::lock_guard<std::recursive_mutex> lk(treefile_mtx);
        if (folder_idx < 0 || folder_idx >= file1->size) return;
        treenode& node = file1->arr[folder_idx];
        if (node.isdeleted || !S_ISDIR(node.metadata.mode)) return;
        folder_path = node.metadata.name;
    }
    
    if (!check_model_is_library(folder_path)) return;
    
    std::string sig = compute_folder_signature(folder_idx);
    if (sig.empty()) return;
    
    std::lock_guard<std::mutex> lock(catalog_mtx);
    auto it = g_lib_catalog.find(sig);
    if (it != g_lib_catalog.end()) {
        int canonical_folder_idx = it->second;
        if (canonical_folder_idx != folder_idx) {
            std::cout << "[LibraryDedup] Duplicate library detected — node-sharing: "
                      << folder_path << " → canonical idx " << canonical_folder_idx
                      << " (" << file1->arr[canonical_folder_idx].metadata.name << ")\n";
            do_node_share(folder_idx, canonical_folder_idx);
        }
    } else {
        std::cout << "[LibraryDedup] Registered new canonical library folder: " << folder_path 
                  << " (Signature: " << sig.substr(0, 8) << "...)" << std::endl;
        g_lib_catalog[sig] = folder_idx;
        save_catalog();
    }
}
