1. Now we plan to implement the FUSE interface for the FastDevFs.
2. For this we will use low_level_fuse.h instead of fuse.h
3. Implement all the neccessary functions required for a fully functional FUSE filesystem.
4. Then update the main.cpp and other build configs.
5. Try running the filesystem and check if it works as expected.
6. The file creation and deletion should be persistent and should be reflected in the underlying storage.
7. The file operation workflow is as follows: 

    a. if the user requests to create or delete a file. We create the file update the inode in the directory tree and do all the neccessary stuff with the directory tree with respect to the request also keeping in mind if it is a directory or a file.

    b. We create file by the name of the hash of the file name. We create it on the host file system in the single directory where all the files are stored and take the inode from here to update the directory tree.

    c. For reading writing and saving follow appropriate procedure by doing operations on the file on the host file system.

    d. Read the Hash Data structure analyse its need and then use it appropriately for fast lookups inside the directory tree array based on file names and any other needs as you think deem fit.

