-- Keep only games played by White="Alpha". Used by the CLI test suite.
function on_match(game)
    return game.white == "Alpha"
end
