#include <cstdint>
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
    int ret;
    char buffer[4095];
    char cur_char;
    uint32_t *occurs;
    std::vector<char> w_buffer{};
    for (int i = 1; i < argc; i++) {
      file_descriptor = open(argv[i], O_RDONLY);
      if (file_descriptor == -1) {
        write(STDOUT_FILENO, "wunzip: cannot open file\n", 25);
        return 1;
      }

      // Run-length decoding algorithm
      while ((ret = read(file_descriptor, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < ret; i += 5) {
          // Assumes little-endianness
          // Reuse section of buffer as the four-byte integer
          occurs = (uint32_t *)&buffer[i];
          cur_char = buffer[i + 4];
          for (uint32_t j = 0; j < *occurs; j++) {
            w_buffer.push_back(cur_char);
          }
        }
      }

      if (file_descriptor != STDIN_FILENO)
        close(file_descriptor);

      if (ret == -1) {
        write(STDOUT_FILENO, "wunzip: invalid read operation\n", 32);
        return 1;
      }
    }
    int written = write(STDOUT_FILENO, w_buffer.data(), w_buffer.size());
    if (written == -1) {
      write(STDOUT_FILENO, "wunzip: invalid write operation\n", 33);
      return 1;
    }
  }

  return 0;
}
