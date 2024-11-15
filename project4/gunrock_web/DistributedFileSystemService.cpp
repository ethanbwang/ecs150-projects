#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>

#include "DistributedFileSystemService.h"
#include "WwwFormEncodedDict.h"
#include "include/ClientError.h"
#include "include/ufs.h"

using namespace std;

DistributedFileSystemService::DistributedFileSystemService(string diskFile)
    : HttpService("/ds3/") {
  static Disk disk = Disk(diskFile, UFS_BLOCK_SIZE);
  static LocalFileSystem fileSystemObj = LocalFileSystem(&disk);
  this->fileSystem = &fileSystemObj;
  // this->fileSystem = new LocalFileSystem(new Disk(diskFile, UFS_BLOCK_SIZE));
}

void DistributedFileSystemService::get(HTTPRequest *request,
                                       HTTPResponse *response) {
  string path = request->getPath();
  // Check that path starts with /ds3/
  if (path.substr(0, 5) != pathPrefix()) {
    throw ClientError::badRequest();
  }
  // Remove /ds3
  path.erase(0, 4);
  if (path.find("..") != string::npos) {
    throw ClientError::badRequest();
    // response->setStatus(403);
    // return;
  }
  // Find inode of last path segment
  int inode_num = UFS_ROOT_DIRECTORY_INODE_NUMBER;
  if (path.length() > 1) {
    string segment = "";
    for (auto iter = path.begin() + 1; iter != path.end(); iter++) {
      if (*iter == '/') {
        inode_num = fileSystem->lookup(inode_num, segment);
        if (inode_num < 0) {
          throw ClientError::notFound();
        }
        segment = "";
      } else {
        segment += *iter;
      }
    }
    // One more time for last path segment
    if (segment.length()) {
      inode_num = fileSystem->lookup(inode_num, segment);
      if (inode_num < 0) {
        throw ClientError::notFound();
      }
    }
  }
  inode_t inode;
  fileSystem->stat(inode_num, &inode);
  if (inode.type == UFS_DIRECTORY) {
    // Read from disk
    int num_ents = inode.size / sizeof(dir_ent_t);
    dir_ent_t buffer[num_ents];
    int bytes_read = fileSystem->read(inode_num, buffer, inode.size);
    // Error check
    if (bytes_read < 0) {
      throw ClientError::badRequest();
    } else if (bytes_read == 0) {
      throw ClientError::notFound();
    }
    // Set response body
    string body = "";
    inode_t tmp;
    for (int idx = 0; idx < num_ents; idx++) {
      if (strcmp(buffer[idx].name, ".") && strcmp(buffer[idx].name, "..")) {
        body += buffer[idx].name;
        fileSystem->stat(buffer[idx].inum, &tmp);
        if (tmp.type == UFS_DIRECTORY) {
          body += "/";
        }
        body += "\n";
      }
    }
    response->setBody(body);
  } else {
    // Read from disk
    char buffer[inode.size];
    int bytes_read = fileSystem->read(inode_num, buffer, inode.size);
    // Error check
    if (bytes_read < 0) {
      throw ClientError::badRequest();
    } else if (bytes_read == 0) {
      throw ClientError::notFound();
    }
    // Set response body
    response->setBody(string(buffer, inode.size));
  }
}

void DistributedFileSystemService::put(HTTPRequest *request,
                                       HTTPResponse *response) {
  string path = request->getPath();
  // Check that path starts with /ds3/
  if (path.substr(0, 5) != pathPrefix()) {
    throw ClientError::badRequest();
  }
  // Remove /ds3
  path.erase(0, 4);
  if (path.find("..") != string::npos) {
    throw ClientError::badRequest();
    // response->setStatus(403);
    // return;
  }
  // Find inode of last path segment
  int inode_num = UFS_ROOT_DIRECTORY_INODE_NUMBER;
  if (path.length() > 1) {
    string segment = "";
    for (auto iter = path.begin() + 1; iter != path.end(); iter++) {
      if (*iter == '/') {
        inode_num = fileSystem->lookup(inode_num, segment);
        if (inode_num < 0) {
          throw ClientError::notFound();
        }
        segment = "";
      } else {
        segment += *iter;
      }
    }
    // One more time for last path segment
    if (segment.length()) {
      inode_num = fileSystem->lookup(inode_num, segment);
      if (inode_num < 0) {
        throw ClientError::notFound();
      }
    }
  }
  inode_t inode;
  fileSystem->stat(inode_num, &inode);
  response->setBody("");
}

void DistributedFileSystemService::del(HTTPRequest *request,
                                       HTTPResponse *response) {
  string path = request->getPath();
  // Check that path starts with /ds3/
  if (path.substr(0, 5) != pathPrefix()) {
    throw ClientError::badRequest();
  }
  // Remove /ds3
  path.erase(0, 4);
  if (path.find("..") != string::npos) {
    throw ClientError::badRequest();
    // response->setStatus(403);
    // return;
  }
  // Find inode of last path segment
  int inode_num = UFS_ROOT_DIRECTORY_INODE_NUMBER;
  int parent_inode_num = inode_num;
  string filename = "";
  if (path.length() > 1) {
    string segment = "";
    for (auto iter = path.begin() + 1; iter != path.end(); iter++) {
      if (*iter == '/') {
        parent_inode_num = inode_num;
        inode_num = fileSystem->lookup(inode_num, segment);
        if (inode_num < 0) {
          throw ClientError::notFound();
        }
        filename = segment;
        segment = "";
      } else {
        segment += *iter;
      }
    }
    // One more time for last path segment
    if (segment.length()) {
      parent_inode_num = inode_num;
      inode_num = fileSystem->lookup(inode_num, segment);
      if (inode_num < 0) {
        throw ClientError::notFound();
      }
      filename = segment;
    }
  } else {
    // Tried to delete root
    throw ClientError::badRequest();
  }
  inode_t inode;
  fileSystem->stat(inode_num, &inode);
  // Delete
  fileSystem->disk->beginTransaction();
  if (fileSystem->unlink(parent_inode_num, filename) < 0) {
    fileSystem->disk->rollback();
    throw ClientError::badRequest();
  }
  fileSystem->disk->commit();
  response->setBody("");
}
