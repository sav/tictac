-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- assess.lua -- one-shot engine-assistance assessment for a list of online
-- users: download their games, then analyse them with cheat.lua. A driver
-- plugin that does its work in init() over an empty input:
--
--   tictac --file /dev/null --no-output --plugin \
--     "assess.lua site=chesscom users=rsprudencio n=10 outdir=~/dl"
--
-- For each user it writes outdir/<user>.pgn (the downloaded games) and
-- outdir/<user>.txt (the cheat report, listing the n most suspicious moves).
-- It reuses the shipped fetch.lua and cheat.lua by re-invoking the running
-- tictac binary once per stage per user, so the pipeline model stays intact
-- (fetch is a source, cheat an aggregator) and there is no duplicated logic.
-- Linux-only: it finds the binary through /proc and its sibling plugins through
-- the Lua debug info. Needs curl and a UCI engine on PATH, like the two plugins
-- it drives.

local plugin = {
  meta = {
    name    = "assess",
    version = "1.0.0",
    description = "Download a user's games and screen them for engine assistance.",
    args = {
      site    = { type = "string", default = "lichess", help = "lichess (default) or chesscom." },
      users   = { type = "string",                help = "Username(s). Repeatable and comma-separated (user= is an alias)." },
      user    = { type = "string",                help = "Alias for users=." },
      n       = { type = "number", default = 10,  help = "Most-suspicious moves to list per user (passed to cheat.lua)." },
      outdir  = { type = "string", default = "./", help = "Directory for <user>.pgn and <user>.txt (default: ./)." },
      max     = { type = "number",                help = "Max games per user (newest first); forwarded to fetch." },
      token   = { type = "string",                help = "Lichess API token; forwarded to fetch (lichess only)." },
      perf    = { type = "string",                help = "Lichess perfType filter; forwarded to fetch." },
      rated   = { type = "bool",                  help = "Lichess rated filter; forwarded to fetch." },
      color   = { type = "string",                help = "Lichess color filter; forwarded to fetch." },
      engine  = { type = "string",                help = "UCI engine path; forwarded to cheat (default: cheat's own)." },
      depth   = { type = "number",                help = "Search depth; forwarded to cheat." },
      movetime= { type = "number",                help = "Search time per move (ms); forwarded to cheat." },
      multipv = { type = "number",                help = "Candidate moves per position; forwarded to cheat." },
      book    = { type = "number",                help = "Opening full-moves to skip; forwarded to cheat." },
      maxply  = { type = "number",                help = "Stop a game after this ply; forwarded to cheat." },
      maia    = { type = "string",                help = "lc0 binary for the Maia oracle; forwarded to cheat." },
      maia_dir     = { type = "string",           help = "Maia weights directory; forwarded to cheat." },
      maia_weights = { type = "string",           help = "Explicit Maia weights file; forwarded to cheat." },
      maia_backend = { type = "string",           help = "lc0 Backend option; forwarded to cheat." },
      jobs    = { type = "number", default = 0,    help = "Parallel workers for the analysis pass (0 = one per CPU)." },
      engines = { type = "number",                help = "Engine processes for the analysis pass (default: match jobs)." },
      bin     = { type = "string",                help = "tictac binary to re-invoke; defaults to the running one via /proc." },
    },
  },
}

local function shsq(s)
  return "'" .. tostring(s):gsub("'", "'\\''") .. "'"
end

-- Escape Lua pattern magic so a username is matched literally by cheat's
-- player= pattern, anchored to the whole header value.
local function player_pattern(user)
  return "^" .. user:gsub("[%^%$%(%)%%%.%[%]%*%+%-%?]", "%%%0") .. "$"
end

-- Absolute path of the running tictac binary. io.popen forks a shell whose
-- parent is this process, so /proc/$PPID/exe resolves to tictac itself.
local function running_binary()
  local f = io.popen("readlink -f /proc/$PPID/exe 2>/dev/null")
  if not f then return nil end
  local p = f:read("l")
  f:close()
  if p and #p > 0 then return p end
  return nil
end

-- Directory holding this plugin, so its siblings fetch.lua/cheat.lua can be
-- named regardless of the working directory. The Lua sandbox omits the debug
-- library, so recover the spec from the process's own argv (the --plugin value
-- carries "<dir>/assess.lua ...") and take its directory.
local function plugin_dir()
  local f = io.open("/proc/self/cmdline", "rb")
  if f then
    local raw = f:read("*a") or ""
    f:close()
    for a in raw:gmatch("[^\0]+") do
      local path = a:match("(%S*assess%.lua)")
      if path then return path:match("^(.*)[/\\][^/\\]+$") or "." end
    end
  end
  return "plugins"
end

