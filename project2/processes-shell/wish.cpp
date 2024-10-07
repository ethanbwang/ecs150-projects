#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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
  std::string error_message = "An error has occurred\n";
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
      std::cerr << error_message;
      // std::cout << "wish: cd: error: invalid number of arguments\n";
      return 1;
    }

    if (chdir(command[1])) {
      std::cerr << error_message;
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
    // std::set<char> delims = {'&', '<', '>', '|', ' '};
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
          std::cerr << error_message;
          return 1;
        }
        ampersand = true;
        // Run the previous command in a child process
        // No problem if nothing comes after an ampersand
        break;
      case '<':
        // Expect one file. Anything after the filename is an error.
        if (ampersand || redirect) {
          std::cerr << error_message;
          return 1;
        }
        redirect = 2;
        break;
      case '>':
        // Expect one file. Anything after the filename is an error.
        if (ampersand || redirect) {
          std::cerr << error_message;
          return 1;
        }
        redirect = 1;
        break;
      default:
        cur_str += c;
      }
    }
    // If redirect, should add cur string to redir_file and not add anything to
    // commands
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
          std::cerr << error_message;
          return 1;
        }
        return 0;
      }
    }
    std::cerr << error_message;
    return 1;
  }

  int run() {
    // Parses a command and allocates the processes to run them
    commands.clear();
    redir_file.clear();
    pid_list.clear();

    if (input.length() == 0) {
      return 0;
    }

    if (parse_command() == 0) {
      for (size_t i = 0; i < commands.size() - 1; i++) {
        // Need to fork
        // When to fork?
        // - When there are multiple commands (meaning parallel commands)
        // - If the command is not a built-in shell command
        pid_t pid = fork();
        if (pid == -1) {
          std::cerr << error_message;
          continue;
        }

        if (pid == 0) {
          // Execute command in child process
          if (strcmp(commands[i][0], "exit") == 0) {
            if (commands[i].size() != 2) {
              std::cerr << error_message;
            } else {
              exit(0);
            }
          } else if (strcmp(commands[i][0], "cd") == 0) {
            exit(cd(commands[i]));
          } else if (strcmp(commands[i][0], "path") == 0) {
            exit(path(commands[i]));
          } else {
            // std::cout << "wish: invalid command: " << pair.first << '\n';
            exit(exec_command(commands[i]));
          }
        } else {
          pid_list.push_back(pid);
        }
      }
      // Last command is treated differently, should not be a parallel process
      size_t last_idx = commands.size() - 1;
      if (last_idx >= 0 && commands[last_idx][0] != nullptr) {
        if (strcmp(commands[last_idx][0], "exit") == 0) {
          if (commands[last_idx].size() != 2) {
            std::cerr << error_message;
          } else {
            exit(0);
          }
          exit(0);
        } else if (strcmp(commands[last_idx][0], "cd") == 0) {
          cd(commands[last_idx]);
        } else if (strcmp(commands[last_idx][0], "path") == 0) {
          path(commands[last_idx]);
        } else {
          // Fork since it is not a built-in command
          pid_t pid = fork();
          if (pid == -1) {
            std::cerr << error_message;
          } else if (pid == 0) {
            exit(exec_command(commands[last_idx]));
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

    return 0;
  }

  int run_stdin() {
    // Runs wish from stdin
    while (true) {
      input.clear();
      std::cout << "wish> ";
      std::getline(std::cin, input);
      run();
    }
    return 0;
  }

  int run_batch(char *file) {
    // Runs the wish shell with batch scripts
    std::ifstream ifs(file, std::ios::in);
    if (!ifs) {
      std::cerr << error_message;
    }
    while (std::getline(ifs, input)) {
      // if (!run()) {
      //   // std::cerr << error_message;
      //   // return 1;
      //   continue;
      // }
      run();
    }
    ifs.close();
    return 0;
  }
};

int main(int argc, char *argv[]) {
  Wish wish;
  switch (argc) {
  case 2:
    wish = Wish(argc, argv);
    return wish.run_batch(argv[1]);
  case 1:
    wish = Wish();
    return wish.run_stdin();
  default:
    std::cerr << "An error has occurred\n";
    exit(1);
  }
}
