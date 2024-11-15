#include <iostream>
#include <memory>
#include <string>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "include/Disk.h"
#include "include/LocalFileSystem.h"
#include "include/ufs.h"

using namespace std;

int main(int argc, char *argv[]) {
  if (argc != 4) {
    cerr << argv[0] << ": diskImageFile src_file dst_inode" << endl;
    cerr << "For example:" << endl;
    cerr << "    $ " << argv[0] << " tests/disk_images/a.img dthread.cpp 3"
         << endl;
    return 1;
  }

  // Parse command line arguments
  unique_ptr<Disk> disk = make_unique<Disk>(argv[1], UFS_BLOCK_SIZE);
  unique_ptr<LocalFileSystem> fileSystem =
      make_unique<LocalFileSystem>(disk.get());
  string srcFile = string(argv[2]);
  int dstInode = stoi(argv[3]);

  int local_fp = open(srcFile.c_str(), O_RDONLY);
  if (local_fp < 0) {
    cerr << "Failed to open file" << endl;
    return 1;
  }

  char r_buf[4096];
  string w_buf = "";
  int bytes_read = 0;
  while ((bytes_read = read(local_fp, r_buf, sizeof(r_buf))) > 0) {
    w_buf += r_buf;
  }

  if (static_cast<unsigned int>(fileSystem->write(
          dstInode, w_buf.c_str(), w_buf.length())) != w_buf.length()) {
    cerr << "Could not write to dst_file" << endl;
    return 1;
  }

  close(local_fp);

  if (bytes_read < 0) {
    cerr << "Read error" << endl;
    return 1;
  }

  return 0;
}
