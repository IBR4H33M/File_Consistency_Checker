# File_Consistency_Checker
In this project, we build vsfsck - a command-line consistency checker and repair tool for a custom Very Simple File System (VSFS). Our utility opens a VSFS image, validates its superblock metadata (magic number, block size, layout pointers, inode count), then inspects the inode and data bitmaps against the actual inodes and block pointers.

Features: 
- Superblock Validator
Verifies magic number, block size, total blocks, inode/table and data block pointers, inode size, and inode count.
- Inode Bitmap Consistency Checker
Ensures each bit set in the inode bitmap corresponds to a valid inode and every valid inode is marked in the bitmap.
- Data Bitmap Consistency Checker
Confirms every block marked used in the data bitmap is referenced by a valid inode and every referenced block is marked used.
- Repair
Automatically fixes detected bitmap inconsistencies, updating both inode and data bitmaps so a re-check passes with no errors.
- Summary Reporting
Prints clear messages about errors found or repairs made, including “No error found” when bitmaps are already consistent.
