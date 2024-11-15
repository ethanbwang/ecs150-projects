#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "include/Disk.h"
#include "include/LocalFileSystem.h"
#include "include/ufs.h"

using namespace std;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    cerr << argv[0] << ": diskImageFile" << endl;
    return 1;
  }

  // Parse command line arguments
  unique_ptr<Disk> disk = make_unique<Disk>(argv[1], UFS_BLOCK_SIZE);
  unique_ptr<LocalFileSystem> fileSystem =
      make_unique<LocalFileSystem>(disk.get());

  // Get metadata
  super_t super;
  fileSystem->readSuperBlock(&super);
  unsigned char inode_bitmap[super.inode_region_len * UFS_BLOCK_SIZE];
  fileSystem->readInodeBitmap(&super, inode_bitmap);
  unsigned char data_bitmap[super.data_region_len * UFS_BLOCK_SIZE];
  fileSystem->readDataBitmap(&super, data_bitmap);

  // Print filesystem metadata
  cout << "Super\n"
       << "inode_region_addr " << super.inode_region_addr << '\n'
       << "inode_region_len " << super.inode_region_len << '\n'
       << "num_inodes " << super.num_inodes << '\n'
       << "data_region_addr " << super.data_region_addr << '\n'
       << "data_region_len " << super.data_region_len << '\n'
       << "num_data " << super.num_data << '\n'
       << '\n';

  // Print inode and data bitmaps
  cout << "Inode bitmap\n";
  int num_bytes = super.num_inodes / 8;
  if (super.num_inodes % 8)
    num_bytes++;
  for (int idx = 0; idx < num_bytes; idx++)
    cout << static_cast<unsigned int>(inode_bitmap[idx]) << ' ';

  cout << "\n\nData bitmap\n";
  num_bytes = super.num_data / 8;
  if (super.num_data % 8)
    num_bytes++;
  for (int idx = 0; idx < num_bytes; idx++)
    cout << static_cast<unsigned int>(data_bitmap[idx]) << ' ';
  cout << '\n';

  return 0;
}
