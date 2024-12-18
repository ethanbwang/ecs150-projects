#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "include/Disk.h"
#include "include/LocalFileSystem.h"
#include "include/ufs.h"

using namespace std;

int main(int argc, char *argv[]) {
  if (argc != 4) {
    cerr << argv[0] << ": diskImageFile parentInode fileName" << endl;
    cerr << "For example:" << endl;
    cerr << "    $ " << argv[0] << " a.img 0 a.txt" << endl;
    return 1;
  }

  // Parse command line arguments
  unique_ptr<Disk> disk = make_unique<Disk>(argv[1], UFS_BLOCK_SIZE);
  unique_ptr<LocalFileSystem> fileSystem =
      make_unique<LocalFileSystem>(disk.get());
  int parentInode = stoi(argv[2]);
  string fileName = string(argv[3]);

  disk->beginTransaction();
  if (fileSystem->create(parentInode, UFS_REGULAR_FILE, fileName) < 0) {
    disk->rollback();
    cerr << "Error creating file" << endl;
    return 1;
  }
  disk->commit();

  return 0;
}
