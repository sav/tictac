#include "runtime.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace tictac {

PluginSpec parsePluginSpec(const std::string &spec) {
    PluginSpec out;
    std::istringstream iss(spec);
    std::string token;
    bool first = true;
    while (iss >> token) {
        if (first) {
            out.path = token;
            first = false;
            continue;
        }
        auto eq = token.find('=');
        if (eq == std::string::npos) {
            out.args.emplace_back(token, "true");
        } else {
            out.args.emplace_back(token.substr(0, eq), token.substr(eq + 1));
        }
    }
    if (out.path.empty()) throw std::runtime_error("empty plugin spec");
    return out;
}

Runtime::Runtime(RunOptions opts) : opts_(std::move(opts)) {
    lua_.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::os,
                        sol::lib::io, sol::lib::package);
    shared_ = lua_.create_table();

    if (!opts_.noOutput) {
        if (opts_.output == "-" || opts_.output.empty()) {
            out_ = std::make_shared<Writer>();
        } else {
            out_ = std::make_shared<Writer>(opts_.output, "w");
        }
    }

    registerTypes();
    loadPlugins();
}

void Runtime::loadPlugins() {
    for (const auto &spec : opts_.plugins) {
        auto inst = std::make_unique<PluginInstance>();

        sol::protected_function_result loaded = lua_.safe_script_file(spec.path, sol::script_pass_on_error);
        if (!loaded.valid()) {
            sol::error err = loaded;
            throw std::runtime_error("failed to load plugin '" + spec.path + "': " + err.what());
        }
        sol::object obj = loaded;
        if (!obj.is<sol::table>()) {
            throw std::runtime_error("plugin '" + spec.path + "' did not return a table");
        }
        inst->plugin = obj.as<sol::table>();

        inst->args = std::make_shared<Args>();
        for (const auto &[k, v] : spec.args) inst->args->values[k] = v;

        inst->name = spec.path;
        if (inst->plugin["meta"].valid()) {
            sol::table meta = inst->plugin["meta"];
            if (meta["name"].valid()) inst->name = meta.get<std::string>("name");
        }

        inst->ctx = buildCtx(*inst);
        plugins_.push_back(std::move(inst));
    }
}

bool Runtime::processGame(const std::shared_ptr<Game> &game, std::size_t index) {
    sol::state_view lua = lua_;
    bool halt = false;

    std::vector<PValue> frontier;
    frontier.push_back(PValue{game, game->boardAt(-1), sol::object{}});

    for (auto &inst : plugins_) {
        if (frontier.empty()) break;
        inst->ctx["index"] = index;

        sol::optional<sol::protected_function> process = inst->plugin["process"];
        std::vector<PValue> next;

        for (auto &val : frontier) {
            if (!process) {
                next.push_back(val);
                continue;
            }

            sol::table input = lua.create_table();
            input["game"] = LuaGame{val.game};
            input["board"] = LuaBoard{val.board};
            input["data"] = val.data;

            sol::protected_function_result res = (*process)(input, inst->ctx);
            if (!res.valid()) {
                sol::error err = res;
                if (opts_.onError == OnError::Abort) {
                    throw std::runtime_error("plugin '" + inst->name + "' error: " + err.what());
                }
                std::cerr << "[" << inst->name << ":" << index << "] error: " << err.what() << "\n";
                if (opts_.onError == OnError::Skip) continue; // drop
                next.push_back(val);                          // warn -> pass
                continue;
            }

            // Interpret the return value (see §4 shorthands).
            sol::object ret = res;
            auto applyResult = [&](sol::table t) {
                std::string action = t["action"].valid() ? t.get<std::string>("action") : "pass";
                if (action == "drop") return;
                PValue out = val;
                if (t["game"].valid()) out.game = t.get<LuaGame>("game").g;
                if (t["board"].valid()) out.board = t.get<LuaBoard>("board").board;
                if (t["data"].valid()) out.data = t.get<sol::object>("data");
                if (action == "halt") halt = true;
                next.push_back(std::move(out));
            };

            if (ret == sol::lua_nil) {
                next.push_back(val);
            } else if (ret.is<bool>()) {
                if (ret.as<bool>()) next.push_back(val); // true -> pass; false -> drop
            } else if (ret.is<LuaBoard>()) {
                PValue out = val;
                out.board = ret.as<LuaBoard>().board;
                next.push_back(std::move(out));
            } else if (ret.is<sol::table>()) {
                sol::table t = ret.as<sol::table>();
                sol::object first = t[1];
                if (first.is<sol::table>()) {
                    for (std::size_t i = 1; i <= t.size(); ++i) applyResult(t.get<sol::table>(i)); // fan-out
                } else {
                    applyResult(t);
                }
            } else {
                next.push_back(val); // unknown -> pass
            }
        }

        frontier = std::move(next);
    }

    if (out_) {
        for (auto &val : frontier) out_->writeGame(val.game);
    }
    return halt;
}

int Runtime::run() {
    // Call init() for every plugin.
    for (auto &inst : plugins_) {
        sol::optional<sol::protected_function> init = inst->plugin["init"];
        if (init) {
            sol::protected_function_result r = (*init)(inst->ctx);
            if (!r.valid()) {
                sol::error err = r;
                std::cerr << "error: init of '" << inst->name << "': " << err.what() << "\n";
                if (opts_.onError == OnError::Abort) return 1;
            }
        }
    }

    std::size_t index = 0;
    bool halted = false;
    for (const auto &file : opts_.files) {
        if (halted) break;
        std::ifstream in(file);
        if (!in) {
            std::cerr << "error: cannot open file: " << file << "\n";
            return 1;
        }
        auto games = parseGames(in);
        for (auto &game : games) {
            ++index;
            current_index_ = index;
            if (processGame(game, index)) {
                halted = true;
                break;
            }
        }
    }

    // Call finish() in pipeline order.
    for (auto &inst : plugins_) {
        sol::optional<sol::protected_function> finish = inst->plugin["finish"];
        if (finish) {
            sol::protected_function_result r = (*finish)(inst->ctx);
            if (!r.valid()) {
                sol::error err = r;
                std::cerr << "error: finish of '" << inst->name << "': " << err.what() << "\n";
            }
        }
    }

    // Writers flush on every write and close on destruction; just release engines.
    for (auto &inst : plugins_) {
        inst->engines.clear();
    }

    return 0;
}

} // namespace tictac
