# File_Consistency_Checker
In this project, we build vsfsck —a command-line consistency checker and repair tool for a custom Very Simple File System (VSFS). Our utility opens a VSFS image, validates its superblock metadata (magic number, block size, layout pointers, inode count), then inspects the inode and data bitmaps against the actual inodes and block pointers.
