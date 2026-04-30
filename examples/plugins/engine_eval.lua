-- Evaluate the FINAL position of every match with the configured UCI engine.
-- Keep games that ended within +/- 1 pawn (interesting endgames).
--
-- Run with:
--   ./tictac search opening e4 c5 \
--     --plugin examples/plugins/engine_eval.lua \
--     --engine /usr/games/stockfish \
--     --engine-option Threads=1 --engine-option Hash=64 \
--     --limit 5
--
-- Note: UCI scores are reported from the side-to-move's perspective.

if not tictac or not tictac.engine then
    error("this plugin requires --engine PATH")
end

local function uci_moves(game)
    local m = {}
    for i, _san, uci in game:moves() do
        m[i] = uci
    end
    return m
end

function on_match(game)
    local moves = uci_moves(game)
    local res = tictac.engine.analyze({ moves = moves, depth = 8 })

    local label
    if res.mate then
        label = string.format("M%+d", res.mate)
    else
        label = string.format("%+d cp", res.score)
    end

    io.write(string.format("Game #%-7d  %-20s vs %-20s  final=%s  pv=%s\n",
        game.id, game.white, game.black, label, res.pv[1] or "-"))

    -- Keep only "balanced" endings (closer than 1 pawn, not mating sequence).
    if res.mate then return false end
    return math.abs(res.score) <= 100
end
