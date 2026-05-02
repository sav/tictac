-- Designed for `tictac load <file.pgn> --plugin examples/stream_summary.lua`.
-- Prints a one-line summary per game while the PGN streams through; no
-- database is ever touched. The on_match return value is ignored by `load`.
local count = 0
function on_match(game)
    count = count + 1
    io.write(string.format("%4d. %-20s vs %-20s  %s  (%d plies)\n",
        count, game.white, game.black, game.result, game.move_count))
end
