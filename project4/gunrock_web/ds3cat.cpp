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
  if (argc != 3) {
    cerr << argv[0] << ": diskImageFile inodeNumber" << endl;
    return 1;
  }

  // Parse command line arguments
  unique_ptr<Disk> disk = make_unique<Disk>(argv[1], UFS_BLOCK_SIZE);
  unique_ptr<LocalFileSystem> fileSystem =
      make_unique<LocalFileSystem>(disk.get());
  int inodeNumber = stoi(argv[2]);

  // Get data
  inode_t inode;
  if (fileSystem->stat(inodeNumber, &inode) || inode.type == UFS_DIRECTORY) {
    cerr << "Error reading file" << endl;
    return 1;
  }
  int num_blocks = inode.size / UFS_BLOCK_SIZE;
  if (inode.size % UFS_BLOCK_SIZE) {
    num_blocks++;
  }

  // Print disk block numbers
  cout << "File blocks\n";
  for (int idx = 0; idx < num_blocks; idx++)
    cout << inode.direct[idx] << '\n';
  cout << '\n';

  // Print file contents
  cout << "File data\n";
  char file_contents[inode.size];
  if (fileSystem->read(inodeNumber, file_contents, inode.size) != inode.size) {
    cerr << "Error reading file" << endl;
    return 1;
  }
  cout.write(file_contents, inode.size);

  return 0;
}
