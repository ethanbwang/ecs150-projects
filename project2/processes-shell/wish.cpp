#include <cstring>
#include <fstream>
#include <iostream>
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

class Command {
private:
  std::vector<char *> args = {};
  std::vector<std::unique_ptr<char[]>> arg_mem = {};
  bool parallel = false;
  std::string redir_in_file = "";
  std::string redir_out_file = "";

public:
  Command() {}

  const std::vector<char *> &get_args() { return args; }

  void add_arg(char *p) { args.push_back(p); }

  void add_arg(const std::string &arg) {
    auto arg_p = std::make_unique<char[]>(arg.length() + 1);
    std::strcpy(arg_p.get(), arg.c_str());
    args.push_back(arg_p.get());
    arg_mem.push_back(std::move(arg_p));
  }

  const bool &get_parallel() { return parallel; }

  void set_parallel() { parallel = true; }

  const std::string &get_in_file() { return redir_in_file; }

  void set_in_file(const std::string &in_f) { redir_in_file = in_f; }

  const std::string &get_out_file() { return redir_out_file; }

  void set_out_file(const std::string &out_f) { redir_out_file = out_f; }
};

class Tokenizer {
private:
  std::set<std::string> delimeters = {"&", "|", "<", ">"};

public:
  Tokenizer() {}

  std::vector<std::string> tokenize(const std::string &line) {
    /*
     * Tokenizes a line of input. Will check for incorrect sequences of tokens.
     *
     * Args:
     *   line: line to tokenize
     *
     * Returns:
     *   tokens: vector of tokens or vector of empty string if error
     */
    if (line.empty()) {
      // Empty lines are allowed
      return {""};
    }

    std::vector<std::string> tokens = {};
    bool one_file = false;
    bool redir_out = false;
    bool redir_in = false;

    std::string cur_str = "";
    for (size_t idx = 0; idx < line.length(); idx++) {
      switch (line[idx]) {
      case EOF:
        if (!cur_str.empty()) {
          if (one_file && tokens.size() > 0 &&
              delimeters.count(tokens[tokens.size() - 1]) == 0) {
            // Multiple files after redirection
            return {};
          }
          // One more token to add
          tokens.push_back(cur_str);
        }

        if (tokens.size() > 0 && delimeters.count(tokens[tokens.size() - 1]) &&
            tokens[tokens.size() - 1] != "&") {
          // Line ended with an illegal special delimeter
          return {};
        }

        // Exit gracefully at EOF
        tokens.push_back("eof_exit");
        return tokens;
      case ' ':
      case '\t':
        if (!cur_str.empty()) {
          if (one_file && tokens.size() > 0 &&
              delimeters.count(tokens[tokens.size() - 1]) == 0) {
            // Multiple files after redirection
            return {};
          }
          tokens.push_back(cur_str);
          cur_str.clear();
        }
        break;
      case '|':
      case '&':
      case '<':
      case '>':
        if (cur_str.empty() && tokens.empty() && line[idx] == '&') {
          // Allow ampersand by itself for some reason
          tokens.push_back(std::string(1, line[idx]));
          break;
        } else if (cur_str.empty() &&
                   (tokens.empty() ||
                    delimeters.count(tokens[tokens.size() - 1]))) {
          // Makes sure that command doesn't begin with a special delimeter
          // and that there aren't two special delimeters in a row
          return {};
        }

        if (!cur_str.empty()) {
          if (one_file && tokens.size() > 0 &&
              delimeters.count(tokens[tokens.size() - 1]) == 0) {
            // Multiple files after redirection
            return {};
          }
          // Just finished parsing cur_str as a token
          tokens.push_back(cur_str);
          cur_str.clear();
        }

        if (line[idx] == '<') {
          if (redir_in) {
            // Multiple redirects in one command
            return {};
          }
          one_file = true;
          redir_in = true;
        } else if (line[idx] == '>') {
          if (redir_out) {
            // Multiple redirects in one command
            return {};
          }
          one_file = true;
          redir_out = true;
        } else {
          one_file = false;
          redir_out = false;
          redir_in = false;
        }

        tokens.push_back(std::string(1, line[idx]));
        break;
      default:
        cur_str += line[idx];
      }
    }

    if (!cur_str.empty()) {
      if (one_file && tokens.size() > 0 &&
          delimeters.count(tokens[tokens.size() - 1]) == 0) {
        // Multiple files after redirection
        return {};
      }
      // One more token to add
      tokens.push_back(cur_str);
    }

    if (tokens.size() > 0 && delimeters.count(tokens[tokens.size() - 1]) &&
        tokens[tokens.size() - 1] != "&") {
      // Line ended with an illegal special delimeter
      return {};
    } else if (tokens.empty() && cur_str.empty()) {
      // Empty line
      return {""};
    }
    return tokens;
  }
};

