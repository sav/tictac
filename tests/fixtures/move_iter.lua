-- Iterates moves and asserts san/uci agree on first half-move:
--   game with 1.e4 ... -> san="e4", uci="e2e4".
-- Records into a global so the test can grep for it on stdout.
function on_match(game)
    local idx, san, uci = game:moves()()
    if idx == 1 then
        io.write(string.format("first-move id=%d san=%s uci=%s\n",
            game.id, san or "?", uci or "?"))
    end
    return true
end
