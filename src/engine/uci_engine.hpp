#pragma once

#include "engine/engine_interface.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <sys/types.h>

namespace tictac {

/// UCI subprocess wrapper. Synchronous, single-threaded use only:
/// `set_position` followed by `analyze` blocks until `bestmove` arrives.
/// Compatible with any UCI engine; defaults assume Stockfish-like behavior.
class UciEngine : public EngineInterface {
public:
    UciEngine() = default;
    ~UciEngine() override;

    UciEngine(const UciEngine&) = delete;
    UciEngine& operator=(const UciEngine&) = delete;

    bool start(const std::filesystem::path& engine_path) override;
    void stop() override;
    [[nodiscard]] bool is_running() const override;

    void set_position(std::string_view fen) override;
    void set_position(std::string_view fen,
                      const std::vector<std::string>& moves) override;

    AnalysisResult analyze(std::chrono::milliseconds time_limit,
                           unsigned depth = 0,
                           unsigned multi_pv = 1) override;

    void set_option(std::string_view name, std::string_view value) override;

    [[nodiscard]] const std::string& engine_name() const noexcept { return name_; }

private:
    void send_line(std::string_view line);
    /// Returns the next line (without trailing newline), or std::nullopt on EOF.
    /// An empty string is a legitimate blank line, distinct from EOF.
    [[nodiscard]] std::optional<std::string> read_line();
    void wait_for_token(std::string_view token);

    pid_t       pid_             = -1;
    int         stdin_fd_        = -1;
    std::FILE*  stdout_file_     = nullptr;
    unsigned    current_multipv_ = 1;
    std::string name_;
};

} // namespace tictac
