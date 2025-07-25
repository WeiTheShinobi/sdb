#include "libsdb/process.hpp"

#include <memory>
#include <csignal>
#include <iostream>
#include <unistd.h>
#include <bits/signum-arch.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include "libsdb/error.hpp"
#include "libsdb/pipe.hpp"

namespace {
    void exit_with_perror(
        sdb::pipe &channel, std::string const &prefix) {
        auto msg = prefix + ": " + std::strerror(errno);
        channel.write(reinterpret_cast<std::byte *>(msg.data()), msg.size());
        exit(-1);
    }
}

std::unique_ptr<sdb::process>
sdb::process::launch(std::filesystem::path path, bool debug) {
    pipe channel(/*close_on_exec=*/true);
    pid_t pid;
    if ((pid = fork()) < 0) {
        error::send_errno("fork failed");
    }

    if (pid == 0) {
        channel.close_read();
        if (debug and ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
            exit_with_perror(channel, "Tracing failed");
        }
        if (execlp(path.c_str(), path.c_str(), nullptr) < 0) {
            exit_with_perror(channel, "exec failed");
        }
    }

    channel.close_write();
    auto data = channel.read();
    channel.close_read();

    if (data.size() > 0) {
        waitpid(pid, nullptr, 0);
        auto chars = reinterpret_cast<char*>(data.data());
        error::send(std::string(chars, chars + data.size()));
    }

    std::unique_ptr<process> proc(
        new process(pid, /*terminate_on_end=*/true, debug));
    if (debug) {
        proc->wait_on_signal();
    }

    return proc;
}

std::unique_ptr<sdb::process> sdb::process::attach(pid_t pid) {
    if (pid == 0) {
        error::send("Invalid pid");
    }

    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) {
        error::send_errno("Could not attach process");
    }

    std::unique_ptr<process> proc(new process(pid, false, true));
    proc->wait_on_signal();

    return proc;
}

sdb::process::~process() {
    if (pid_ != 0) {
        int status;
        if (is_attached_) {
            if (state_ == process_state::running) {
                kill(pid_, SIGSTOP);
                waitpid(pid_, &status, 0);
            }

            ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
            kill(pid_, SIGCONT);
        }

        if (terminate_on_end_) {
            kill(pid_, SIGKILL);
            waitpid(pid_, &status, 0);
        }
    }
}

void sdb::process::resume() {
    if (ptrace(PTRACE_CONT, pid_, nullptr, nullptr) < 0) {
        error::send_errno("resume failed");
    }
    state_ = process_state::running;
}

sdb::stop_reason::stop_reason(int wait_status) {
    if (WIFEXITED(wait_status)) {
        reason = process_state::exited;
        info = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        reason = process_state::terminated;
        info = WTERMSIG(wait_status);
    } else if (WIFSTOPPED(wait_status)) {
        reason = process_state::stopped;
        info = WSTOPSIG(wait_status);
    }
}

sdb::stop_reason sdb::process::wait_on_signal() {
    int wait_status;
    int options = 0;
    if (waitpid(pid_, &wait_status, options) < 0) {
        error::send_errno("wait_on_signal failed");
    }
    stop_reason reason(wait_status);
    state_ = reason.reason;
    return reason;
}
