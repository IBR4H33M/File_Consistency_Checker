#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#define MAGIC 0xD34D  
#define TOTAL_BLOCKS 64
#define DESIRED_BLOCK_SIZE 4096 
#define INODE_SIZE 256

void printBitmap(const char *label, const uint8_t *bitmap, size_t num_bytes) {
    printf("%s:\n", label);
    for (size_t i = 0; i < num_bytes; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            printf("%d", (bitmap[i] >> bit) & 1);
        }
        printf(" ");
    }
    printf("\n");
}

struct Superblock {
    uint32_t inode_count;
    uint32_t inode_bitmap_block;
    uint32_t data_bitmap_block;
    uint32_t first_data_block;
    uint16_t magic_bytes;             
    uint32_t block_size;               
    uint32_t total_blocks;     
    uint32_t inode_table_start;           
    uint32_t inode_size;            
    uint8_t reserved[4058];          
};

struct Inode {
    uint32_t mode;  
    uint32_t user_id;
    uint32_t group_id;
    uint32_t file_size;
    uint32_t last_access_time;
    uint32_t creation_time;
    uint32_t last_modification_time;
    uint32_t deletion_time;
    uint32_t hard_links;
    uint32_t data_blocks;
    uint32_t direct_block_pointer;
    uint32_t single_indirect_pointer;
    uint32_t double_indirect_pointer;
    uint32_t triple_indirect_pointer;
    uint8_t reserved[156];
};

void superblockValidator();
void checkDataBitmapConsistency(const struct Superblock *sb);
void checkInodeBitmapConsistency(const struct Superblock *sb);
void updateInode0IfNeeded(const struct Superblock *sb);
void checkBadBlocks(const struct Superblock *sb);
void checkDuplicateBlocks(const struct Superblock *sb);

uint32_t findFreeBlock(uint8_t *bitmap, uint32_t total_blocks, uint32_t first_block) {
    for (uint32_t block = first_block; block < total_blocks; block++) {
        int byte = block / 8;
        int bit = 7 - (block % 8);
        if (!((bitmap[byte] >> bit) & 1)) {
            return block;
        }
    }
    return 0;
}

void markBlockUsed(uint8_t *bitmap, uint32_t block) {
    int byte = block / 8;
    int bit = 7 - (block % 8);
    bitmap[byte] |= (1 << bit);
}

