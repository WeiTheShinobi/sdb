#include <iostream>
#include <unistd.h>
#include<string_view>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <editline/readline.h>
#include <string>
#include <vector>
#include <sstream>

namespace {
    std::vector<std::string> split(std::string_view str, char delimiter) {
        std::vector<std::string> result{};
        std::stringstream ss {std::string{str}};
        std::string item;

        while (std::getline(ss, item, delimiter)) {
            result.push_back(item);
        }

        return result;
    }

    bool is_prefix(std::string_view str, std::string_view prefix) {
        if (str.length() > prefix.length()) return false;
        return std::equal(str.begin(), str.end(), prefix.begin());
    }

    void resume(pid_t pid) {
        if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0) {
            std::cerr << "Couldn't continue\n";
            std::exit(-1);
        }
    }

    void wait_on_signal(pid_t pid) {
        int wait_signal;
        int options = 0;
        if (waitpid(pid, &wait_signal, options) < 0) {
            std::perror("waitpid failed");
            std::exit(-1);
        }
    }

    pid_t attach(int argc, const char **argv) {
        pid_t pid = 0;

        if (argc == 3 && argv[1] == std::string_view("-p")) {
            pid = std::atoi(argv[2]);
            if (pid <= 0) {
                std::cerr << "Invalid pid\n";
                return -1;
            }
            if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) {
                std::perror("Could not attach");
                return -1;
            }
        } else {
            const char *program_path = argv[1];
            if ((pid = fork()) < 0) {
                std::perror("fork failed");
                return -1;
            }

            if (pid == 0) {
                if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
                    std::perror("Trace failed");
                    return -1;
                }
                if (execlp(program_path, program_path, nullptr) < 0) {
                    std::perror("Trace failed");
                    return -1;
                }
            }
        }

        return pid;
    }

    void handle_command(const pid_t pid, const std::string_view line) {
        const auto args = split(line, ' ');
        for (auto a: args) std::cout << a << " "   ;
        const auto& command = args[0];

        if (is_prefix(command, "continue")) {
            resume(pid);
            wait_on_signal(pid);
        } else {
            std::cerr << "Unknown command: " << command << "\n";
        }
    }
}

int main(int argc, const char **argv) {
    if (argc == 1) {
        std::cerr << "No argument provided\n";
        return -1;
    }

    pid_t pid = attach(argc, argv);

    int wait_status;
    int options = 0;
    if (waitpid(pid, &wait_status, options) < 0) {
        std::perror("waitpid failed");
    }

    char *line = nullptr;
    while ((line = readline("sdb> ")) != nullptr) {
        std::string line_str;

        if (line == std::string_view("")) {
            free(line);
            if (history_length > 0) {
                line_str = history_list()[history_length - 1]->line;
            }
        } else {
            line_str = line;
            add_history(line);
            free(line);
        }

        if (!line_str.empty()) {
            handle_command(pid, line_str);
        }
    }
}
