#include <iostream>
#include "../../../include/daemon/directory tree/adt.h"
#include "../../../include/daemon/directory tree/hash.h"

int main() {
    std::cout << "Testing extended metadata support..." << std::endl;
    treefile* file = new treefile();
    
    initialize(*file);
    
    // Insert a test file
    insert("testfile.txt", "", *file);
    
    int idx = hashindex("testfile.txt", *file);
    if (idx >= 0) {
        std::cout << "\nFile created successfully!" << std::endl;
        std::cout << "Metadata for 'testfile.txt':" << std::endl;
        std::cout << "  Name: " << file->arr[idx].metadata.name << std::endl;
        std::cout << "  Inode: " << file->arr[idx].metadata.inode << std::endl;
        std::cout << "  Mode: 0" << std::oct << file->arr[idx].metadata.mode << std::dec << std::endl;
        std::cout << "  UID: " << file->arr[idx].metadata.uid << std::endl;
        std::cout << "  GID: " << file->arr[idx].metadata.gid << std::endl;
        std::cout << "  Size: " << file->arr[idx].metadata.size << std::endl;
        std::cout << "  Nlink: " << file->arr[idx].metadata.nlink << std::endl;
        std::cout << "  Atime: " << file->arr[idx].metadata.atime << std::endl;
        std::cout << "  Mtime: " << file->arr[idx].metadata.mtime << std::endl;
        std::cout << "  Ctime: " << file->arr[idx].metadata.ctime << std::endl;
    }
    
    delete file;
    std::cout << "\nMetadata test completed successfully!" << std::endl;
    
    return 0;
}
