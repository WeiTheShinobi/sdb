#include <iostream>
#include <memory>
#include<string_view>
#include <editline/readline.h>
#include <string>
#include <vector>
#include <sstream>

#include "libsdb/error.hpp"
#include "libsdb/process.hpp"

namespace sdb {
    class process;
}

namespace {
    std::vector<std::string> split(std::string_view str, char delimiter) {
        std::vector<std::string> result{};
        std::stringstream ss{std::string{str}};
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

    std::unique_ptr<sdb::process> attach(int argc, const char **argv) {
        if (argc == 3 && argv[1] == std::string_view("-p")) {
            pid_t pid = std::atoi(argv[2]);
            return sdb::process::attach(pid);
        }
        const char *program_path = argv[1];
        return sdb::process::launch(program_path);
    }

    void print_stop_reason(const sdb::process &process, sdb::stop_reason reason) {
        std::cout << "Process " << process.pid() << ' ';

        switch (reason.reason) {
            case sdb::process_state::exited:
                std::cout << "exited with status "
                        << static_cast<int>(reason.info);
                break;
            case sdb::process_state::terminated:
                std::cout << "terminated with signal "
                        << sigabbrev_np(reason.info);
                break;
            case sdb::process_state::stopped:
                std::cout << "stopped with signal " << sigabbrev_np(reason.info);
                break;
            case sdb::process_state::running:
                break;
        }

        std::cout << std::endl;
    }

    void handle_command(std::unique_ptr<sdb::process> &process, const std::string_view line) {
        const auto args = split(line, ' ');
        const auto &command = args[0];

        if (is_prefix(command, "continue")) {
            process->resume();
            auto reason = process->wait_on_signal();
            print_stop_reason(*process, reason);
        } else {
            std::cerr << "Unknown command: " << command << "\n";
        }
    }

    void main_loop(std::unique_ptr<sdb::process> &process) {
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
                try {
                    handle_command(process, line_str);
                } catch (const sdb::error &err) {
                    std::cout << err.what() << '\n';
                }
            }
        }
    }
}

int main(int argc, const char **argv) {
    if (argc == 1) {
        std::cerr << "No argument provided\n";
        return -1;
    }

    try {
        auto process = attach(argc, argv);
        main_loop(process);
    } catch (const sdb::error &err) {
        std::cerr << err.what() << '\n';
    }
}
