-- Exercises game:fen([ply]).
-- For Game 1 (Alpha vs Beta, 1.e4 e5 2.Nf3 Nc6 3.Bb5), prints:
--   * starting FEN (ply=0)
--   * FEN after 1.e4 (ply=1)
--   * FEN after the full game (ply=move_count)
--   * default-arg FEN (no game.ply set on opening search → also ply=0)
function on_match(game)
    if game.white ~= "Alpha" then return true end
    io.write("start=",   game:fen(0),                "\n")
    io.write("ply1=",    game:fen(1),                "\n")
    io.write("end=",     game:fen(game.move_count),  "\n")
    io.write("default=", game:fen(),                 "\n")
    return true
end
