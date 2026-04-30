-- Keep games where both sides are rated >= 2400.
-- Adjust the threshold to taste; lichess data skews amateur.
function on_match(game)
    return game.white_elo >= 2400 and game.black_elo >= 2400
end
