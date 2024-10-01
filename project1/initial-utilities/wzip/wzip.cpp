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
    write(STDOUT_FILENO, "wzip: file1 [file2 ...]\n", 24);
    return 1;
  } else {
    // Loop through each file name provided
    int file_descriptor;
    int ret;
    char buffer[4096];
    char cur_char = '\0';
    uint32_t occurs = 0;
    std::vector<char> w_buffer{};
    for (int i = 1; i < argc; i++) {
      file_descriptor = open(argv[i], O_RDONLY);
      if (file_descriptor == -1) {
        write(STDOUT_FILENO, "wzip: cannot open file\n", 23);
        return 1;
      }

      // Run-length encoding algorithm
      while ((ret = read(file_descriptor, buffer, sizeof(buffer))) > 0) {
        // Set cur_char to the first char in the buffer
        // Only run on the first read call
        if (cur_char == '\0') {
          cur_char = buffer[0];
        }
        for (int i = 0; i < ret; i++) {
          if (cur_char != buffer[i]) {
            char *occurs_str = (char *)&occurs;
            w_buffer.insert(w_buffer.end(), occurs_str, occurs_str + 4);
            w_buffer.push_back(cur_char);
            cur_char = buffer[i];
            occurs = 1;
          } else {
            occurs++;
          }
        }
      }

      if (file_descriptor != STDIN_FILENO)
        close(file_descriptor);

      if (ret == -1) {
        write(STDOUT_FILENO, "wzip: invalid read operation\n", 30);
        return 1;
      }
    }
    char *occurs_str = (char *)&occurs;
    w_buffer.insert(w_buffer.end(), occurs_str, occurs_str + 4);
    w_buffer.push_back(cur_char);
    int written = write(STDOUT_FILENO, w_buffer.data(), w_buffer.size());
    if (written == -1) {
      write(STDOUT_FILENO, "wzip: invalid write operation\n", 31);
      return 1;
    }
  }

  return 0;
}
