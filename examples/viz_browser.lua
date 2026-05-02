-- Queue every accepted game into the browser, with the full UCI move list
-- so the GUI's Prev / Next buttons can step through plies. The Next Game
-- button advances to the following match.
--
-- Run with:
--   ./tictac search opening e4 c5 \
--     --plugin examples/viz_browser.lua \
--     --viz \
--     --limit 10

if not tictac or not tictac.viz then
    error("this plugin requires --viz")
end

function on_match(game)
    local moves = {}
    for i, _san, uci in game:moves() do moves[i] = uci end

    tictac.viz.add({
        moves = moves,
        info = {
            id         = tostring(game.id),
            white      = game.white,
            black      = game.black,
            white_elo  = tostring(game.white_elo),
            black_elo  = tostring(game.black_elo),
            event      = game.event,
            date       = game.date,
            result     = game.result,
            move_count = tostring(game.move_count),
        },
    })
    return true
end
