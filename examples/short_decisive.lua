-- Print every game we accept and keep only short, decisive ones.
-- Demonstrates iterating moves with game:moves().
function on_match(game)
    if game.move_count > 40 then return false end
    if game.result ~= "win" and game.result ~= "lose" then return false end

    io.write(string.format("Game #%d  %s vs %s  (%d plies, %s)\n",
        game.id, game.white, game.black, game.move_count, game.result))

    -- First few moves, in SAN
    local opening = {}
    for i, san, _uci in game:moves() do
        if i > 6 then break end
        opening[#opening + 1] = san
    end
    io.write("  opening: ", table.concat(opening, " "), "\n")
    return true
end
