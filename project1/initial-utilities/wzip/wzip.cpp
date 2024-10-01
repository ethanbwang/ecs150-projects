#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc == 1) {
    // No args
    write(STDOUT_FILENO, "wzip: file1 [file2 ...]\n", 24);
    return 1;
  } else {
    // Loop through each file name provided
    int file_descriptor;
    int read_bytes;
    char r_buf[4096];
    char cur_char = '\0';
    uint32_t occurs = 0;
    char occurs_bin[4];
    std::vector<char> w_buf{};

    for (int i = 1; i < argc; i++) {
      file_descriptor = open(argv[i], O_RDONLY);
      if (file_descriptor == -1) {
        write(STDOUT_FILENO, "wzip: cannot open file\n", 23);
        return 1;
      }

      // Run-length encoding algorithm
      while ((read_bytes = read(file_descriptor, r_buf, sizeof(r_buf))) > 0) {
        // Set cur_char to the first char in the buffer
        // Only run on the first read call
        if (cur_char == '\0') {
          cur_char = r_buf[0];
        }
        for (int i = 0; i < read_bytes; i++) {
          if (cur_char != r_buf[i]) {
            std::memcpy(&occurs_bin, &occurs, sizeof(occurs));
            w_buf.insert(w_buf.end(), occurs_bin, occurs_bin + 4);
            w_buf.push_back(cur_char);
            cur_char = r_buf[i];
            occurs = 1;
          } else {
            occurs++;
          }
        }
      }

      if (file_descriptor != STDIN_FILENO)
        close(file_descriptor);

      if (read_bytes == -1) {
        write(STDOUT_FILENO, "wzip: invalid read operation\n", 30);
        return 1;
      }
    }

    std::memcpy(&occurs_bin, &occurs, sizeof(occurs));
    w_buf.insert(w_buf.end(), occurs_bin, occurs_bin + 4);
    w_buf.push_back(cur_char);

    int written = write(STDOUT_FILENO, w_buf.data(), w_buf.size());
    if (written == -1) {
      write(STDOUT_FILENO, "wzip: invalid write operation\n", 31);
      return 1;
    }
  }

  return 0;
}
