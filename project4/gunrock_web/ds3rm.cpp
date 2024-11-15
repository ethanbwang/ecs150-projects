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
  if (argc != 4) {
    cerr << argv[0] << ": diskImageFile parentInode entryName" << endl;
    return 1;
  }

  // Parse command line arguments
  unique_ptr<Disk> disk = make_unique<Disk>(argv[1], UFS_BLOCK_SIZE);
  unique_ptr<LocalFileSystem> fileSystem =
      make_unique<LocalFileSystem>(disk.get());
  int parentInode = stoi(argv[2]);
  string entryName = string(argv[3]);

  if (fileSystem->unlink(parentInode, entryName) < 0) {
    cerr << "Could not remove file or directory" << endl;
    return 1;
  }

  return 0;
}
