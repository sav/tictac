-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- engine_check.lua -- drive ctx.engine() against the mock UCI engines under
-- mock/ and assert on what comes back. `mode=` selects the case; the success
-- modes assert exact values (the mock's output is fixed), the failure modes
-- expect the call to raise, which surfaces as a plugin error.

local plugin = {}

local function engine(ctx)
  local path = ctx.args:get("engine", "mock/uci_engine.sh")
  return ctx.engine(path)
end

-- Descriptors currently open in *this* process. $PPID inside the popen'd shell
-- is tictac, so this counts our own fds rather than the helper shell's.
local function openFds()
  local p = io.popen("ls /proc/$PPID/fd 2>/dev/null | wc -l")
  if not p then return nil end
  local n = tonumber(p:read("l"))
  p:close()
  return n
end

function plugin.process(input, ctx)
  local mode = ctx.args:get("mode", "analyse")

  -- Success paths: the mock always answers "bestmove e2e4" with a fixed
  -- "score cp 25" first line, so every field below is a known value.
  if mode == "analyse" then
    local a = engine(ctx):analyse(input.board, { depth = 12 })
    assert(a.bestmove == "e2e4", "bestmove: " .. tostring(a.bestmove))
    assert(a.score == 25, "score: " .. tostring(a.score))
    assert(a.mate == nil, "mate must be unset for a cp score")
    assert(a.depth == 12, "depth: " .. tostring(a.depth))
    assert(a.nodes == 4096, "nodes: " .. tostring(a.nodes))
    assert(a.time == 7, "time: " .. tostring(a.time))
    assert(a.nps == 585142, "nps: " .. tostring(a.nps))
    assert(#a.pv == 2 and a.pv[1] == "e2e4" and a.pv[2] == "e7e5",
      "pv: " .. table.concat(a.pv, " "))
    -- multipv defaults to 1, so the per-line table stays absent.
    assert(a.lines == nil, "lines must be absent for single-pv analysis")
    return input
  end

  if mode == "bestmove" then
    local mv = engine(ctx):bestmove(input.board, { depth = 12 })
    assert(mv == "e2e4", "bestmove: " .. tostring(mv))
    return input
  end

  -- A mate score must reach Lua as `mate`, with `score` left unset.
  if mode == "mate" then
    local a = engine(ctx):analyse(input.board, { depth = 12 })
    assert(a.mate == 3, "mate: " .. tostring(a.mate))
    assert(a.score == nil, "score must be unset for a mate score")
    return input
  end

  -- Three ranked lines, in multipv order. The mock also emits a scoreless
  -- "currmove" info line, which must not blank a slot or add one.
  if mode == "multipv" then
    local a = engine(ctx):analyse(input.board, { depth = 12, multipv = 3 })
    assert(a.lines ~= nil, "lines must be present when multipv > 1")
    assert(#a.lines == 3, "lines: " .. tostring(#a.lines))
    assert(a.lines[1].score == 25, "line 1 score: " .. tostring(a.lines[1].score))
    assert(a.lines[2].score == 15, "line 2 score: " .. tostring(a.lines[2].score))
    assert(a.lines[3].score == 5, "line 3 score: " .. tostring(a.lines[3].score))
    assert(a.lines[1].pv[1] == "e2e4", "line 1 pv: " .. tostring(a.lines[1].pv[1]))
    assert(a.lines[2].pv[1] == "d2d4", "line 2 pv: " .. tostring(a.lines[2].pv[1]))
    return input
  end

  -- Options must reach the wire in UCI spelling: an integer stays an integer
  -- ("16", never "16.000000"), and a bool becomes true/false.
  if mode == "options" then
    local log = ctx.args:get("log")
    assert(log ~= nil, "options mode requires log=")
    local e = ctx.engine("mock/uci_engine.sh", { Threads = 16, Hash = 32.0, Ponder = false, Style = "solid" })
    -- setOption takes the same value types as the constructor table, so the
    -- two must spell a Lua number or boolean for UCI identically.
    e:setOption("Skill Level", 10)
    e:setOption("Chess960", true)
    e:setOption("Contempt", 1.5)
    e:setOption("Book", "on")
    -- The constructor's options are already on disk by the time it returns (its
    -- isready/readyok forces the engine to have consumed them), but a bare
    -- setOption has no such round trip. Drive one so the log is complete.
    e:bestmove(input.board, { depth = 1 })
    local seen = {}
    for entry in io.lines(log) do
      local k, v = entry:match("^(.-)=(.*)$")
      seen[k] = v
    end
    assert(seen.Threads == "16", "Threads sent as: " .. tostring(seen.Threads))
    assert(seen.Hash == "32", "Hash sent as: " .. tostring(seen.Hash))
    assert(seen.Ponder == "false", "Ponder sent as: " .. tostring(seen.Ponder))
    assert(seen.Style == "solid", "Style sent as: " .. tostring(seen.Style))
    assert(seen["Skill Level"] == "10", "Skill Level sent as: " .. tostring(seen["Skill Level"]))
    assert(seen.Chess960 == "true", "Chess960 sent as: " .. tostring(seen.Chess960))
    assert(seen.Contempt == "1.5", "Contempt sent as: " .. tostring(seen.Contempt))
    assert(seen.Book == "on", "Book sent as: " .. tostring(seen.Book))
    return input
  end

  -- Failure paths: each of these must raise rather than return.

  -- Spawning a path that does not exist: execlp fails, the child exits 127,
  -- and the handshake read hits EOF.
  if mode == "missing" then
    ctx.engine("mock/no_such_engine.sh")
    return input
  end

  -- A binary that exits without answering "uci".
  if mode == "not_uci" then
    ctx.engine("mock/not_an_engine.sh")
    return input
  end

  -- An engine that dies mid-search: the handshake succeeds, "go" kills it.
  if mode == "dies" then
    ctx.engine("mock/dies_on_go.sh"):analyse(input.board, { depth = 12 })
    return input
  end

  -- A constructor that throws must still release its pipes and reap the child.
  -- Spawn a failing engine often enough that leaking two descriptors per attempt
  -- would exhaust the process limit: the leak shows up as the error changing to
  -- "failed to create pipes" partway through, so assert it never does.
  if mode == "leak" then
    local n = ctx.args:number("times", 50)
    local before = openFds()
    assert(before ~= nil, "cannot read /proc; this test needs Linux")
    for i = 1, n do
      local ok, err = pcall(function() ctx.engine("mock/not_an_engine.sh") end)
      assert(not ok, "attempt " .. i .. " should have failed")
      assert(tostring(err):find("process closed unexpectedly", 1, true)
          or tostring(err):find("write failed", 1, true),
        "attempt " .. i .. " failed with an unexpected error: " .. tostring(err))
    end
    local after = openFds()
    -- Two descriptors would leak per failed spawn; allow no growth at all.
    assert(after <= before,
      string.format("leaked %d descriptors over %d failed spawns (%d -> %d)",
        after - before, n, before, after))
    return input
  end

  -- analyse() requires at least one search limit.
  if mode == "no_limits" then
    engine(ctx):analyse(input.board, {})
    return input
  end

  -- multipv is validated up front instead of silently returning fewer lines.
  if mode == "multipv_zero" then
    engine(ctx):analyse(input.board, { depth = 12, multipv = 0 })
    return input
  end

  if mode == "multipv_huge" then
    engine(ctx):analyse(input.board, { depth = 12, multipv = 100 })
    return input
  end

  error("engine_check: unknown mode '" .. tostring(mode) .. "'")
end

return plugin
