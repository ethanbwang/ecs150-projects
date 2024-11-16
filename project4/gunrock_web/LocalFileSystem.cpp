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
  int inodes_per_block = UFS_BLOCK_SIZE / sizeof(inode_t);
  for (int block_num = 0; block_num < super->inode_region_len; block_num++) {
    disk->readBlock(super->inode_region_addr + block_num,
                    inodes + inodes_per_block * block_num);
  }
}

void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes) {
  int inodes_per_block = UFS_BLOCK_SIZE / sizeof(inode_t);
  for (int block_num = 0; block_num < super->inode_region_len; block_num++) {
    disk->writeBlock(super->inode_region_addr + block_num,
                     inodes + inodes_per_block * block_num);
  }
}

void __find_free_bit(const int &bitmap_size, const int &num_entries,
                     unsigned char bitmap[], int &free_bit_number,
                     int &num_shifts, int &bitmap_byte) {
  int entries_parsed = 0;
  for (int idx = 0; idx < bitmap_size; idx++) {
    if (bitmap[idx] != 0xff) {
      free_bit_number = idx * 8;
      unsigned char tmp = bitmap[idx];
      // Shift until tmp's LSB is 0, each shift is one occupied inode
      while (tmp & 0x1) {
        tmp >>= 1;
        num_shifts++;
      }
      // free_inode_number now points to the first free inode
      // Check that it is a valid inode
      if (free_bit_number + num_shifts >= num_entries) {
        // No valid inodes available
        free_bit_number = -1;
        return;
      }
      free_bit_number += num_shifts;
      bitmap_byte = idx;
      return;
    }
    // 8 entries per byte
    entries_parsed += 8;
    if (entries_parsed >= num_entries) {
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
  inode_t inodes[super.inode_region_len * UFS_BLOCK_SIZE / sizeof(inode_t)];
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

  inode_t inodes[super.inode_region_len * UFS_BLOCK_SIZE / sizeof(inode_t)];
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
  inode_t inodes[super.inode_region_len * UFS_BLOCK_SIZE / sizeof(inode_t)];
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
    disk->readBlock(inode.direct[idx], read_buf);
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
  // Make sure type is valid
  if (type != UFS_DIRECTORY && type != UFS_REGULAR_FILE) {
    return -EINVALIDTYPE;
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
  inode_t inodes[super.inode_region_len * UFS_BLOCK_SIZE / sizeof(inode_t)];
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
      else {
        return -EINVALIDTYPE;
      }
    }
  }
  // Searched all entries without finding name
  // Create new inode
  // 1. Find first free inode in bitmap
  int free_inode_number = -1;
  int num_shifts = 0;
  __find_free_bit(inode_bitmap_size, super.num_inodes, inode_bitmap,
                  free_inode_number, num_shifts, bitmap_byte);
  if (free_inode_number < 0) {
    // No free inode
    return -ENOTENOUGHSPACE;
  }
  // Pre-allocate inode
  inode_bitmap[bitmap_byte] |= 0x1 << num_shifts;

  // 2. Find first free block in data region if directory
  int data_bitmap_size = super.data_bitmap_len * UFS_BLOCK_SIZE;
  unsigned char data_bitmap[data_bitmap_size];
  readDataBitmap(&super, data_bitmap);
  int free_data_number = -1;
  num_shifts = 0;
  if (type == UFS_DIRECTORY) {
    __find_free_bit(data_bitmap_size, super.num_data, data_bitmap,
                    free_data_number, num_shifts, bitmap_byte);
    if (free_data_number < 0) {
      // No free data block
      return -ENOTENOUGHSPACE;
    }
    // Preallocate data block
    data_bitmap[bitmap_byte] |= 0x1 << num_shifts;
  }

  // 3. Create inode
  inodes[free_inode_number] = inode_t();
  inodes[free_inode_number].size =
      type == UFS_REGULAR_FILE ? 0 : 2 * sizeof(dir_ent_t);
  inodes[free_inode_number].type = type;
  if (type == UFS_DIRECTORY) {
    inodes[free_inode_number].direct[0] =
        super.data_region_addr + free_data_number;
  }

  // 4. Create directory entry in parent directory
  int parent_direct = parent_inode.direct[parent_inode.size / UFS_BLOCK_SIZE];
  int block_offset = parent_inode.size % UFS_BLOCK_SIZE / sizeof(dir_ent_t);
  if (block_offset == 0) {
    // Allocate new data block for parent if possible
    int parent_block_number = -1;
    num_shifts = 0;
    __find_free_bit(data_bitmap_size, super.num_data, data_bitmap,
                    parent_block_number, num_shifts, bitmap_byte);
    if (parent_block_number < 0) {
      // No free data block for parent
      return -ENOTENOUGHSPACE;
    }
    // Allocate data block
    data_bitmap[bitmap_byte] |= 0x1 << num_shifts;
    // Update inode
    parent_inode.direct[parent_inode.size / UFS_BLOCK_SIZE + 1] =
        super.data_region_addr + parent_block_number;
    parent_direct = super.data_region_addr + parent_block_number;
  }
  // Update inode
  parent_inode.size += sizeof(dir_ent_t);
  // Read directory data block
  dir_ent_t parent_dir_ent[UFS_BLOCK_SIZE / sizeof(dir_ent_t)];
  disk->readBlock(parent_direct, parent_dir_ent);
  // Create new directory entry
  parent_dir_ent[block_offset] = dir_ent_t();
  parent_dir_ent[block_offset].inum = free_inode_number;
  strcpy(parent_dir_ent[block_offset].name, name.c_str());

  // 5. Create . and .. entries if type is directory
  // Begin transaction here since writeBlock may be called in conditional
  // disk->beginTransaction();
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
  disk->writeBlock(parent_direct, parent_dir_ent);

  // Commit
  // disk->commit();
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

  // Check if inode is a directory inode
  inode_t inodes[super.inode_region_len * UFS_BLOCK_SIZE / sizeof(inode_t)];
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
  // Allocate/deallocate data blocks if necessary
  // disk->beginTransaction();
  if (num_req_blocks > num_blocks) {
    // Allocate data blocks
    int data_bitmap_size = super.data_bitmap_len * UFS_BLOCK_SIZE;
    unsigned char data_bitmap[data_bitmap_size];
    readDataBitmap(&super, data_bitmap);
    int free_block_num = -1;
    int num_shifts = 0;
    for (int idx = num_blocks; idx < num_req_blocks; idx++) {
      num_shifts = 0;
      __find_free_bit(data_bitmap_size, super.num_data, data_bitmap,
                      free_block_num, num_shifts, bitmap_byte);
      if (free_block_num < 0) {
        break;
      }
      // Preallocate data block
      data_bitmap[bitmap_byte] |= 0b1 << num_shifts;
      // Add data block number to inode's direct pointer list
      inode.direct[idx] = super.data_region_addr + free_block_num;
      num_blocks++;
    }
    // Write bitmap
    writeDataBitmap(&super, data_bitmap);
    // Update inode size
    // Handle case where there wasn't enough memory
    inode.size = UFS_BLOCK_SIZE * num_blocks;
    if (size < inode.size) {
      // Able to allocate enough blocks
      inode.size = size;
    }
  } else if (num_req_blocks < num_blocks) {
    // Deallocate data blocks
    int data_bitmap_size = super.data_bitmap_len * UFS_BLOCK_SIZE;
    unsigned char data_bitmap[data_bitmap_size];
    readDataBitmap(&super, data_bitmap);
    int num_shifts = 0;
    for (int idx = num_blocks - 1; idx >= num_req_blocks; idx--) {
      // Deallocate data block
      int relative_block_num = inode.direct[idx] - super.data_region_addr;
      bitmap_byte = relative_block_num / 8;
      num_shifts = relative_block_num % 8;
      data_bitmap[bitmap_byte] &= ~(0b1 << num_shifts);
    }
    // Write bitmap
    writeDataBitmap(&super, data_bitmap);
    // Update inode size
    inode.size = size;
    num_blocks = num_req_blocks;
  } else {
    // Enough blocks
    inode.size = size;
  }

  // 2. Write to disk
  writeInodeRegion(&super, inodes);
  const char *buffer_p = static_cast<const char *>(buffer);
  for (int idx = 0; idx < num_blocks - 1; idx++) {
    disk->writeBlock(inode.direct[idx], const_cast<char *>(buffer_p));
    buffer_p += UFS_BLOCK_SIZE;
    bytes_written += UFS_BLOCK_SIZE;
  }
  // If last block isn't filled, need to avoid accessing bad memory
  char tmp_buf[UFS_BLOCK_SIZE];
  memcpy(tmp_buf, buffer_p, inode.size - bytes_written);
  disk->writeBlock(inode.direct[num_blocks - 1], tmp_buf);
  bytes_written = inode.size;

  // disk->commit();
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
  inode_t inodes[super.inode_region_len * UFS_BLOCK_SIZE / sizeof(inode_t)];
  readInodeRegion(&super, inodes);
  inode_t &parent_inode = inodes[parentInodeNumber];
  if (parent_inode.type != UFS_DIRECTORY) {
    return -EINVALIDINODE;
  }

  // Check if name exists in directory
  int num_ents = parent_inode.size / sizeof(dir_ent_t);
  int num_parent_blocks = parent_inode.size / UFS_BLOCK_SIZE;
  if (parent_inode.size % UFS_BLOCK_SIZE) {
    num_parent_blocks++;
  }
  int dir_ent_bytes = num_parent_blocks * UFS_BLOCK_SIZE;
  dir_ent_t dir_ents[dir_ent_bytes];
  read(parentInodeNumber, dir_ents, dir_ent_bytes);
  for (int idx = 0; idx < num_ents; idx++) {
    if (dir_ents[idx].name == name) {
      // 1. If inode type is directory, make sure it's empty
      inode_t inode = inodes[dir_ents[idx].inum];
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
        int relative_block_num = inode.direct[idx] - super.data_region_addr;
        byte_offset = relative_block_num % 8;
        bitmap_byte = relative_block_num / 8;
        data_bitmap[bitmap_byte] &= ~(0b1 << byte_offset);
      }

      // 3. Unallocate inode
      byte_offset = dir_ents[idx].inum % 8;
      bitmap_byte = dir_ents[idx].inum / 8;
      inode_bitmap[bitmap_byte] &= ~(0b1 << byte_offset);

      // 4. Remove directory entry from parent and reduce parent size by
      // sizeof(dir_ent_t)
      // Find directory entry and replace with last directory entry
      if (num_ents - 1 != idx) {
        // Replace with last entry to avoid shifting all entries forward
        dir_ents[idx] = dir_ents[num_ents - 1];
      }
      // Reduce parent size
      parent_inode.size -= sizeof(dir_ent_t);
      // If parent size now spans fewer blocks, unallocate last data block
      int new_num_blocks = parent_inode.size / UFS_BLOCK_SIZE;
      if (parent_inode.size % UFS_BLOCK_SIZE) {
        new_num_blocks++;
      }
      if (new_num_blocks < num_parent_blocks) {
        int relative_block_num =
            parent_inode.direct[num_parent_blocks - 1] - super.data_region_addr;
        byte_offset = relative_block_num % 8;
        bitmap_byte = relative_block_num / 8;
        data_bitmap[bitmap_byte] &= ~(0b1 << byte_offset);
        num_parent_blocks--;
      }

      // 5. Write to disk
      // disk->beginTransaction();
      writeInodeBitmap(&super, inode_bitmap);
      writeInodeRegion(&super, inodes);
      writeDataBitmap(&super, data_bitmap);
      // Write new directory entries
      int ents_per_block = UFS_BLOCK_SIZE / sizeof(dir_ent_t);
      dir_ent_t *dir_ents_p = dir_ents;
      for (int idx = 0; idx < num_parent_blocks; idx++) {
        disk->writeBlock(parent_inode.direct[idx], dir_ents_p);
        dir_ents_p += ents_per_block;
      }
      // disk->commit();
    }
  }
  // Either unlinked file or didn't find name in directory
  return 0;
}
