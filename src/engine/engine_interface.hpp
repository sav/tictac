#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "engine/analysis_result.hpp"

namespace tictac {

/// Abstract interface for UCI chess engines (Phase 2).
class EngineInterface {
public:
    virtual ~EngineInterface() = default;

    virtual bool start(const std::filesystem::path& engine_path) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool is_running() const = 0;

    virtual void set_position(std::string_view fen) = 0;
    virtual void set_position(std::string_view fen,
                              const std::vector<std::string>& moves) = 0;

    virtual AnalysisResult analyze(std::chrono::milliseconds time_limit,
                                   unsigned depth = 0,
                                   unsigned multi_pv = 1) = 0;

    virtual void set_option(std::string_view name, std::string_view value) = 0;
};

} // namespace tictac
