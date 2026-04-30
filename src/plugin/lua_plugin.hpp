#pragma once

#include <filesystem>
#include <optional>

#include "core/types.hpp"

struct lua_State;

namespace tictac {

class EngineInterface;

/// Loads a Lua plugin script and dispatches game records to its `on_match`.
///
/// The script must define a global function `on_match(game)`. The function
/// is called once per candidate match. A truthy return value causes the
/// candidate to be retained in the result set; falsy values discard it.
///
/// When constructed with a non-null `engine`, the script also gets a global
/// `tictac.engine` table with `analyze(opts)` and `set_option(name, value)`.
class LuaPlugin {
public:
    explicit LuaPlugin(const std::filesystem::path& script,
                       EngineInterface* engine = nullptr);
    ~LuaPlugin();

    LuaPlugin(const LuaPlugin&) = delete;
    LuaPlugin& operator=(const LuaPlugin&) = delete;
    LuaPlugin(LuaPlugin&&) = delete;
    LuaPlugin& operator=(LuaPlugin&&) = delete;

    /// Invoke `on_match(game)`. `ply` is exposed as `game.ply` when set.
    /// Returns whatever truthiness the plugin returned.
    bool on_match(const GameRecord& game, std::optional<HalfMoveIdx> ply = std::nullopt);

private:
    lua_State* L_ = nullptr;
};

} // namespace tictac
