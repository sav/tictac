-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- fetch.lua -- download a list of lichess or chess.com users' games into PGN.
-- A source plugin: it has no input game, so it does its work in init() and
-- writes PGN itself. Point tictac at an empty database and discard the stream:
--
--   tictac --file /dev/null --no-output --plugin \
--     "fetch.lua user=zav1o user=SSMMG out=games.pgn max=100 clocks evals"
--   tictac --file /dev/null --no-output --plugin \
--     "fetch.lua site=chesscom user=hikaru out=games.pgn max=100"
--
-- With dir= it writes one <user>.pgn per user; with out= it concatenates every
-- user into one file; with neither it falls back to ctx.out. Usernames come
-- from repeated/comma-separated user= and/or a file= of whitespace-separated
-- names. Clocks and evals are requested by default so the result feeds
-- cheat.lua directly. Needs curl on PATH.
--
-- site=lichess (default) hits lichess.org/api/games/user and honours every
-- filter below; token= raises its rate limit. site=chesscom hits the
-- api.chess.com monthly archives (newest months first until max is reached),
-- supports only user=/max=, and takes no token. Both 404 without a User-Agent,
-- so ua= is always sent.
--
-- new makes fetching incremental: when the target file already holds games it
-- reads just the first and last of them for the newest timestamp, fetches only
-- games newer than that (lichess via since=, chesscom by stopping the archive
-- walk at the cutoff), and prepends them. Needs a per-user file -- out= with one
-- user or dir=.

local plugin = {
  meta = {
    name    = "fetch",
    version = "1.1.0",
    description = "Download lichess/chess.com users' games into PGN.",
    args = {
      site    = { type = "string", default = "lichess", help = "lichess (default) or chesscom." },
      user    = { type = "string",                help = "Username. Repeatable and comma-separated." },
      file    = { type = "string",                help = "File of whitespace/comma/newline-separated usernames." },
      out     = { type = "string",                help = "Concatenate every user into this one PGN (default: ctx.out)." },
      dir     = { type = "string",                help = "Write one <user>.pgn per user into this directory prefix." },
      token   = { type = "string",                help = "Lichess API token (Bearer); raises the rate limit. Ignored for chesscom." },
      max     = { type = "number",                help = "Max games per user (newest first)." },
      rated   = { type = "bool",                  help = "lichess: only rated (true) or only casual (false) games." },
      perf    = { type = "string",                help = "lichess: perfType filter, e.g. blitz,rapid,classical." },
      color   = { type = "string",                help = "lichess: only games the user played as 'white' or 'black'." },
      since   = { type = "number",                help = "lichess: only games at or after this epoch-ms timestamp." },
      before  = { type = "number",                help = "lichess: only games before this epoch-ms timestamp (API 'until')." },
      clocks  = { type = "bool",   default = true, help = "lichess: include [%clk] clock comments." },
      evals   = { type = "bool",   default = true, help = "lichess: include [%eval] analysis comments when present." },
      opening = { type = "bool",   default = true, help = "lichess: include ECO/Opening headers." },
      curl    = { type = "string", default = "curl", help = "curl binary to invoke." },
      ua      = { type = "string", default = "tictac-fetch/1.0", help = "User-Agent; both sites 404 requests without one." },
      delay   = { type = "number", default = 1,    help = "Seconds to pause between users (be kind to the API)." },
      new     = { type = "bool",   default = false, help = "Incremental: when the target file already has games, fetch only those newer than the newest present and prepend them. Reads just the first and last existing games to find the cutoff. Needs out= (single user) or dir=." },
    },
  },
}

local function shsq(s)
  return "'" .. tostring(s):gsub("'", "'\\''") .. "'"
end

