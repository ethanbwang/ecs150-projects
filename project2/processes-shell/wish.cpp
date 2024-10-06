#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <fcntl.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

class Wish {
private:
  std::vector<std::string> paths = {"/bin"};
  std::vector<std::string> batch_scripts = {};
  std::string input = "";
  // Each command is a vector of args
  // Multiple commands will be due to an ampersand
  std::vector<std::vector<std::unique_ptr<char[]>>> command_mem = {};
  std::vector<std::vector<char *>> commands = {};
  std::string redir_file = "";
  char error_message[23] = "An error has occurred\n";
  int err_msg_len = strlen(error_message);
  std::vector<pid_t> pid_list = {};

public:
  Wish() {}

  Wish(const int &argc, char **argv) {
    for (int i = 0; i < argc; i++) {
      batch_scripts.push_back(argv[i]);
    }
  }

  int path(const std::vector<char *> &command) {
    /*
     * Updates path vector to contain the paths specified in args
     *
     * Args:
     *   command: array of arguments with command[0] being the command name
     *
     * Returns:
     *   exit code: 0 on success, 1 on error
     */
    paths.clear();
    for (size_t i = 1; i < command.size() - 1; i++) {
      paths.push_back(std::string(command[i]));
    }
    return 0;
  }

  int cd(const std::vector<char *> &command) {
    /*
     * Changes working directory to the one specified in args
     *
     * Args:
     *   command: array of arguments with command[0] being the command name
     *
     * Returns:
     *   exit code: 0 on success, 1 on error
     */
    if (command.size() != 3 || strcmp(command[0], "cd") ||
        command[2] != nullptr) {
      write(STDERR_FILENO, error_message, err_msg_len);
      // std::cout << "wish: cd: error: invalid number of arguments\n";
      return 1;
    }

    if (chdir(command[1])) {
      write(STDERR_FILENO, error_message, err_msg_len);
      // std::cout << "wish: cd: error: invalid directory\n";
      return 1;
    }
    return 0;
  }

  void add_arg(const std::string &arg, std::vector<char *> &args,
               std::vector<std::unique_ptr<char[]>> &arg_mem) {
    auto arg_p = std::make_unique<char[]>(arg.length() + 1);
    std::strcpy(arg_p.get(), arg.c_str());
    args.push_back(arg_p.get());
    arg_mem.push_back(std::move(arg_p));
  }

  int parse_command() {
    /*
     * Parses command line input
     *
     * Args:
     *   args: arguments passed along with cd invocation
     *
     * Returns:
     *   exit code: 0 on success, 1 on error
     */
    std::string cur_str = "";
    std::vector<std::unique_ptr<char[]>> arg_mem = {};
    std::vector<char *> args = {};
    std::set<char> delims = {'&', '<', '>', '|', ' '};
    int redirect = 0;
    bool ampersand = false;
    for (auto c : input) {
      switch (c) {
      case EOF:
        if (!cur_str.empty()) {
          // One more argument to add
          add_arg(cur_str, args, arg_mem);
        }
        args.push_back(nullptr);
        commands.push_back(args);
        command_mem.push_back(std::move(arg_mem));

        // Add exit to exit gracefully
        cur_str = "exit";
        args.clear();
        arg_mem.clear();
        add_arg(cur_str, args, arg_mem);
        args.push_back(nullptr);
        commands.push_back(args);
        command_mem.push_back(std::move(arg_mem));
        cur_str.clear();
        return 0;
      case ' ':
      case '\t':
        if (!cur_str.empty()) {
          // Just finished parsing an argument
          add_arg(cur_str, args, arg_mem);
          cur_str.clear();
        }
        break;
      case '&':
        if (ampersand) {
          write(STDOUT_FILENO, error_message, err_msg_len);
          return 1;
        }
        ampersand = true;
        // Run the previous command in a child process
        // No problem if nothing comes after an ampersand
        break;
      case '<':
        // Expect one file. Anything after the filename is an error.
        if (ampersand || redirect) {
          write(STDOUT_FILENO, error_message, err_msg_len);
          return 1;
        }
        redirect = 2;
        break;
      case '>':
        // Expect one file. Anything after the filename is an error.
        if (ampersand || redirect) {
          write(STDOUT_FILENO, error_message, err_msg_len);
          return 1;
        }
        redirect = 1;
        break;
      default:
        cur_str += c;
      }
    }
    if (!cur_str.empty()) {
      // Last argument hasn't been added yet
      add_arg(cur_str, args, arg_mem);
      cur_str.clear();
    }
    args.push_back(nullptr);
    commands.push_back(args);
    command_mem.push_back(std::move(arg_mem));

    return 0;
  }