int main() {
    printf("\n\n");
    struct Superblock sb1;
    sb1.magic_bytes = MAGIC;
    sb1.block_size = DESIRED_BLOCK_SIZE;
    sb1.total_blocks = TOTAL_BLOCKS;
    sb1.inode_bitmap_block = 1;
    sb1.data_bitmap_block = 2;
    sb1.inode_table_start = 3;
    sb1.first_data_block = 8;
    sb1.inode_size = INODE_SIZE;
    uint32_t inodes_per_block = DESIRED_BLOCK_SIZE / INODE_SIZE;
    sb1.inode_count = (sb1.first_data_block - sb1.inode_table_start) * inodes_per_block;

    FILE *file = fopen("vsfs.img", "r+b");
    if (file == NULL) {
        perror("Failed to open vsfs.img");
        return 1;
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    printf("File size: %ld bytes\n", file_size);
    
    uint16_t current_magic;
    if (fread(&current_magic, sizeof(uint16_t), 1, file) != 1) {
        printf("Failed to read magic number.\n");
        fclose(file);
        return 1;
    }
    if (current_magic != MAGIC) {
        printf("Magic number does not match. Updating the magic number...\n");
        fseek(file, 0, SEEK_SET);
        uint16_t magic_val = MAGIC;
        if (fwrite(&magic_val, sizeof(uint16_t), 1, file) != 1) {
            printf("Failed to update magic number. Check file permissions.\n");
            fclose(file);
            return 1;
        }
        fflush(file);
        printf("Magic number updated to: 0x%X\n", MAGIC);
    } else {
        printf("Magic number is already correct.\n");
    }
    
    struct Superblock sb;
    fseek(file, 1024, SEEK_SET);
    if (fread(&sb, sizeof(sb), 1, file) != 1) {
        perror("Failed to read superblock");
        fclose(file);
        return 1;
    }
    uint32_t expected_inode_bitmap_start = 1;
    uint32_t expected_data_bitmap_start  = 2;
    uint32_t expected_inode_table_start  = 3;
    uint32_t expected_data_block_start   = 8;
    uint32_t expected_inode_size = INODE_SIZE;
    uint32_t inode_table_blocks = expected_data_block_start - expected_inode_table_start;
    uint32_t calculated_total_inodes = inode_table_blocks * (DESIRED_BLOCK_SIZE / INODE_SIZE);
    uint32_t expected_total_blocks = TOTAL_BLOCKS;
    uint32_t expected_block_size = DESIRED_BLOCK_SIZE;
    
    if (sb.inode_bitmap_block == expected_inode_bitmap_start &&
        sb.data_bitmap_block == expected_data_bitmap_start &&
        sb.inode_table_start == expected_inode_table_start &&
        sb.first_data_block == expected_data_block_start &&
        sb.inode_size == expected_inode_size &&
        sb.inode_count == calculated_total_inodes &&
        sb.total_blocks == expected_total_blocks &&
        sb.block_size == expected_block_size) { 
        printf("The superblock already follows the expected layout.\n");        
    } else {
        sb.inode_bitmap_block = expected_inode_bitmap_start;
        sb.data_bitmap_block  = expected_data_bitmap_start;
        sb.inode_table_start  = expected_inode_table_start;
        sb.first_data_block   = expected_data_block_start;
        sb.inode_size = expected_inode_size;
        sb.total_blocks = expected_total_blocks;
        sb.block_size = expected_block_size;
        sb.inode_count = calculated_total_inodes;
        fseek(file, 1024, SEEK_SET);
        if (fwrite(&sb, sizeof(sb), 1, file) != 1) {
            perror("Failed to write corrected superblock");
            fclose(file);
            return 1;
        }
        fflush(file);
        printf("\nSuperblock updated with fixed layout:\n");
        printf("Block size: %u bytes\n", sb.block_size);
        printf("Total number of blocks: %u\n", sb.total_blocks);
        printf("Inode size: %u bytes\n", sb.inode_size);
        printf("Inode count: %u\n", sb.inode_count);
        printf("Inode bitmap block number: %u\n", sb.inode_bitmap_block);
        printf("Data bitmap block number: %u\n", sb.data_bitmap_block);
        printf("Inode table start block number: %u\n", sb.inode_table_start);
        printf("First data block number: %u\n", sb.first_data_block);
        
    }
    superblockValidator();
    fclose(file);

    updateInode0IfNeeded(&sb);

    checkDataBitmapConsistency(&sb);
    checkInodeBitmapConsistency(&sb);
    checkBadBlocks(&sb);
    checkDuplicateBlocks(&sb);

    return 0;
}

void updateInode0IfNeeded(const struct Superblock *sb) {
    FILE *fp = fopen("vsfs.img", "r+b");
    if (!fp) {
        perror("Failed to open vsfs.img for inode update");
        return;
    }
    long inode_table_offset = sb->inode_table_start * sb->block_size;
    fseek(fp, inode_table_offset, SEEK_SET);
    struct Inode inode0;
    if (fread(&inode0, sizeof(struct Inode), 1, fp) != 1) {
        perror("Failed to read inode 0");
        fclose(fp);
        return;
    }
    if (!(inode0.hard_links > 0 && inode0.deletion_time == 0)) {
        printf("Inode 0 is invalid. Correcting inode 0 to be a valid root inode.\n");
        memset(&inode0, 0, sizeof(struct Inode));
        inode0.mode = 0x1FF;
        inode0.user_id = 1000;
        inode0.group_id = 1000;
        inode0.file_size = DESIRED_BLOCK_SIZE;
        inode0.last_access_time = 1680000000;
        inode0.creation_time = 1670000000;
        inode0.last_modification_time = 1690000000;
        inode0.deletion_time = 0;
        inode0.hard_links = 1;
        inode0.data_blocks = 1;
        inode0.direct_block_pointer = sb->first_data_block;
        inode0.single_indirect_pointer = 0;
        inode0.double_indirect_pointer = 0;
        inode0.triple_indirect_pointer = 0;
        memset(inode0.reserved, 0, sizeof(inode0.reserved));
        fseek(fp, inode_table_offset, SEEK_SET);
        if (fwrite(&inode0, sizeof(struct Inode), 1, fp) != 1) {
            perror("Failed to update inode 0");
            fclose(fp);
            return;
        }
        fflush(fp);
        printf("Inode 0 has been updated to a valid state.\n");
    } else {
        printf("Inode 0 is already valid.\n");
    }
    fclose(fp);
}

void checkDataBitmapConsistency(const struct Superblock *sb) {
    printf("\n\n");
    printf("--------------------------------------\n");
    printf("DATA BITMAP CONSISTENCY CHECKER:\n");
    printf("--------------------------------------\n");

    FILE *fp = fopen("vsfs.img", "r+b");
    if (!fp) {
        perror("Failed to open vsfs.img for data bitmap check");
        return;
    }
    uint8_t *data_bitmap = malloc(sb->block_size);
    if (data_bitmap == NULL) {
        perror("Memory allocation failed for data_bitmap");
        fclose(fp);
        return;
    }
    fseek(fp, sb->data_bitmap_block * sb->block_size, SEEK_SET);
    if (fread(data_bitmap, 1, sb->block_size, fp) != sb->block_size) {
        perror("Failed to read data bitmap");
        free(data_bitmap);
        fclose(fp);
        return;
    }

    bool dataBlockReferenced[TOTAL_BLOCKS] = { false };
    uint32_t total_inodes = sb->inode_count;
    fseek(fp, sb->inode_table_start * sb->block_size, SEEK_SET);
    struct Inode inode;
    for (uint32_t i = 0; i < total_inodes; i++) {
        if (fread(&inode, sizeof(struct Inode), 1, fp) != 1) {
            perror("Failed to read inode from inode table");
            free(data_bitmap);
            fclose(fp);
            return;
        }
        bool valid = (inode.hard_links > 0 && inode.deletion_time == 0);
        if (valid) {
            uint32_t ptr = inode.direct_block_pointer;
            if (ptr != 0 && ptr >= sb->first_data_block && ptr < sb->total_blocks) {
                dataBlockReferenced[ptr] = true;
            }
        }
    }

    bool modified = false;
    for (uint32_t block = sb->first_data_block; block < sb->total_blocks; block++) {
        int bit = (data_bitmap[block / 8] & (1 << (7 - (block % 8)))) ? 1 : 0;
        if (bit == 1 && !dataBlockReferenced[block]) {
            data_bitmap[block / 8] &= ~(1 << (7 - (block % 8)));
            printf("Data block %u marked used but not referenced. Clearing bit.\n", block);
            modified = true;
        } else if (dataBlockReferenced[block] && bit == 0) {
            data_bitmap[block / 8] |= (1 << (7 - (block % 8)));
            printf("Data block %u referenced but not marked. Marking it.\n", block);
            modified = true;
        }
    }
    fseek(fp, sb->data_bitmap_block * sb->block_size, SEEK_SET);
    if (modified) {
        if (fwrite(data_bitmap, 1, sb->block_size, fp) != sb->block_size) {
            perror("Failed to write corrected data bitmap");
        } else {
            printf("Data bitmap updated.\n");
            printBitmap("Updated Data Bitmap", data_bitmap, (sb->total_blocks + 7) / 8);
        }
    } else {
        printf("No data bitmap errors found.\n");
    }
    fflush(fp);
    free(data_bitmap);
    fclose(fp);
}

void checkInodeBitmapConsistency(const struct Superblock *sb) {
    printf("\n\n");
    printf("--------------------------------------\n");
    printf("INODE BITMAP CONSISTENCY CHECKER:\n");
    printf("--------------------------------------\n");
    FILE *fp = fopen("vsfs.img", "r+b");
    if (!fp) {
        perror("Failed to open vsfs.img for inode bitmap check");
        return;
    }
    uint8_t *inode_bitmap = malloc(sb->block_size);
    if (inode_bitmap == NULL) {
        perror("Memory allocation failed for inode_bitmap");
        fclose(fp);
        return;
    }
    fseek(fp, sb->inode_bitmap_block * sb->block_size, SEEK_SET);
    if (fread(inode_bitmap, 1, sb->block_size, fp) != sb->block_size) {
        perror("Failed to read inode bitmap");
        free(inode_bitmap);
        fclose(fp);
        return;
    }
    
    bool modified = false;
    uint32_t total_inodes = sb->inode_count;
    fseek(fp, sb->inode_table_start * sb->block_size, SEEK_SET);
    struct Inode inode;
    for (uint32_t i = 0; i < total_inodes; i++) {
        if (fread(&inode, sizeof(struct Inode), 1, fp) != 1) {
            perror("Failed to read inode");
            free(inode_bitmap);
            fclose(fp);
            return;
        }
        bool valid = (inode.hard_links > 0 && inode.deletion_time == 0);
        int bit = (inode_bitmap[i / 8] & (1 << (7 - (i % 8)))) ? 1 : 0;
        if (valid && bit == 0) {
            inode_bitmap[i / 8] |= (1 << (7 - (i % 8)));
            printf("Inode %u valid but unmarked. Marking.\n", i);
            modified = true;
        } else if (!valid && bit == 1) {
            inode_bitmap[i / 8] &= ~(1 << (7 - (i % 8)));
            printf("Inode %u invalid but marked. Clearing.\n", i);
            modified = true;
        }
    }
    fseek(fp, sb->inode_bitmap_block * sb->block_size, SEEK_SET);
    if (modified) {
        if (fwrite(inode_bitmap, 1, sb->block_size, fp) != sb->block_size) {
            perror("Failed to write corrected inode bitmap");
        } else {
            printf("Inode bitmap updated.\n");
            printBitmap("Updated Inode Bitmap", inode_bitmap, (sb->inode_count + 7) / 8);
        }
    } else {
        printf("No inode bitmap errors found.\n");
    }
    fflush(fp);
    free(inode_bitmap);
    fclose(fp);
}

void superblockValidator(){
    printf("\n\n");
    printf("--------------------------------------\n");
    printf("Superblock Validator:\n");
    printf("--------------------------------------\n");    
    
    FILE *file;
    unsigned char block[DESIRED_BLOCK_SIZE];  
    size_t bytesWritten;
    long file_size;
    long target_size = TOTAL_BLOCKS * DESIRED_BLOCK_SIZE;
    file = fopen("vsfs.img", "r+b");
    if (file == NULL) {
        perror("Failed to open file");
        return;
    }
    fseek(file, 0, SEEK_END); 
    file_size = ftell(file);  
    fseek(file, 0, SEEK_SET); 
    printf("Target size for %d blocks: %ld bytes\n", TOTAL_BLOCKS, target_size);
    if (file_size < target_size) {
        printf("Extending file...\n");
        fseek(file, 0, SEEK_END);
        for (long i = file_size; i < target_size; i++) {
            fputc(0, file); 
        }
        file_size = target_size;
        printf("File extended to %ld bytes.\n", file_size);
    }
    if (file_size == target_size) {
        printf("File size correct.\n");
    }
    if (file_size > target_size) {
        printf("Trimming file...\n");
        if (ftruncate(fileno(file), target_size) != 0) {
            perror("Truncate failed");
            fclose(file);
            return;
        }
        file_size = target_size; 
        printf("File truncated to %ld bytes.\n", file_size);    
        fseek(file, 0, SEEK_SET);
        bytesWritten = fwrite(block, sizeof(unsigned char), DESIRED_BLOCK_SIZE, file);
        if (bytesWritten != DESIRED_BLOCK_SIZE) {
            perror("Failed to write block");
            fclose(file);
            return;
        }
        printf("Block overwritten.\n");
    }
    fclose(file);
}

void checkBadBlocks(const struct Superblock *sb) {
    printf("\n\n");
    printf("--------------------------------------\n");
    printf("BAD BLOCK CHECKER:\n");
    printf("--------------------------------------\n");
    FILE *fp = fopen("vsfs.img", "r+b");
    if (!fp) {
        perror("Failed to open vsfs.img");
        return;
    }

    bool found_bad = false;
    uint32_t total_inodes = sb->inode_count;
    fseek(fp, sb->inode_table_start * sb->block_size, SEEK_SET);
    struct Inode inode;

    for (uint32_t i = 0; i < total_inodes; i++) {
        if (fread(&inode, sizeof(struct Inode), 1, fp) != 1) {
            perror("Failed to read inode");
            fclose(fp);
            return;
        }
        if (inode.hard_links > 0 && inode.deletion_time == 0) {
            uint32_t blocks[] = {
                inode.direct_block_pointer,
                inode.single_indirect_pointer,
                inode.double_indirect_pointer,
                inode.triple_indirect_pointer
            };
            for (int j = 0; j < 4; j++) {
                uint32_t block = blocks[j];
                if (block != 0 && (block < sb->first_data_block || block >= sb->total_blocks)) {
                    printf("Inode %u references bad block %u. Fixing...\n", i, block);
                    blocks[j] = 0;
                    found_bad = true;
                    long offset = ftell(fp) - sizeof(struct Inode);
                    fseek(fp, offset, SEEK_SET);
                    fwrite(&inode, sizeof(struct Inode), 1, fp);
                    fflush(fp);
                }
            }
        }
    }
    if (!found_bad) {
        printf("No bad blocks found.\n");
    }
    fclose(fp);
}

void checkDuplicateBlocks(const struct Superblock *sb) {
    printf("\n\n");
    printf("--------------------------------------\n");
    printf("DUPLICATE BLOCK CHECKER:\n");
    printf("--------------------------------------\n");
    FILE *fp = fopen("vsfs.img", "r+b");
    if (!fp) {
        perror("Failed to open vsfs.img");
        return;
    }

    uint32_t block_counts[TOTAL_BLOCKS] = {0};
    uint32_t total_inodes = sb->inode_count;
    uint8_t *data_bitmap = malloc(sb->block_size);
    fseek(fp, sb->data_bitmap_block * sb->block_size, SEEK_SET);
    fread(data_bitmap, 1, sb->block_size, fp);

    fseek(fp, sb->inode_table_start * sb->block_size, SEEK_SET);
    struct Inode inode;
    bool found_duplicates = false;

    for (uint32_t i = 0; i < total_inodes; i++) {
        if (fread(&inode, sizeof(struct Inode), 1, fp) != 1) {
            perror("Failed to read inode");
            fclose(fp);
            return;
        }
        if (inode.hard_links > 0 && inode.deletion_time == 0) {
            uint32_t block = inode.direct_block_pointer;
            if (block != 0 && block >= sb->first_data_block && block < sb->total_blocks) {
                block_counts[block]++;
            }
        }
    }

    fseek(fp, sb->inode_table_start * sb->block_size, SEEK_SET);
    for (uint32_t i = 0; i < total_inodes; i++) {
        if (fread(&inode, sizeof(struct Inode), 1, fp) != 1) {
            perror("Failed to read inode");
            fclose(fp);
            return;
        }
        if (inode.hard_links > 0 && inode.deletion_time == 0) {
            uint32_t block = inode.direct_block_pointer;
            if (block != 0 && block_counts[block] > 1) {
                uint32_t new_block = findFreeBlock(data_bitmap, sb->total_blocks, sb->first_data_block);
                if (new_block == 0) {
                    printf("No free blocks for duplicate fix.\n");
                    continue;
                }
                printf("Inode %u: Reallocating block %u to %u\n", i, block, new_block);
                inode.direct_block_pointer = new_block;
                markBlockUsed(data_bitmap, new_block);
                block_counts[block]--;
                block_counts[new_block]++;
                long offset = ftell(fp) - sizeof(struct Inode);
                fseek(fp, offset, SEEK_SET);
                fwrite(&inode, sizeof(struct Inode), 1, fp);
                found_duplicates = true;
            }
        }
    }

    if (found_duplicates) {
        fseek(fp, sb->data_bitmap_block * sb->block_size, SEEK_SET);
        fwrite(data_bitmap, 1, sb->block_size, fp);
        printf("Duplicate blocks fixed.\n");
    } else {
        printf("No duplicate blocks found.\n");
    }
    printf("\n\n");
    free(data_bitmap);
    fclose(fp);
}
