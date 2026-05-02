-- Per-game engine analysis with three artifacts written to CWD:
--
--   game-<id>.pgn   annotated PGN — moves carry ?!/? /??, with NAG-style
--                   { class, NN cp loss } comments
--   game-<id>.org   org-mode stats: accuracy %, avg cp loss, notable moves
--   game-<id>.png   eval graph rendered by gnuplot (piped over io.popen)
--
-- Algorithm: walk every played ply, ask the engine for its top line, store
-- white-relative cp; then for each move compare the eval before and after
-- (from the mover's POV) to derive cp loss and a Lichess-style accuracy
-- score per move.
--
-- Run with:
--   ./tictac search opening e4 e5 \
--     --plugin examples/analyze_game.lua \
--     --engine /usr/games/stockfish \
--     --engine-option Threads=1 --engine-option Hash=128 \
--     --limit 3
--
-- Requires: --engine, --plugin, and `gnuplot` on PATH.

if not tictac or not tictac.engine then
    error("this plugin requires --engine PATH")
end

local DEPTH    = 21
local STARTPOS = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
local MATE_CP  = 100000

-- Saturation used for cp-loss math: a saturated mate eval (+MATE_CP) on
-- both sides of a move would otherwise produce a fake six-figure "loss"
-- when the mover correctly continues a forced mate. Clamping the inputs
-- to the loss subtraction caps any single-move loss at 2*SATURATE.
local SATURATE = 1500

local function white_score(line, stm_white)
    if line.mate then
        local sign = (line.mate > 0) and 1 or -1
        return (stm_white and sign or -sign) * MATE_CP
    end
    return stm_white and line.score or -line.score
end

-- Lichess win-percentage curve: cp -> win probability for the side to move.
local function win_pct(cp)
    return 50 + 50 * (2 / (1 + math.exp(-0.00368208 * cp)) - 1)
end

-- Lichess per-move accuracy from the loss in win percentage points.
local function move_accuracy(wp_loss)
    local acc = 103.1668 * math.exp(-0.04354 * wp_loss) - 3.1669
    if acc < 0   then return 0   end
    if acc > 100 then return 100 end
    return acc
end

-- Classification thresholds operate on the LOSS in win percentage points
-- rather than raw cp. cp saturates at ±MATE_CP / mate-in-N boundaries and
-- produces fake six-figure "blunders" when the engine just runs out of
-- depth on a forced sequence; win_pct compresses both ends and tracks
-- practical strength loss. Cutoffs are Lichess's published values.
local function classify(wp_loss, was_winning, still_winning)
    if was_winning and not still_winning and wp_loss >= 15 then
        return "??", "missed win"
    end
    if wp_loss >= 20 then return "??", "blunder"     end
    if wp_loss >= 10 then return "?",  "mistake"     end
    if wp_loss >= 5  then return "?!", "inaccuracy"  end
    return "", ""
end

local function result_token(result)
    if result == "win"  then return "1-0"     end
    if result == "lose" then return "0-1"     end
    if result == "draw" then return "1/2-1/2" end
    return "*"
end

local function write_pgn(filename, game, sans, per_move)
    local f = assert(io.open(filename, "w"))
    f:write('[Event "',     game.event or "", '"]\n')
    f:write('[Date "',      game.date  or "", '"]\n')
    f:write('[White "',     game.white or "", '"]\n')
    f:write('[Black "',     game.black or "", '"]\n')
    f:write('[WhiteElo "',  tostring(game.white_elo or 0), '"]\n')
    f:write('[BlackElo "',  tostring(game.black_elo or 0), '"]\n')
    local r = result_token(game.result)
    f:write('[Result "', r, '"]\n')
    f:write('[Annotator "tictac analyze_game.lua"]\n\n')

    local out = {}
    for ply = 1, #sans do
        if ply % 2 == 1 then
            out[#out + 1] = string.format("%d.", (ply + 1) // 2)
        end
        local pm = per_move[ply]
        if pm.sym ~= "" then
            out[#out + 1] = sans[ply] .. pm.sym
            out[#out + 1] = string.format("{%s, %d cp loss}", pm.label, pm.loss)
        else
            out[#out + 1] = sans[ply]
        end
    end
    out[#out + 1] = r
    f:write(table.concat(out, " "), "\n")
    f:close()
end

local function write_org(filename, game, per_move, summary)
    local f = assert(io.open(filename, "w"))
    f:write(string.format("#+TITLE: Game %d Analysis: %s vs %s\n",
        game.id, game.white or "?", game.black or "?"))
    f:write("#+STARTUP: showall\n\n")

    f:write("* Overview\n")
    f:write(string.format("- Event :: %s\n", game.event or ""))
    f:write(string.format("- Date :: %s\n",  game.date  or ""))
    f:write(string.format("- Result :: %s\n", result_token(game.result)))
    f:write(string.format("- White :: %s (%d)\n",
        game.white or "?", game.white_elo or 0))
    f:write(string.format("- Black :: %s (%d)\n", game.black or "?", game.black_elo or 0))
    f:write(string.format("- Plies :: %d\n\n", #per_move))

    f:write("* Accuracy\n")
    f:write("| Side  | Accuracy | Avg CP Loss |\n")
    f:write("|-------+----------+-------------|\n")
    f:write(string.format("| White |  %5.1f%% | %11.1f |\n",
        summary.acc_white, summary.loss_white))
    f:write(string.format("| Black |  %5.1f%% | %11.1f |\n\n",
        summary.acc_black, summary.loss_black))

    f:write("* Move Counts\n")
    f:write("| Side  | Inacc. | Mistake | Blunder | Missed Win |\n")
    f:write("|-------+--------+---------+---------+------------|\n")
    f:write(string.format("| White |  %5d | %7d | %7d | %10d |\n",
        summary.cnt_white.inaccuracy, summary.cnt_white.mistake,
        summary.cnt_white.blunder,    summary.cnt_white.missed_win))
    f:write(string.format("| Black |  %5d | %7d | %7d | %10d |\n\n",
        summary.cnt_black.inaccuracy, summary.cnt_black.mistake,
        summary.cnt_black.blunder,    summary.cnt_black.missed_win))

    f:write("* Notable Moves\n")
    local any = false
    for _, pm in ipairs(per_move) do
        if pm.sym ~= "" then
            if not any then
                f:write("| Ply | Mover | Move    | CP Loss | Class       |\n")
                f:write("|-----+-------+---------+---------+-------------|\n")
                any = true
            end
            f:write(string.format("| %3d | %-5s | %-7s | %7d | %-11s |\n",
                pm.ply, pm.mover_white and "white" or "black",
                pm.san, pm.loss, pm.label))
        end
    end
    if not any then f:write("(none — clean game)\n") end
    f:close()
end

local gnuplot_warned = false
local function gnuplot_available()
    local rc = os.execute("command -v gnuplot >/dev/null 2>&1")
    -- Lua 5.4: os.execute returns true on rc == 0, otherwise (nil, 'exit'|'signal', code).
    -- Lua 5.1: returns the integer exit code directly.
    return rc == true or rc == 0
end

local function plot_eval(filename, white_evals, game)
    if not gnuplot_available() then
        if not gnuplot_warned then
            io.stderr:write("warning: gnuplot not on PATH; skipping plot output\n")
            gnuplot_warned = true
        end
        return
    end
    local gp = io.popen("gnuplot 2>/dev/null", "w")
    if not gp then return end
    local title = string.format("Game %d: %s vs %s",
        game.id, game.white or "?", game.black or "?")
    title = title:gsub("'", "''")  -- escape for gnuplot single-quoted string

    gp:write("set terminal pngcairo size 900,400 enhanced font 'Sans,10'\n")
    gp:write(string.format("set output '%s'\n", filename))
    gp:write(string.format("set title '%s' noenhanced\n", title))
    gp:write("set xlabel 'Ply'\n")
    gp:write("set ylabel 'Centipawns (white-relative)'\n")
    gp:write("set yrange [-1500:1500]\n")
    gp:write("set grid\n")
    gp:write("set zeroaxis lt -1 lw 1\n")
    gp:write("set key off\n")
    gp:write("plot '-' with lines lw 2 lc rgb '#1f77b4'\n")
    for i = 1, #white_evals do
        local v = white_evals[i]
        if v >  1500 then v =  1500 end
        if v < -1500 then v = -1500 end
        gp:write(string.format("%d %d\n", i - 1, v))
    end
    gp:write("e\n")
    gp:close()
end

function on_match(game)
    local plies = game.move_count
    if plies == 0 then return false end

    local sans, ucis = {}, {}
    for i, san, uci in game:moves() do
        sans[i] = san
        ucis[i] = uci
    end

    -- White-relative cp at every ply 0..plies (length = plies + 1).
    local white_evals = {}
    local prefix = {}
    for ply = 0, plies do
        local res
        if ply == 0 then
            res = tictac.engine.analyze({
                fen = STARTPOS, depth = DEPTH, multipv = 1,
            })
        else
            prefix[ply] = ucis[ply]
            res = tictac.engine.analyze({
                moves = prefix, depth = DEPTH, multipv = 1,
            })
        end
        local stm_white = (ply % 2 == 0)
        white_evals[ply + 1] = white_score(res.lines[1], stm_white)
    end

    local per_move = {}
    local total_loss_w, total_loss_b = 0, 0
    local count_w,      count_b      = 0, 0
    local sum_acc_w,    sum_acc_b    = 0, 0
    local cnt_w = { inaccuracy = 0, mistake = 0, blunder = 0, missed_win = 0 }
    local cnt_b = { inaccuracy = 0, mistake = 0, blunder = 0, missed_win = 0 }

    for ply = 1, plies do
        local mover_white  = (ply % 2 == 1)
        local before_w     = white_evals[ply]      -- before this move
        local after_w      = white_evals[ply + 1]  -- after  this move
        local before_mover = mover_white and before_w or -before_w
        local after_mover  = mover_white and after_w  or -after_w

        local before_clamped = math.max(-SATURATE, math.min(SATURATE, before_mover))
        local after_clamped  = math.max(-SATURATE, math.min(SATURATE, after_mover))
        local cp_loss        = math.max(0, before_clamped - after_clamped)

        local wp_before     = win_pct(before_mover)
        local wp_after      = win_pct(after_mover)
        local wp_loss       = math.max(0, wp_before - wp_after)
        local acc           = move_accuracy(wp_loss)
        local was_winning   = wp_before >= 75
        local still_winning = wp_after  >= 50
        local sym, label    = classify(wp_loss, was_winning, still_winning)

        per_move[ply] = {
            ply = ply, mover_white = mover_white, san = sans[ply],
            loss = cp_loss, sym = sym, label = label, accuracy = acc,
        }

        local cnt = mover_white and cnt_w or cnt_b
        if label == "inaccuracy" then cnt.inaccuracy = cnt.inaccuracy + 1
        elseif label == "mistake"     then cnt.mistake    = cnt.mistake + 1
        elseif label == "blunder"     then cnt.blunder    = cnt.blunder + 1
        elseif label == "missed win"  then cnt.missed_win = cnt.missed_win + 1
        end

        if mover_white then
            total_loss_w = total_loss_w + cp_loss
            count_w      = count_w + 1
            sum_acc_w    = sum_acc_w + acc
        else
            total_loss_b = total_loss_b + cp_loss
            count_b      = count_b + 1
            sum_acc_b    = sum_acc_b + acc
        end
    end

    local summary = {
        acc_white  = count_w > 0 and sum_acc_w / count_w   or 0,
        acc_black  = count_b > 0 and sum_acc_b / count_b   or 0,
        loss_white = count_w > 0 and total_loss_w / count_w or 0,
        loss_black = count_b > 0 and total_loss_b / count_b or 0,
        cnt_white  = cnt_w,
        cnt_black  = cnt_b,
    }

    local id = game.id
    write_pgn(string.format("game-%d.pgn", id), game, sans,    per_move)
    write_org(string.format("game-%d.org", id), game, per_move, summary)
    plot_eval(string.format("game-%d.png", id), white_evals,    game)

    io.write(string.format(
        "game %-7d  white %5.1f%% (avg %4d cp)  |  black %5.1f%% (avg %4d cp)\n",
        id, summary.acc_white, math.floor(summary.loss_white + 0.5),
            summary.acc_black, math.floor(summary.loss_black + 0.5)))
    io.flush()

    return true
end