class Wish {
private:
  Tokenizer tokenizer = Tokenizer();
  std::vector<std::string> paths = {"/bin"};
  std::string input = "";
  // Each command is a vector of args
  // Multiple commands will be due to an ampersand
  std::vector<Command> commands = {};
  std::string error_message = "An error has occurred\n";
  std::vector<pid_t> pid_list = {};

public:
  Wish() {}

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
    std::vector<std::string> tokens = tokenizer.tokenize(input);
    if (tokens.empty()) {
      std::cerr << error_message;
      return 1;
    } else if (tokens.size() == 1 && tokens[0] == "") {
      return 0;
    }
    Command cmd = Command();
    bool redir_out = false;
    bool redir_in = false;

    for (auto token : tokens) {
      if (token == "eof_exit") {
        if (!cmd.get_args().empty()) {
          cmd.add_arg(nullptr);
          commands.push_back(std::move(cmd));
          cmd = Command();
        }
        cmd.add_arg(std::string("exit"));
        commands.push_back(std::move(cmd));
        return 0;
      } else if (token == "&") {
        cmd.set_parallel();
        cmd.add_arg(nullptr);
        commands.push_back(std::move(cmd));
        cmd = Command();
        redir_out = false;
        redir_in = false;
      } else if (token == ">") {
        redir_out = true;
      } else if (token == "<") {
        // TODO: Implement redirect in
        redir_in = true;
        std::cerr << error_message;
        return 1;
      } else if (token == "|") {
        // TODO: Implement piping
        std::cerr << error_message;
        return 1;
      } else {
        if (redir_out) {
          cmd.set_out_file(token);
        } else if (redir_in) {
          cmd.set_in_file(token);
        } else {
          cmd.add_arg(token);
        }
      }
    }

    if (!cmd.get_args().empty()) {
      // Add last command
      cmd.add_arg(nullptr);
      commands.push_back(std::move(cmd));
    }

    return 0;
  }

  int exec_command(Command &command) {
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
      exec_path = path + '/' + command.get_args()[0];
      if (access(exec_path.c_str(), X_OK) == 0) {
        if (!command.get_out_file().empty()) {
          if (!std::freopen(command.get_out_file().c_str(), "w", stdout)) {
            std::cerr << error_message;
            return 1;
          }
        }
        if (execv(exec_path.c_str(), command.get_args().data()) == -1) {
          std::cerr << error_message;
          if (!command.get_out_file().empty()) {
            std::fclose(stdout);
          }
          return 1;
        }
        if (!command.get_out_file().empty()) {
          std::fclose(stdout);
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
    pid_list.clear();

    if (input.length() == 0) {
      return 0;
    }

    if (parse_command() == 0) {
      for (auto &cmd : commands) {
        // Need to fork
        // When to fork?
        // - When there are multiple commands (meaning parallel commands)
        // - If the command is not a built-in shell command
        if (cmd.get_parallel()) {
          pid_t pid = fork();
          if (pid == -1) {
            std::cerr << error_message;
            continue;
          }

          if (pid == 0) {
            // Execute command in child process
            if (strcmp(cmd.get_args()[0], "exit") == 0) {
              if (cmd.get_args().size() != 2) {
                std::cerr << error_message;
              } else {
                exit(0);
              }
            } else if (strcmp(cmd.get_args()[0], "cd") == 0) {
              exit(cd(cmd.get_args()));
            } else if (strcmp(cmd.get_args()[0], "path") == 0) {
              exit(path(cmd.get_args()));
            } else {
              exit(exec_command(cmd));
            }
          } else {
            pid_list.push_back(pid);
          }
        } else {
          // Command is not supposed to be run in parallel
          if (strcmp(cmd.get_args()[0], "exit") == 0) {
            if (cmd.get_args().size() != 2) {
              std::cerr << error_message;
            } else {
              exit(0);
            }
          } else if (strcmp(cmd.get_args()[0], "cd") == 0) {
            cd(cmd.get_args());
          } else if (strcmp(cmd.get_args()[0], "path") == 0) {
            path(cmd.get_args());
          } else {
            // Fork since it is not a built-in command
            pid_t pid = fork();
            if (pid == -1) {
              std::cerr << error_message;
            } else if (pid == 0) {
              exit(exec_command(cmd));
            } else {
              pid_list.push_back(pid);
            }
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
    // Runs wish taking input from stdin
    while (true) {
      input.clear();
      std::cout << "wish> ";
      std::getline(std::cin, input);
      run();
    }
    return 0;
  }

  int run_batch(char *file) {
    // Runs the wish shell with specified batch script
    std::ifstream ifs(file, std::ios::in);
    if (!ifs) {
      std::cerr << error_message;
      return 1;
    }
    bool valid_batch = false;
    while (std::getline(ifs, input)) {
      valid_batch = true;
      run();
    }
    ifs.close();
    if (!valid_batch) {
      std::cerr << error_message;
      return 1;
    }
    return 0;
  }
};

int main(int argc, char *argv[]) {
  Wish wish = Wish();
  switch (argc) {
  case 2:
    return wish.run_batch(argv[1]);
  case 1:
    return wish.run_stdin();
  default:
    std::cerr << "An error has occurred\n";
    exit(1);
  }
}
