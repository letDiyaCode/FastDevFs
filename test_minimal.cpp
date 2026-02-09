#include "../include/daemon/directory tree/adt.h"
#include <iostream>

int main() {
    std::cout << "Creating treefile..." << std::endl;
    treefile* file = new treefile();  // Allocate on heap to avoid stack overflow
    
    std::cout << "Initializing..." << std::endl;
    initialize(*file);
    
    std::cout << "Initialize completed successfully!" << std::endl;
    std::cout << "firstfree: " << file->head.firstfree << std::endl;
    std::cout << "nodeallocated: " << file->head.nodeallocated << std::endl;
    std::cout << "size: " << file->head.size << std::endl;
    
    delete file;
    
    return 0;
}
