#include <iostream>
#include <string>

#include <fcntl.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

// Helper function to get length of string
int len(const char *str) {
  int str_len = 0;
  while (*str++)
    str_len++;
  return str_len;
}

// Buffers and searches for search string in file
int grep(int file_descriptor, char *search_str) {
  /**
   * Algorithm
   * Read char from buffer
   * 1. If char is newline, check that search_i == search_str_len, if so, print
   *    the line, reset line and search_i
   * 2. Else if char == search_str[search_i], increment search_i
   * 3. Else, reset search_i
   * 4. Add char to line for printing
   */
  int read_bytes;
  char r_buf[4096];
  std::string line;
  int search_i = 0;
  int search_str_len = len(search_str);

  while ((read_bytes = read(file_descriptor, r_buf, sizeof(r_buf))) > 0) {
    for (int i = 0; i < read_bytes; i++) {
      if (r_buf[i] == '\n') {
        if (search_i == search_str_len) {
          line += r_buf[i];
          int write_res = write(STDOUT_FILENO, line.c_str(), line.length());
          if (write_res == -1) {
            write(STDOUT_FILENO, "wgrep: invalid write operation\n", 31);
            return 1;
          }
        }
        search_i = 0;
        line.clear();
      } else {
        if (search_i != search_str_len) {
          if (r_buf[i] == search_str[search_i])
            search_i++;
          else
            search_i = 0;
        }
        line += r_buf[i];
      }
    }
  }

  if (file_descriptor != STDIN_FILENO)
    close(file_descriptor);

  if (read_bytes == -1) {
    write(STDOUT_FILENO, "wgrep: invalid read operation\n", 31);
    return 1;
  }

  return 0;
}

int main(int argc, char *argv[]) {
  if (argc == 1) {
    // No args
    write(STDOUT_FILENO, "wgrep: searchterm [file ...]\n", 29);
    return 1;
  } else if (argc == 2) {
    // No files
    int failed = grep(STDIN_FILENO, argv[1]);
    if (failed) {
      return failed;
    }
  } else {
    // Loop through each file name provided
    int file_descriptor;
    for (int i = 2; i < argc; i++) {
      file_descriptor = open(argv[i], O_RDONLY);
      if (file_descriptor == -1) {
        write(STDOUT_FILENO, "wgrep: cannot open file\n", 24);
        return 1;
      }

      int failed = grep(file_descriptor, argv[1]);
      if (failed) {
        return failed;
      }
    }
  }

  return 0;
}