  int exec_command(const std::vector<char *> &command) {
    /*
     * Searches paths for command and attempts to execute via execv
     *
     * Args:
     *   command: array of arguments with command[0] being the command name
     *
     * Returns:
     *   return code: 0 on success, 1 on failure
     */
    std::string exec_path = "";
    for (auto path : paths) {
      exec_path = path + '/' + command[0];
      if (access(exec_path.c_str(), X_OK) == 0) {
        if (execv(exec_path.c_str(), command.data()) == -1) {
          write(STDOUT_FILENO, error_message, err_msg_len);
          return 1;
        }
        return 0;
      }
    }
    write(STDOUT_FILENO, error_message, err_msg_len);
    return 1;
  }

  int run() {
    // Runs the wish shell
    int good_command = 1;
    while (true) {
      input.clear();
      commands.clear();
      redir_file.clear();
      pid_list.clear();

      std::cout << "wish> ";
      std::getline(std::cin, input);
      if (input.length() == 0) {
        continue;
      }

      good_command = parse_command();
      if (good_command == 0) {
        for (size_t i = 0; i < commands.size() - 1; i++) {
          // Need to fork
          // When to fork?
          // - When there are multiple commands (meaning parallel commands)
          // - If the command is not a built-in shell command
          pid_t pid = fork();
          if (pid == -1) {
            write(STDERR_FILENO, error_message, err_msg_len);
            continue;
          }

          if (pid == 0) {
            // Execute command in child process
            if (strcmp(commands[i][0], "exit") == 0) {
              assert(commands[i].size() == 2);
              exit(5);
            } else if (strcmp(commands[i][0], "cd") == 0) {
              cd(commands[i]);
            } else if (strcmp(commands[i][0], "path") == 0) {
              path(commands[i]);
            } else {
              // std::cout << "wish: invalid command: " << pair.first << '\n';
              exec_command(commands[i]);
            }
          } else {
            pid_list.push_back(pid);
          }
        }
        // Last command is treated differently, should not be a parallel process
        size_t last_idx = commands.size() - 1;
        if (last_idx >= 0) {
          if (strcmp(commands[last_idx][0], "exit") == 0) {
            assert(commands[last_idx].size() == 2);
            exit(5);
          } else if (strcmp(commands[last_idx][0], "cd") == 0) {
            cd(commands[last_idx]);
          } else if (strcmp(commands[last_idx][0], "path") == 0) {
            path(commands[last_idx]);
          } else {
            // Fork since it is not a built-in command
            pid_t pid = fork();
            if (pid == -1) {
              write(STDERR_FILENO, error_message, err_msg_len);
            } else if (pid == 0) {
              exec_command(commands[last_idx]);
            } else {
              pid_list.push_back(pid);
            }
          }
        }

        for (auto pid : pid_list) {
          int child_stat;
          waitpid(pid, &child_stat, 0);
        }
      }
    }
    return 0;
  }

  int run_batch() {
    // Runs the wish shell with batch scripts
    return 0;
  }
};

int main(int argc, char *argv[]) {
  Wish wish;
  if (argc > 1) {
    wish = Wish(argc, argv);
    if (wish.run_batch()) {
      return 1;
    }
    return wish.run();
  } else {
    wish = Wish();
    return wish.run();
  }
}
