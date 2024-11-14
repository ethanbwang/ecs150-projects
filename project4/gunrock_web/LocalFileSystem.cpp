#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "include/Disk.h"
#include "include/LocalFileSystem.h"
#include "include/ufs.h"

using namespace std;

LocalFileSystem::LocalFileSystem(Disk *disk) { this->disk = disk; }

void LocalFileSystem::readSuperBlock(super_t *super) {
  // Block 0 is always the super block
  char buffer[UFS_BLOCK_SIZE];
  disk->readBlock(0, buffer);
  memcpy(super, buffer, sizeof(super_t));
}

void LocalFileSystem::readInodeBitmap(super_t *super,
                                      unsigned char *inodeBitmap) {
  for (int block_num = 0; block_num < super->inode_bitmap_len; block_num++) {
    disk->readBlock(super->inode_bitmap_addr + block_num,
                    inodeBitmap + (block_num * UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::writeInodeBitmap(super_t *super,
                                       unsigned char *inodeBitmap) {
  for (int block_num = 0; block_num < super->inode_bitmap_len; block_num++) {
    disk->writeBlock(super->inode_bitmap_addr + block_num,
                     inodeBitmap + (block_num * UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::readDataBitmap(super_t *super,
                                     unsigned char *dataBitmap) {
  for (int block_num = 0; block_num < super->data_bitmap_len; block_num++) {
    disk->readBlock(super->data_bitmap_addr + block_num,
                    dataBitmap + (block_num * UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::writeDataBitmap(super_t *super,
                                      unsigned char *dataBitmap) {
  for (int block_num = 0; block_num < super->data_bitmap_len; block_num++) {
    disk->writeBlock(super->data_bitmap_addr + block_num,
                     dataBitmap + (block_num * UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::readInodeRegion(super_t *super, inode_t *inodes) {
  // Get number of blocks that inode region spans
  int num_inode_blocks = super->num_inodes * sizeof(inode_t) / UFS_BLOCK_SIZE;
  if (super->num_inodes * sizeof(inode_t) % UFS_BLOCK_SIZE) {
    num_inode_blocks++;
  }

  // Read inode region into inodes
  int inodes_per_block = UFS_BLOCK_SIZE / sizeof(inode_t);
  for (int block_num = 0; block_num < num_inode_blocks; block_num++) {
    disk->readBlock(super->inode_region_addr + block_num,
                    inodes + inodes_per_block * block_num);
  }
}

void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes) {
  // Get number of blocks that inode region spans
  int num_inode_blocks = super->num_inodes * sizeof(inode_t) / UFS_BLOCK_SIZE;
  if (super->num_inodes * sizeof(inode_t) % UFS_BLOCK_SIZE) {
    num_inode_blocks++;
  }

  // Write inodes into inode region
  int inodes_per_block = UFS_BLOCK_SIZE / sizeof(inode_t);
  for (int block_num = 0; block_num < num_inode_blocks; block_num++) {
    disk->writeBlock(super->inode_region_addr + block_num,
                     inodes + inodes_per_block * block_num);
  }
}

void __find_free_bit(const int &bitmap_size, unsigned char bitmap[],
                     int &free_bit_number, int &num_shifts, int &bitmap_byte) {
  for (int idx = 0; idx < bitmap_size; idx++) {
    if (bitmap[idx] != 0xff) {
      free_bit_number = idx * sizeof(char) - 1;
      unsigned char tmp = bitmap[idx];
      // Shift until tmp's LSB is 0, each shift is one occupied inode
      while (tmp & 0x1) {
        tmp >>= 1;
        num_shifts++;
      }
      // free_inode_number now points to the first free inode
      free_bit_number += num_shifts;
      bitmap_byte = idx;
      return;
    }
  }
}

int LocalFileSystem::lookup(int parentInodeNumber, string name) {
  // Make sure name is valid
  if (name.length() <= 0 || name.length() > 27) {
    return -ENOTFOUND;
  }
  // Assume 0-based inode indexing
  super_t super = super_t();
  readSuperBlock(&super);
  if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  }

  unsigned char inode_bitmap[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  readInodeBitmap(&super, inode_bitmap);

  // Check if parent inode is allocated
  int byte_offset = parentInodeNumber % 8;
  int bitmap_byte = parentInodeNumber / 8;
  char bitmask = 0b1 << byte_offset;
  if (!(inode_bitmap[bitmap_byte] & bitmask)) {
    return -EINVALIDINODE;
  }

  // Check if parent inode is a directory inode
  inode_t inodes[super.num_inodes];
  readInodeRegion(&super, inodes);
  if (inodes[parentInodeNumber].type != UFS_DIRECTORY) {
    return -EINVALIDINODE;
  }

  // Check if name exists in directory
  dir_ent_t buffer[UFS_BLOCK_SIZE / sizeof(dir_ent_t)];
  int entries_processed = 0;
  int total_entries = inodes[parentInodeNumber].size / sizeof(dir_ent_t);
  for (unsigned int dp : inodes[parentInodeNumber].direct) {
    disk->readBlock(dp, buffer);
    for (unsigned int idx = 0; idx < UFS_BLOCK_SIZE / sizeof(dir_ent_t);
         idx++) {
      if (buffer[idx].name == name) {
        return buffer[idx].inum;
      } else if (++entries_processed == total_entries) {
        // Searched all entries without finding name
        return -ENOTFOUND;
      }
    }
  }
  return -ENOTFOUND;
}

int LocalFileSystem::stat(int inodeNumber, inode_t *inode) {
  // Assume 0-based inode indexing
  super_t super = super_t();
  readSuperBlock(&super);
  if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  }

  unsigned char inode_bitmap[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  readInodeBitmap(&super, inode_bitmap);

  // Check if inode is allocated
  int byte_offset = inodeNumber % 8;
  int bitmap_byte = inodeNumber / 8;
  char bitmask = 0b1 << byte_offset;
  if (!(inode_bitmap[bitmap_byte] & bitmask)) {
    return -EINVALIDINODE;
  }

  inode_t inodes[super.num_inodes];
  readInodeRegion(&super, inodes);
  *inode = inodes[inodeNumber];
  return 0;
}

int LocalFileSystem::read(int inodeNumber, void *buffer, int size) {
  // 1. Check existence of inode
  super_t super = super_t();
  readSuperBlock(&super);
  if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  }

  unsigned char inode_bitmap[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  readInodeBitmap(&super, inode_bitmap);

  // Check if inode is allocated
  int byte_offset = inodeNumber % 8;
  int bitmap_byte = inodeNumber / 8;
  char bitmask = 0b1 << byte_offset;
  if (!(inode_bitmap[bitmap_byte] & bitmask)) {
    return -EINVALIDINODE;
  }
  // 2. Check inode type and requirements
  // Read in inodes
  inode_t inodes[super.inode_region_len * UFS_BLOCK_SIZE];
  readInodeRegion(&super, inodes);
  inode_t inode = inodes[inodeNumber];
  if (inode.type == UFS_DIRECTORY && size % sizeof(dir_ent_t)) {
    return -EINVALIDSIZE;
  }
  // 3. Copy to buffer
  int partial_block_size = inode.size % UFS_BLOCK_SIZE;
  int num_blocks = inode.size / UFS_BLOCK_SIZE;
  unsigned int bytes_read = 0;
  char *buf_offset = static_cast<char *>(buffer);
  char read_buf[UFS_BLOCK_SIZE];

  for (int idx = 0; idx < num_blocks; idx++) {
    // Read block
    disk->readBlock(super.data_region_addr + inode.direct[idx++], read_buf);
    if (size > UFS_BLOCK_SIZE) {
      // Can copy the whole block
      memcpy(buf_offset, read_buf, UFS_BLOCK_SIZE);
      buf_offset += UFS_BLOCK_SIZE;
      bytes_read += UFS_BLOCK_SIZE;
      size -= UFS_BLOCK_SIZE;
    } else {
      // Copy size bytes and return
      memcpy(buf_offset, read_buf, size);
      // Read size total bytes
      return bytes_read + size;
    }
  }

  // Read last block if it exists and there's still space in the buffer
  if (partial_block_size && size) {
    disk->readBlock(inode.direct[num_blocks], read_buf);
    if (size <= partial_block_size) {
      memcpy(buf_offset, read_buf, size);
      bytes_read += size;
    } else {
      memcpy(buf_offset, read_buf, partial_block_size);
      bytes_read += partial_block_size;
    }
  }
  return bytes_read;
}

int LocalFileSystem::create(int parentInodeNumber, int type, string name) {
  // Assume 0-based inode indexing
  // Make sure name is valid
  if (name.length() <= 0 || name.length() > 27) {
    return -EINVALIDNAME;
  }
  super_t super = super_t();
  readSuperBlock(&super);
  if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  }

  int inode_bitmap_size = super.inode_bitmap_len * UFS_BLOCK_SIZE;
  unsigned char inode_bitmap[inode_bitmap_size];
  readInodeBitmap(&super, inode_bitmap);

  // Check if parent inode is allocated
  int byte_offset = parentInodeNumber % 8;
  int bitmap_byte = parentInodeNumber / 8;
  char bitmask = 0b1 << byte_offset;
  if (!(inode_bitmap[bitmap_byte] & bitmask)) {
    return -EINVALIDINODE;
  }

  // Check if parent inode is a directory inode
  inode_t inodes[super.num_inodes];
  readInodeRegion(&super, inodes);
  inode_t &parent_inode = inodes[parentInodeNumber];
  if (parent_inode.type != UFS_DIRECTORY) {
    return -EINVALIDINODE;
  }

  // Check if name exists in parent directory
  dir_ent_t buffer[UFS_BLOCK_SIZE / sizeof(dir_ent_t)];
  int entries_processed = 0;
  int total_entries = parent_inode.size / sizeof(dir_ent_t);
  for (unsigned int dp : parent_inode.direct) {
    disk->readBlock(super.data_region_addr + dp, buffer);
    for (unsigned int idx = 0; idx < UFS_BLOCK_SIZE / sizeof(dir_ent_t);
         idx++) {
      if (buffer[idx].name == name) {
        // Inode exists, check type
        if (inodes[buffer[idx].inum].type == type)
          return buffer[idx].inum;
        else
          return -EINVALIDTYPE;
      } else if (++entries_processed == total_entries) {
        // Searched all entries without finding name
        // Create new inode
        disk->beginTransaction();
        // 1. Find first free inode in bitmap
        int free_inode_number = -1;
        int num_shifts = -1;
        int bitmap_byte = -1;
        __find_free_bit(inode_bitmap_size, inode_bitmap, free_inode_number,
                        num_shifts, bitmap_byte);
        if (free_inode_number < 0) {
          // No free inode
          disk->rollback();
          return -ENOTENOUGHSPACE;
        }
        // Pre-allocate inode
        inode_bitmap[bitmap_byte] |= 0x1 << num_shifts;

        // 2. Find first free block in data region
        int data_bitmap_size = super.data_bitmap_len * UFS_BLOCK_SIZE;
        unsigned char data_bitmap[data_bitmap_size];
        readDataBitmap(&super, data_bitmap);
        int free_data_number = -1;
        __find_free_bit(data_bitmap_size, data_bitmap, free_data_number,
                        num_shifts, bitmap_byte);
        if (free_data_number < 0) {
          // No free data block
          disk->rollback();
          return -ENOTENOUGHSPACE;
        }
        // Preallocate data block
        data_bitmap[bitmap_byte] |= 0x1 << num_shifts;

        // 3. Create inode
        inodes[free_inode_number] = inode_t();
        inodes[free_inode_number].size =
            type == UFS_REGULAR_FILE ? 0 : 2 * sizeof(dir_ent_t);
        inodes[free_inode_number].type = type;
        inodes[free_inode_number].direct[0] = free_data_number;
        writeInodeRegion(&super, inodes);

        // 4. Create directory entry in parent directory
        int parent_direct =
            parent_inode.direct[parent_inode.size / UFS_BLOCK_SIZE];
        int block_offset = parent_inode.size % UFS_BLOCK_SIZE;
        if (block_offset == 0) {
          // Allocate new data block for parent if possible
          int parent_block_number = -1;
          __find_free_bit(data_bitmap_size, data_bitmap, parent_block_number,
                          num_shifts, bitmap_byte);
          if (parent_block_number < 0) {
            // No free data block for parent
            disk->rollback();
            return -ENOTENOUGHSPACE;
          }
          // Allocate data block
          data_bitmap[bitmap_byte] |= 0x1 << num_shifts;
          // Update inode
          parent_inode.direct[parent_inode.size / UFS_BLOCK_SIZE + 1] =
              parent_block_number;
          parent_direct = parent_block_number;
        }
        // Update inode
        parent_inode.size += sizeof(dir_ent_t);
        // Read directory data block
        dir_ent_t parent_dir_ent_block[UFS_BLOCK_SIZE / sizeof(dir_ent_t)];
        disk->readBlock(super.data_region_addr + parent_direct,
                        parent_dir_ent_block);
        // Create new directory entry
        parent_dir_ent_block[block_offset] = dir_ent_t();
        parent_dir_ent_block[block_offset].inum = free_inode_number;
        strcpy(parent_dir_ent_block[block_offset].name, name.c_str());

        // 5. Create . and .. entries if type is directory
        if (type == UFS_DIRECTORY) {
          // New block, no need to read
          dir_ent_t child_dir_ent_block[UFS_BLOCK_SIZE / sizeof(dir_ent_t)];
          child_dir_ent_block[0] = dir_ent_t();
          child_dir_ent_block[0].inum = free_inode_number;
          strcpy(child_dir_ent_block[0].name, ".");
          child_dir_ent_block[1] = dir_ent_t();
          child_dir_ent_block[1].inum = parentInodeNumber;
          strcpy(child_dir_ent_block[1].name, "..");
          // Write here instead of adding another conditional later
          disk->writeBlock(super.data_region_addr + free_data_number,
                           child_dir_ent_block);
        }

        // Write changes
        writeInodeBitmap(&super, inode_bitmap);
        writeInodeRegion(&super, inodes);
        writeDataBitmap(&super, data_bitmap);
        disk->writeBlock(super.data_region_addr + parent_direct,
                         parent_dir_ent_block);

        // Commit
        disk->commit();
        return free_inode_number;
      }
    }
  }
  return -EINVALIDNAME;
}

int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  return 0;
}

int LocalFileSystem::unlink(int parentInodeNumber, string name) {
  if (name == "." || name == "..") {
    return -EUNLINKNOTALLOWED;
  }
  return 0;
}
