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

      int ret;
      char buffer[4096];

      while ((ret = read(file_descriptor, buffer, sizeof(buffer))) > 0)
        write(STDOUT_FILENO, buffer, ret);

      if (file_descriptor != STDIN_FILENO)
        close(file_descriptor);

      if (ret == -1) {
        write(STDOUT_FILENO, "wcat: invalid read operation\n", 29);
      }
    }
  }

  return 0;
}