local function usernames(ctx)
  local names, seen = {}, {}
  local function add(raw)
    for name in tostring(raw):gmatch("[^%s,]+") do
      if not seen[name] then
        seen[name] = true
        names[#names + 1] = name
      end
    end
  end
  for _, u in ipairs(ctx.args:list("user") or {}) do add(u) end
  local file = ctx.args:get("file")
  if file then
    local f, err = io.open(file, "r")
    if not f then error("fetch: cannot open user file '" .. file .. "': " .. tostring(err)) end
    add(f:read("*a"))
    f:close()
  end
  return names
end

-- Split a multi-game PGN into one string per game, each self-contained and
-- keeping its trailing blank line so a reordered concatenation stays valid.
local function split_games(text)
  local games = {}
  local idx = text:find("%[Event ")
  while idx do
    local nxt = text:find("\n%[Event ", idx + 1)
    if nxt then
      games[#games + 1] = text:sub(idx, nxt)
      idx = nxt + 1
    else
      games[#games + 1] = text:sub(idx)
      idx = nil
    end
  end
  return games
end

-- Pull the broken-down UTC date/time out of one game's headers, preferring the
-- UTC tags both sites emit and falling back to the display Date/Time. Returns
-- year, month, day, hour, min, sec (time defaulting to 0) or nil.
local function game_fields(g)
  local ud = g:match("%[UTCDate%s+\"([%d%.]+)\"%]") or g:match("%[Date%s+\"([%d%.]+)\"%]")
  if not ud then return nil end
  local y, mo, d = ud:match("(%d+)%.(%d+)%.(%d+)")
  if not y then return nil end
  local ut = g:match("%[UTCTime%s+\"([%d:]+)\"%]") or g:match("%[StartTime%s+\"([%d:]+)\"%]")
          or g:match("%[Time%s+\"([%d:]+)\"%]")
  local h, mi, s = "0", "0", "0"
  if ut then h, mi, s = ut:match("(%d+):(%d+):(%d+)") end
  return tonumber(y), tonumber(mo), tonumber(d),
         tonumber(h) or 0, tonumber(mi) or 0, tonumber(s) or 0
end

-- A single monotone integer key (YYYYMMDDhhmmss) for ordering games by time.
local function game_key(g)
  local y, mo, d, h, mi, s = game_fields(g)
  if not y then return nil end
  return ((((y * 100 + mo) * 100 + d) * 100 + h) * 100 + mi) * 100 + s
end

-- Epoch seconds for a game's UTC fields. os.time reads a table as local time,
-- so correct by the local-to-UTC offset to land on a true UTC epoch.
local function game_epoch(g)
  local y, mo, d, h, mi, s = game_fields(g)
  if not y then return nil end
  local lt = os.time({ year = y, month = mo, day = d, hour = h, min = mi, sec = s })
  if not lt then return nil end
  local off = os.difftime(os.time(os.date("!*t", lt)), lt)
  return lt - off
end

-- The newest game already in `path`, examining only the first and last game in
-- the file (either end may be the newest, depending on how it was written).
-- Returns { key, epoch, text } -- text is the whole existing file, so the
-- caller can prepend new games without re-reading it -- or nil when the file is
-- absent or holds no parseable game.
local function newest_in_file(path)
  local f = io.open(path, "r")
  if not f then return nil end
  local text = f:read("*a") or ""
  f:close()
  local games = split_games(text)
  if #games == 0 then return nil end
  local best, bestg
  for _, g in ipairs({ games[1], games[#games] }) do
    local k = game_key(g)
    if k and (not best or k > best) then best, bestg = k, g end
  end
  if not best then return nil end
  return { key = best, epoch = game_epoch(bestg), text = text }
end

local function http_get(ctx, url, accept, auth)
  local cmd = {
    shsq(ctx.args:get("curl", "curl")), "-sS", "-f", "-g",
    "-A", shsq(ctx.args:get("ua", "tictac-fetch/1.0")),
  }
  if accept then
    cmd[#cmd + 1] = "-H"
    cmd[#cmd + 1] = shsq("Accept: " .. accept)
  end
  if auth then
    cmd[#cmd + 1] = "-H"
    cmd[#cmd + 1] = shsq("Authorization: " .. auth)
  end
  cmd[#cmd + 1] = shsq(url)
  local f, err = io.popen(table.concat(cmd, " "), "r")
  if not f then error("fetch: io.popen failed: " .. tostring(err)) end
  local data = f:read("*a") or ""
  local ok = f:close()
  return data, ok
end

local function lichess_query(ctx, since_ms)
  local parts = {}
  local function put(k, v)
    if v ~= nil then parts[#parts + 1] = k .. "=" .. tostring(v) end
  end
  local function putint(k, v)
    if v ~= nil then parts[#parts + 1] = string.format("%s=%.0f", k, v) end
  end
  putint("max", ctx.args:number("max"))
  local rated = ctx.args:bool("rated")
  if rated ~= nil then put("rated", rated and "true" or "false") end
  put("perfType", ctx.args:get("perf"))
  put("color", ctx.args:get("color"))
  -- An incremental cutoff narrows the server-side window; a client-side filter
  -- still trims the boundary second, so this only has to be close, not exact.
  putint("since", since_ms or ctx.args:number("since"))
  putint("until", ctx.args:number("before"))
  put("clocks", ctx.args:bool("clocks") and "true" or "false")
  put("evals", ctx.args:bool("evals") and "true" or "false")
  put("opening", ctx.args:bool("opening") and "true" or "false")
  if #parts == 0 then return "" end
  return "?" .. table.concat(parts, "&")
end

local function fetch_lichess(ctx, user, q)
  local token = ctx.args:get("token")
  local auth = token and ("Bearer " .. token) or nil
  return http_get(ctx, "https://lichess.org/api/games/user/" .. user .. q,
    "application/x-chess-pgn", auth)
end

local function fetch_chesscom(ctx, user, max, cutoff)
  local list, ok = http_get(ctx,
    "https://api.chess.com/pub/player/" .. user:lower() .. "/games/archives",
    "application/json")
  if not ok then return "", false end
  -- The archives list runs oldest month to newest; walk it newest month first.
  local months = {}
  for u in list:gmatch("https://[%w%.%-/]+/games/%d+/%d+") do months[#months + 1] = u end
  if #months == 0 then return "", false end

  -- Build a globally newest-first slice. Each month's own game order is not
  -- guaranteed, so sort it by timestamp (newest first) before appending. With
  -- an incremental cutoff, stop the moment a game at or older than it appears:
  -- everything beyond is already on disk, so no older month is even fetched.
  local ordered = {}
  local done = false
  for i = #months, 1, -1 do
    local pgn, pok = http_get(ctx, months[i] .. "/pgn", "application/x-chess-pgn")
    if pok and pgn:find("%[Event ") then
      local gs = split_games(pgn)
      table.sort(gs, function(a, b) return (game_key(a) or 0) > (game_key(b) or 0) end)
      for _, g in ipairs(gs) do
        if cutoff and (game_key(g) or 0) <= cutoff then done = true; break end
        ordered[#ordered + 1] = g
        if max and #ordered >= max then done = true; break end
      end
    end
    if done then break end
  end
  if #ordered == 0 then return "", false end
  return table.concat(ordered), true
end

function plugin.init(ctx)
  if ctx.worker ~= 1 then return end

  local site = ctx.args:get("site", "lichess"):gsub("%.", ""):lower()
  if site ~= "lichess" and site ~= "chesscom" then
    error("fetch: unknown site '" .. site .. "' (want lichess or chesscom)")
  end

  local names = usernames(ctx)
  if #names == 0 then error("fetch: no usernames; pass user= and/or file=") end

  local dir = ctx.args:get("dir")
  local out = ctx.args:get("out")
  if not dir and not out and not ctx.out then
    error("fetch: nowhere to write; pass out=, dir=, or drop --no-output")
  end

  -- Incremental needs a file to read a cutoff from and a per-user target, so it
  -- is off for stdout and for a shared out= holding more than one user.
  local incremental = ctx.args:bool("new", false) == true
  if incremental and not dir then
    if not out then
      ctx.log.warn("new: incremental needs out= or dir= (stdout has no file to read); fetching all")
      incremental = false
    elseif #names > 1 then
      ctx.log.warn("new: incremental with out= needs a single user; fetching all into %s", out)
      incremental = false
    end
  end

  local max = ctx.args:number("max")
  local delay = ctx.args:number("delay", 1)

  for i, user in ipairs(names) do
    local path = dir and (dir .. user .. ".pgn") or out   -- nil => stdout
    local prev = (incremental and path) and newest_in_file(path) or nil

    local data, ok
    if site == "chesscom" then
      data, ok = fetch_chesscom(ctx, user, max, prev and prev.key)
    else
      local q = lichess_query(ctx, prev and prev.epoch and (prev.epoch * 1000) or nil)
      data, ok = fetch_lichess(ctx, user, q)
    end

    local has_games = ok and (data or ""):find("%[Event ")
    if prev then
      -- Incremental: keep only games strictly newer than the cutoff, newest
      -- first, and prepend them to what is already on disk. No new games means
      -- the file is left untouched (no needless rewrite).
      local fresh = {}
      if has_games then
        for _, g in ipairs(split_games(data)) do
          local k = game_key(g)
          if k and k > prev.key then fresh[#fresh + 1] = g end
        end
      end
      if #fresh > 0 then
        local newtext = table.concat(fresh)
        if newtext:sub(-1) ~= "\n" then newtext = newtext .. "\n" end
        local w = ctx.open(path)
        w:write(newtext)
        w:write(prev.text)
        if prev.text:sub(-1) ~= "\n" then w:write("\n") end
      end
      ctx.log.info("%s: %d new game(s) (kept %d)", user, #fresh,
                   #split_games(prev.text))
    elseif not has_games then
      ctx.log.warn("%s: no games returned (user missing, private, or rate-limited)", user)
    else
      local games = select(2, data:gsub("%[Event ", ""))
      local w = path and ctx.open(path) or ctx.out
      w:write(data)
      if data:sub(-1) ~= "\n" then w:write("\n") end
      ctx.log.info("%s: %d game(s)", user, games)
    end
    if delay > 0 and i < #names then os.execute("sleep " .. tostring(delay)) end
  end
end

function plugin.process(input, ctx)
  return input
end

return plugin
