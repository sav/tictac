-- Queue every accepted game's final position into the visualization buffer.
-- After the search completes, --viz spins up a Qt window so you can step
-- through the games with the Prev / Next buttons.
--
-- Run with:
--   ./tictac search opening e4 c5 \
--     --plugin examples/viz_browser.lua \
--     --viz \
--     --limit 10
--
-- This plugin does no engine work and no extra filtering; it just feeds
-- every match to the browser. Combine with another filter pattern (e.g.
-- elite.lua's elo gate) for a curated set.

if not tictac or not tictac.viz then
    error("this plugin requires --viz")
end

function on_match(game)
    local final = game:fen(game.move_count)
    tictac.viz.add(final, {
        id         = tostring(game.id),
        white      = game.white,
        black      = game.black,
        white_elo  = tostring(game.white_elo),
        black_elo  = tostring(game.black_elo),
        event      = game.event,
        date       = game.date,
        result     = game.result,
        move_count = tostring(game.move_count),
    })
    return true
end
