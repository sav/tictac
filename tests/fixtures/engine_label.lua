-- Engine-driven plugin used by the CLI test suite. Evaluates the first match
-- only (kept fast for tests) and writes a single "eval=..." line to stdout.
local seen = false

function on_match(game)
    if seen then return true end
    seen = true

    local moves = {}
    for i, _, uci in game:moves() do moves[i] = uci end

    local r = tictac.engine.analyze({ moves = moves, depth = 4 })
    local label
    if r.mate then
        label = string.format("M%+d", r.mate)
    else
        label = string.format("%+dcp", r.score)
    end
    io.write(string.format("eval=%s id=%d\n", label, game.id))
    return true
end