local function collect_users(ctx)
  local names, seen = {}, {}
  local function add(raw)
    for name in tostring(raw):gmatch("[^%s,]+") do
      if not seen[name] then
        seen[name] = true
        names[#names + 1] = name
      end
    end
  end
  local function each(v)
    if v == nil then return {} end
    return type(v) == "table" and v or { v }
  end
  for _, v in ipairs(each(ctx.args:get("users"))) do add(v) end
  for _, v in ipairs(each(ctx.args:get("user"))) do add(v) end
  return names
end

-- Append "key=value" to a plugin-spec builder for each forwarded arg that is
-- present, skipping absent and explicitly-empty ones.
local function forward(spec, ctx, keys)
  for _, k in ipairs(keys) do
    local v = ctx.args:get(k)
    if v ~= nil and v ~= "" then
      local vs = type(v) == "table" and v or { v }
      for _, one in ipairs(vs) do spec[#spec + 1] = k .. "=" .. tostring(one) end
    end
  end
end

local function fint(n)
  return string.format("%.0f", n)
end

function plugin.init(ctx)
  if ctx.worker ~= 1 then return end

  local site = ctx.args:get("site", "lichess"):gsub("%.", ""):lower()
  if site ~= "lichess" and site ~= "chesscom" then
    error("assess: unknown site '" .. site .. "' (want lichess or chesscom)")
  end

  local users = collect_users(ctx)
  if #users == 0 then error("assess: no usernames; pass users= and/or user=") end

  local bin = ctx.args:get("bin") or running_binary()
  if not bin then
    error("assess: cannot locate the tictac binary; pass bin=/path/to/tictac")
  end
  local dir = plugin_dir()
  local fetch_lua = dir .. "/fetch.lua"
  local cheat_lua = dir .. "/cheat.lua"

  local outdir = ctx.args:get("outdir", "./")
  if outdir:sub(1, 1) == "~" then outdir = (os.getenv("HOME") or "~") .. outdir:sub(2) end
  if outdir:sub(-1) ~= "/" then outdir = outdir .. "/" end
  os.execute("mkdir -p " .. shsq(outdir))

  local n = fint(math.max(0, ctx.args:number("n", 10)))
  local jobs = fint(ctx.args:number("jobs", 0))
  local engines = ctx.args:number("engines")

  for _, user in ipairs(users) do
    local pgn = outdir .. user .. ".pgn"
    local report = outdir .. user .. ".txt"

    -- Stage 1: download this user's games via fetch.lua.
    local fspec = { fetch_lua, "site=" .. site, "user=" .. user, "out=" .. pgn }
    local max = ctx.args:number("max")
    if max then fspec[#fspec + 1] = "max=" .. fint(max) end
    if site == "lichess" then forward(fspec, ctx, { "token", "perf", "rated", "color" }) end
    local fcmd = table.concat({
      shsq(bin), "--file", "/dev/null", "--no-output",
      "--plugin", shsq(table.concat(fspec, " ")),
    }, " ")
    ctx.log.info("%s: downloading (%s)...", user, site)
    if not os.execute(fcmd) then
      ctx.log.warn("%s: fetch failed; skipping", user)
      goto continue
    end

    do
      local f = io.open(pgn, "r")
      local empty = (not f) or (f:read(1) == nil)
      if f then f:close() end
      if empty then
        os.remove(pgn)
        ctx.log.warn("%s: no games downloaded; skipping analysis", user)
        goto continue
      end
    end

    -- Stage 2: screen the downloaded games with cheat.lua.
    local cspec = {
      cheat_lua, "player=" .. player_pattern(user),
      "suspects=" .. n, "report=" .. report,
    }
    forward(cspec, ctx, {
      "engine", "depth", "movetime", "multipv", "book", "maxply",
      "maia", "maia_dir", "maia_weights", "maia_backend",
    })
    local ccmd = { shsq(bin), "--file", shsq(pgn), "--no-output", "-j", jobs }
    if engines then ccmd[#ccmd + 1] = "--engines"; ccmd[#ccmd + 1] = fint(engines) end
    ccmd[#ccmd + 1] = "--plugin"
    ccmd[#ccmd + 1] = shsq(table.concat(cspec, " "))
    ctx.log.info("%s: analysing...", user)
    if not os.execute(table.concat(ccmd, " ")) then
      ctx.log.warn("%s: analysis failed", user)
      goto continue
    end

    do
      local rf = io.open(report, "r")
      if rf then
        local verdict
        for line in rf:lines() do
          if line:match("^Verdict:") then verdict = line; break end
        end
        rf:close()
        ctx.log.info("%s: %s  (report: %s)", user, verdict or "no verdict", report)
      end
    end

    ::continue::
  end
end

function plugin.process(input, ctx)
  return input
end

return plugin

-- vim:ft=lua:ts=2:sw=2:
