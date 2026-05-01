-- Mine tactical puzzles in two passes.
--
--   1. Shallow single-PV walk over every ply, tracking the white-relative
--      eval. A move that drops the eval by BLUNDER_CP or more against the
--      mover marks the resulting position as a candidate.
--   2. Deep MultiPV recheck on each candidate. Emit the FEN when the side
--      now to move has a clear "only move" (best decisively beats second)
--      AND the position is decisive (best move achieves >= MIN_DECISIVE).
--
-- One puzzle per game: as soon as a candidate verifies, the FEN is printed
-- and on_match returns true so the CLI's --limit caps total puzzle count.
-- A game with no verified puzzle returns false and is silently dropped.
--
-- Run with:
--   ./tictac search opening e4 c5 \
--     --plugin examples/puzzles.lua \
--     --engine /usr/games/stockfish \
--     --engine-option Threads=1 --engine-option Hash=128 \
--     --limit 20

if not tictac or not tictac.engine then
    error("this plugin requires --engine PATH")
end

local SKIP_OPENING  = 8     -- skip the first N plies (book theory)
local DEPTH_SHALLOW = 8     -- pre-filter depth (per ply)
local DEPTH_DEEP    = 14    -- only-move verification depth
local MULTIPV       = 3
local BLUNDER_CP    = 150   -- white-relative swing against the mover
local ONLY_CP       = 180   -- best - second cp gap to call it forced
local MIN_DECISIVE  = 80    -- best move must achieve this (STM-relative cp)

local MATE_CP = 100000

-- Convert a line's score (STM-relative from UCI) to white-relative cp.
-- Returns nil when the line carries no usable score.
local function white_score(line, stm_white)
    if not line then return nil end
    if line.mate then
        local sign = (line.mate > 0) and 1 or -1
        return (stm_white and sign or -sign) * MATE_CP
    end
    if line.score == nil then return nil end
    return stm_white and line.score or -line.score
end

local function is_only_move(best, second)
    if not best then return false end
    -- A single returned PV at multipv >= 2 means there is effectively no
    -- viable alternative — the strongest possible forced-move signal.
    if not second then return true end
    if best.mate then
        if not second.mate then return true end
        return math.abs(best.mate) + 1 <= math.abs(second.mate)
    end
    if second.mate then
        -- Best is cp, second is mate: typically means second loses to a
        -- forced mate against STM, so best really is the only saving move.
        return second.mate < 0
    end
    return (best.score - second.score) >= ONLY_CP
end

local function is_decisive(best)
    if not best then return false end
    if best.mate then return best.mate > 0 end
    return best.score ~= nil and best.score >= MIN_DECISIVE
end

-- Pass 1: walk every played ply with a shallow single-PV eval, return plies
-- whose arrival represents an eval swing of >= BLUNDER_CP against the mover.
-- We start at ply 1 because the engine API rejects an empty `moves` list,
-- and the starting position is roughly equal anyway.
local function scan_blunders(moves)
    local candidates = {}
    local prefix     = {}
    local prev       = 0   -- treat the starting position as ~equal
    for ply = 1, #moves do
        prefix[ply] = moves[ply]
        local res = tictac.engine.analyze({
            moves   = prefix,
            depth   = DEPTH_SHALLOW,
            multipv = 1,
        })
        local stm_white = (ply % 2 == 0)
        local cur = white_score(res.lines[1], stm_white)
        if cur and ply > SKIP_OPENING then
            -- The mover is the side that played the move from ply-1 -> ply,
            -- which is the opposite of the current side-to-move.
            local mover_white = not stm_white
            local drop
            if mover_white then drop = prev - cur else drop = cur - prev end
            if drop >= BLUNDER_CP then candidates[#candidates + 1] = ply end
        end
        if cur then prev = cur end
    end
    return candidates
end

-- Pass 2: deeper MultiPV recheck on a single candidate ply.
local function verify_puzzle(moves, ply)
    local prefix = {}
    for k = 1, ply do prefix[k] = moves[k] end
    local res = tictac.engine.analyze({
        moves   = prefix,
        depth   = DEPTH_DEEP,
        multipv = MULTIPV,
    })
    local best   = res.lines[1]
    local second = res.lines[2]
    return is_only_move(best, second) and is_decisive(best)
end

function on_match(game)
    local moves = {}
    for i, _san, uci in game:moves() do moves[i] = uci end
    if #moves < SKIP_OPENING + 2 then return false end

    local candidates = scan_blunders(moves)
    if #candidates == 0 then return false end

    for _, ply in ipairs(candidates) do
        if verify_puzzle(moves, ply) then
            io.write(game:fen(ply), "\n\n")
            io.flush()
            return true
        end
    end
    return false
end
