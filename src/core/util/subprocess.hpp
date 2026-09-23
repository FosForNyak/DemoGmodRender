// =============================================================================
//  subprocess.hpp — запустити консольну програму і читати її вивід по рядках.
//
//  Для зовнішніх інструментів (whisper-cli): без вікна консолі, stdout і stderr
//  разом, скасування — завершенням процесу.
// =============================================================================
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace gmdr {

struct ProcessResult {
    bool started = false;
    int  exit_code = -1;
    bool cancelled = false;
};

// on_line отримує кожен рядок виводу (без \r\n). cancel — завершити процес.
ProcessResult run_process(const std::filesystem::path& exe, const std::vector<std::string>& args,
                          const std::function<void(const std::string&)>& on_line,
                          const std::atomic<bool>* cancel = nullptr, std::string* error = nullptr);

} // namespace gmdr
