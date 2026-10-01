#pragma once

#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gmdr {

struct ProcessOptions {
    std::filesystem::path executable;
    std::vector<std::string> args;             // UTF-8, without argv[0]
    std::vector<std::intptr_t> inheritHandles; // the only handles the child may inherit (besides its stdout)
    std::uint64_t memoryLimitBytes = 0;        // 0 = no limit
    // Windows: run inside this AppContainer (profile created on first use, no capabilities): the child can
    // use the handles it inherits but cannot open user files by path. Empty = no AppContainer.
    std::string appContainer;
    // If the AppContainer cannot be used (no profile API, executable not readable by AppContainer apps),
    // start without it instead of failing; sandboxNote() then says why.
    bool appContainerFallback = true;
};

// A restricted child process whose stdout is a pipe read line by line.
//
// Windows: created suspended inside a Job Object (kill-on-close, one active process, optional memory cap),
// with PROC_THREAD_ATTRIBUTE_HANDLE_LIST so it inherits nothing but the listed handles.
// POSIX: fork + exec with RLIMIT_AS; the listed fds keep their numbers, everything else is close-on-exec.
class ChildProcess {
public:
    ChildProcess();
    ~ChildProcess();
    ChildProcess(ChildProcess&&) noexcept;
    ChildProcess& operator=(ChildProcess&&) noexcept;

    static Result<ChildProcess> spawn(const ProcessOptions& options);

    // Blocks until a full line (without the newline) is read. Returns false at end of stream.
    bool readLine(std::string& line);
    void kill();
    // Waits for exit and returns the exit code.
    int wait();
    // True when the child runs inside the requested AppContainer.
    bool inAppContainer() const;
    // Why the AppContainer was not used (empty when it was, or when none was requested).
    const std::string& sandboxNote() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Path of the running executable (used to find gmdr-import next to the app).
std::filesystem::path currentExecutablePath();

} // namespace gmdr
