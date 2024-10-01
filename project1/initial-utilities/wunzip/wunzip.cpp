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
    write(STDOUT_FILENO, "wunzip: file1 [file2 ...]\n", 26);
    return 1;
  } else {
    // Loop through each file name provided
    int file_descriptor;
    int read_bytes;
    char r_buf[4095];
    uint32_t occurs;
    std::vector<char> w_buf{};

    for (int i = 1; i < argc; i++) {
      file_descriptor = open(argv[i], O_RDONLY);
      if (file_descriptor == -1) {
        write(STDOUT_FILENO, "wunzip: cannot open file\n", 25);
        return 1;
      }

      // Run-length decoding algorithm
      while ((read_bytes = read(file_descriptor, r_buf, sizeof(r_buf))) > 0) {
        for (int i = 0; i < read_bytes; i += 5) {
          // Assumes little-endianness
          // Reuse section of buffer as the four-byte integer
          std::memcpy(&occurs, &r_buf[i], sizeof(occurs));
          w_buf.insert(w_buf.end(), occurs, r_buf[i + 4]);
        }
      }

      if (file_descriptor != STDIN_FILENO)
        close(file_descriptor);

      if (read_bytes == -1) {
        write(STDOUT_FILENO, "wunzip: invalid read operation\n", 32);
        return 1;
      }
    }

    int written = write(STDOUT_FILENO, w_buf.data(), w_buf.size());
    if (written == -1) {
      write(STDOUT_FILENO, "wunzip: invalid write operation\n", 33);
      return 1;
    }
  }

  return 0;
}
