-- Mine tactical puzzles by eval swing + only-move detection.
--
-- For each ply we ask the engine for the top two lines (MultiPV=2). When the
-- side that just moved drops the white-relative eval by BLUNDER_CP or more,
-- and the side now to move has an "only move" (best line beats second-best
-- by ONLY_CP), the position is flagged as a puzzle and its FEN is printed.
--
-- Output: one FEN per puzzle, separated by a blank line. Caps at MAX_PUZZLES
-- across the whole run.
--
-- Run with:
--   ./tictac search opening e4 \
--     --plugin examples/puzzles.lua \
--     --engine /usr/games/stockfish \
--     --engine-option Threads=1 --engine-option Hash=64 \
--     --limit 500

if not tictac or not tictac.engine then
    error("this plugin requires --engine PATH")
end

local DEPTH        = 10    -- per-position search depth
local BLUNDER_CP   = 200   -- centipawn swing that flags a blunder
local ONLY_CP      = 150   -- best - second_best to qualify as only-move
local SKIP_OPENING = 10    -- ignore the first N plies (book theory)
local MAX_PUZZLES  = 20

local MATE_CP = 100000
local found   = 0

-- White-relative score for a line whose .score is from STM's POV.
local function white_score(line, stm_white)
    if line.mate then
        local sign = (line.mate > 0) and 1 or -1
        return (stm_white and sign or -sign) * MATE_CP
    end
    return stm_white and line.score or -line.score
end

-- True when best is decisively better than second (the puzzle has one answer).
local function is_only_move(best, second)
    if not best or not second then return false end
    if best.mate and not second.mate then return true end
    if best.mate and second.mate then
        return math.abs(best.mate) + 2 <= math.abs(second.mate)
    end
    if not best.mate and not second.mate then
        return (best.score - second.score) >= ONLY_CP
    end
    return false
end

function on_match(game)
    if found >= MAX_PUZZLES then return false end

    local moves = {}
    for i, _san, uci in game:moves() do moves[i] = uci end

    local prefix    = {}
    local prev_eval = nil

    for ply = 0, #moves do
        local stm_white = (ply % 2 == 0)
        local res = tictac.engine.analyze({
            moves   = prefix,
            depth   = DEPTH,
            multipv = 2,
        })
        local best     = res.lines[1]
        local second   = res.lines[2]
        local cur_eval = best and white_score(best, stm_white) or 0

        if prev_eval and ply > SKIP_OPENING then
            -- The side that just moved is the opposite of the current STM.
            local mover_white = not stm_white
            local drop
            if mover_white then
                drop = prev_eval - cur_eval
            else
                drop = cur_eval - prev_eval
            end

            if drop >= BLUNDER_CP and is_only_move(best, second) then
                io.write(game:fen(ply), "\n\n")
                io.flush()
                found = found + 1
                if found >= MAX_PUZZLES then return false end
            end
        end

        prev_eval = cur_eval
        if ply < #moves then
            prefix[ply + 1] = moves[ply + 1]
        end
    end

    return false
end
