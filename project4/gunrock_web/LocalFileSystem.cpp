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
  int total_entries = inodes[parentInodeNumber].size / sizeof(dir_ent_t);
  dir_ent_t buffer[total_entries];
  read(parentInodeNumber, buffer, inodes[parentInodeNumber].size);
  for (int idx = 0; idx < total_entries; idx++) {
    if (buffer[idx].name == name) {
      return buffer[idx].inum;
    }
  }
  // Searched all entries without finding name
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
  if (size < 0) {
    return -EINVALIDSIZE;
  }
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
  int total_entries = parent_inode.size / sizeof(dir_ent_t);
  dir_ent_t buffer[total_entries];
  read(parentInodeNumber, buffer, parent_inode.size);
  for (int idx = 0; idx < total_entries; idx++) {
    if (buffer[idx].name == name) {
      // Inode exists, check type
      if (inodes[buffer[idx].inum].type == type)
        return buffer[idx].inum;
      else
        return -EINVALIDTYPE;
    }
  }
  // Searched all entries without finding name
  // Create new inode
  // 1. Find first free inode in bitmap
  int free_inode_number = -1;
  int num_shifts = -1;
  __find_free_bit(inode_bitmap_size, inode_bitmap, free_inode_number,
                  num_shifts, bitmap_byte);
  if (free_inode_number < 0) {
    // No free inode
    return -ENOTENOUGHSPACE;
  }
  // Pre-allocate inode
  inode_bitmap[bitmap_byte] |= 0x1 << num_shifts;

  // 2. Find first free block in data region
  int data_bitmap_size = super.data_bitmap_len * UFS_BLOCK_SIZE;
  unsigned char data_bitmap[data_bitmap_size];
  readDataBitmap(&super, data_bitmap);
  int free_data_number = -1;
  __find_free_bit(data_bitmap_size, data_bitmap, free_data_number, num_shifts,
                  bitmap_byte);
  if (free_data_number < 0) {
    // No free data block
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
  int parent_direct = parent_inode.direct[parent_inode.size / UFS_BLOCK_SIZE];
  int block_offset = parent_inode.size % UFS_BLOCK_SIZE;
  if (block_offset == 0) {
    // Allocate new data block for parent if possible
    int parent_block_number = -1;
    __find_free_bit(data_bitmap_size, data_bitmap, parent_block_number,
                    num_shifts, bitmap_byte);
    if (parent_block_number < 0) {
      // No free data block for parent
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
  disk->readBlock(super.data_region_addr + parent_direct, parent_dir_ent_block);
  // Create new directory entry
  parent_dir_ent_block[block_offset] = dir_ent_t();
  parent_dir_ent_block[block_offset].inum = free_inode_number;
  strcpy(parent_dir_ent_block[block_offset].name, name.c_str());

  // 5. Create . and .. entries if type is directory
  // Begin transaction here since writeBlock may be called in conditional
  disk->beginTransaction();
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

  // 6. Write changes
  writeInodeBitmap(&super, inode_bitmap);
  writeInodeRegion(&super, inodes);
  writeDataBitmap(&super, data_bitmap);
  disk->writeBlock(super.data_region_addr + parent_direct,
                   parent_dir_ent_block);

  // Commit
  disk->commit();
  return free_inode_number;
}

int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  if (size < 0) {
    return -EINVALIDSIZE;
  }
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

  // Check if parent inode is a directory inode
  inode_t inodes[super.num_inodes];
  readInodeRegion(&super, inodes);
  inode_t &inode = inodes[inodeNumber];
  if (inode.type == UFS_DIRECTORY) {
    return -EINVALIDTYPE;
  }

  int bytes_written = 0;
  // 1. Check if more data blocks are required, allocate more data blocks if
  // necessary
  int num_blocks = inode.size / UFS_BLOCK_SIZE;
  if (inode.size % UFS_BLOCK_SIZE) {
    num_blocks++;
  }
  int num_req_blocks = size / UFS_BLOCK_SIZE;
  if (size % UFS_BLOCK_SIZE) {
    num_req_blocks++;
  }
  // Allocate data blocks if necessary
  disk->beginTransaction();
  if (num_req_blocks > num_blocks) {
    int data_bitmap_size = super.data_bitmap_len * UFS_BLOCK_SIZE;
    unsigned char data_bitmap[data_bitmap_size];
    readDataBitmap(&super, data_bitmap);
    int free_block_num = -1;
    int num_shifts = 0;
    for (int idx = 0; idx < num_req_blocks - num_blocks; idx++) {
      __find_free_bit(data_bitmap_size, data_bitmap, free_block_num, num_shifts,
                      bitmap_byte);
      if (free_block_num < 0) {
        // No more memory
        disk->rollback();
        return -EINVALIDSIZE;
      }
      // Preallocate data block
      data_bitmap[bitmap_byte] |= 0b1 << num_shifts;
      // Add data block number to inode's direct pointer list
      inode.direct[num_blocks++] = free_block_num;
    }
    // Write bitmap
    writeDataBitmap(&super, data_bitmap);
  }
  // Update inode size
  inode.size = size;

  // 2. Write to disk
  writeInodeRegion(&super, inodes);
  const char *buffer_p = static_cast<const char *>(buffer);
  for (int idx = 0; idx < num_blocks; idx++) {
    const void *tmp = static_cast<const void *>(buffer_p);
    disk->writeBlock(inode.direct[idx], const_cast<void *>(tmp));
    buffer_p += UFS_BLOCK_SIZE;
  }

  disk->commit();
  return bytes_written;
}

int LocalFileSystem::unlink(int parentInodeNumber, string name) {
  // Make sure name is valid
  if (name.length() <= 0 || name.length() > 27) {
    return -EINVALIDNAME;
  }
  if (name == "." || name == "..") {
    return -EUNLINKNOTALLOWED;
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
  inode_t parent_inode = inodes[parentInodeNumber];
  if (parent_inode.type != UFS_DIRECTORY) {
    return -EINVALIDINODE;
  }

  // Check if name exists in directory
  int total_entries = parent_inode.size / sizeof(dir_ent_t);
  dir_ent_t buffer[total_entries];
  read(parentInodeNumber, buffer, parent_inode.size);
  for (int idx = 0; idx < total_entries; idx++) {
    if (buffer[idx].name == name) {
      // 1. If inode type is directory, make sure it's empty
      inode_t inode = inodes[buffer[idx].inum];
      if (inode.type == UFS_DIRECTORY &&
          inode.size > static_cast<int>(2 * sizeof(dir_ent_t))) {
        return -EDIRNOTEMPTY;
      }

      // 2. For each direct pointer in inode, unallocate representative bit on
      // bitmap
      unsigned char data_bitmap[UFS_BLOCK_SIZE * super.data_bitmap_len];
      readDataBitmap(&super, data_bitmap);
      int num_blocks = inode.size / UFS_BLOCK_SIZE;
      if (inode.size % UFS_BLOCK_SIZE) {
        num_blocks++;
      }
      // Unset each data bit
      for (int idx = 0; idx < num_blocks; idx++) {
        byte_offset = inode.direct[idx] % 8;
        bitmap_byte = inode.direct[idx] / 8;
        data_bitmap[bitmap_byte] &= ~(0b1 << byte_offset);
      }

      // 3. Unallocate inode
      byte_offset = buffer[idx].inum % 8;
      bitmap_byte = buffer[idx].inum / 8;
      inode_bitmap[bitmap_byte] &= ~(0b1 << byte_offset);

      // 4. Remove directory entry from parent and reduce parent size by
      // sizeof(dir_ent_t)
      // Find directory entry and replace with last directory entry
      num_blocks = parent_inode.size / UFS_BLOCK_SIZE;
      if (parent_inode.size % UFS_BLOCK_SIZE) {
        num_blocks++;
      }
      int num_ents = parent_inode.size / sizeof(dir_ent_t);
      dir_ent_t dir_ents[num_ents];
      // Read all entries into memory
      read(parentInodeNumber, dir_ents, num_ents);
      if (dir_ents[num_ents - 1].name != name) {
        // Find directory entry
        for (int idx = 0; idx < num_ents; idx++) {
          if (dir_ents[idx].name == name) {
            // Replace with last entry to avoid shifting all entries forward
            dir_ents[idx] = dir_ents[num_ents - 1];
            break;
          }
        }
      }
      // Reduce parent size
      parent_inode.size -= sizeof(dir_ent_t);
      // If parent size now spans fewer blocks, unallocate last data block
      byte_offset = parent_inode.direct[num_blocks - 1] % 8;
      bitmap_byte = parent_inode.direct[num_blocks - 1] / 8;
      data_bitmap[bitmap_byte] &= ~(0b1 << byte_offset);
      num_blocks--;

      // 5. Write to disk
      disk->beginTransaction();
      writeInodeBitmap(&super, inode_bitmap);
      writeInodeRegion(&super, inodes);
      writeDataBitmap(&super, data_bitmap);
      // Write new directory entries
      int ents_per_block = UFS_BLOCK_SIZE / sizeof(dir_ent_t);
      dir_ent_t *dir_ents_p = dir_ents;
      for (int idx = 0; idx < num_blocks; idx++) {
        disk->writeBlock(parent_inode.direct[idx], dir_ents_p);
        dir_ents_p += ents_per_block;
      }
      disk->commit();
    }
  }
  // Either unlinked file or didn't find name in directory
  return 0;
}
