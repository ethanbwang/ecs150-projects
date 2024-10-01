#include <iostream>

#include <fcntl.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc > 1) {
    int file_descriptor;
    for (int i = 1; i < argc; i++) {
      file_descriptor = open(argv[i], O_RDONLY);
      if (file_descriptor == -1) {
        write(STDOUT_FILENO, "wcat: cannot open file\n", 23);
        return 1;
      }

      int read_bytes;
      char r_buf[4096];

      while ((read_bytes = read(file_descriptor, r_buf, sizeof(r_buf))) > 0) {
        int bytes_written = write(STDOUT_FILENO, r_buf, read_bytes);
        if (bytes_written != read_bytes) {
          write(STDOUT_FILENO, "wcat: invalid write operation\n", 30);
          return 1;
        }
      }

      if (file_descriptor != STDIN_FILENO)
        close(file_descriptor);

      if (read_bytes == -1) {
        write(STDOUT_FILENO, "wcat: invalid read operation\n", 29);
      }
    }
  }

  return 0;
}
