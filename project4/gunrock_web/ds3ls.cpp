#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "include/Disk.h"
#include "include/LocalFileSystem.h"
#include "include/ufs.h"
#include "shared/include/StringUtils.h"

using namespace std;

// Use this function with std::sort for directory entries
bool compareByName(const dir_ent_t &a, const dir_ent_t &b) {
  return strcmp(a.name, b.name) < 0;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    cerr << argv[0] << ": diskImageFile directory" << endl;
    cerr << "For example:" << endl;
    cerr << "    $ " << argv[0] << " tests/disk_images/a.img /a/b" << endl;
    return 1;
  }

  // parse command line arguments
  unique_ptr<Disk> disk = make_unique<Disk>(argv[1], UFS_BLOCK_SIZE);
  unique_ptr<LocalFileSystem> fileSystem =
      make_unique<LocalFileSystem>(disk.get());
  string directory = string(argv[2]);

  if (directory.size() > 0 && directory[0] == '/') {
    // Find inode of directory
    int inode_num = UFS_ROOT_DIRECTORY_INODE_NUMBER;
    int parent_inode_num = inode_num;
    if (directory.size() != 1) {
      string segment = "";
      for (auto iter = directory.begin() + 1; iter != directory.end(); iter++) {
        if (*iter == '/') {
          parent_inode_num = inode_num;
          inode_num = fileSystem->lookup(inode_num, segment);
          if (inode_num < 0) {
            cerr << "Directory not found" << endl;
            return 1;
          }
          segment = "";
        } else {
          segment += *iter;
        }
      }
      // One more time for last path segment
      parent_inode_num = inode_num;
      inode_num = fileSystem->lookup(inode_num, segment);
      if (inode_num < 0) {
        cerr << "Directory not found" << endl;
        return 1;
      }
    }

    // Create inode struct
    inode_t inode = inode_t();
    if (fileSystem->stat(inode_num, &inode)) {
      cerr << "Directory not found" << endl;
      return 1;
    }

    // List directory
    int bytes_read = 0;
    if (inode.type == UFS_REGULAR_FILE) {
      // Create parent inode
      inode_t parent_inode = inode_t();
      if (fileSystem->stat(parent_inode_num, &parent_inode)) {
        cerr << "Directory not found" << endl;
        return 1;
      }
      // Get parent inode directory entries
      int num_entries = parent_inode.size / sizeof(dir_ent_t);
      dir_ent_t buffer[num_entries];
      bytes_read =
          fileSystem->read(parent_inode_num, buffer, parent_inode.size);
      if (bytes_read != parent_inode.size) {
        cerr << "Directory not found" << endl;
        return 1;
      }
      // Search for inode entry to get filename
      for (int idx = 0; idx < num_entries; idx++) {
        if (buffer[idx].inum == inode_num) {
          cout << inode_num << '\t' << buffer[idx].name << endl;
          return 0;
        }
      }
    } else {
      // Get directory entries
      int num_entries = inode.size / sizeof(dir_ent_t);
      dir_ent_t buffer[num_entries];
      bytes_read = fileSystem->read(inode_num, buffer, inode.size);
      if (bytes_read != inode.size) {
        cerr << "Directory not found" << endl;
        return 1;
      }

      // Print sorted list
      sort(buffer, buffer + num_entries, compareByName);
      for (int idx = 0; idx < num_entries; idx++) {
        cout << buffer[idx].inum << '\t' << buffer[idx].name << endl;
      }
    }
  } else {
    cerr << "Directory not found" << endl;
    return 1;
  }

  return 0;
}
