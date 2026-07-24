-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- filter.lua -- keep games whose headers match the given patterns.
--   Every "key=pattern" arg is matched against the same-named PGN header; the
--   header name is looked up case-insensitively. A few keys are special:
--     player=<pat>   match the pattern against either the White or Black header
--     min_elo=<n>    keep only games with WhiteElo >= n
--   --plugin "filter.lua white=^Fischer event=London min_elo=2700"

local plugin = {
  meta = {
    name = "filter",
    -- Declared for --help/validation; the runtime does not consume args yet.
    -- Beyond the keys below, any key names a PGN header to match a pattern on.
    args = {
      player  = { type = "string", help = "Pattern matched against either player." },
      min_elo = { type = "number", default = 0, help = "Minimum WhiteElo to keep." },
    },
  },
}

-- Keys with special meaning, not treated as header names.
local RESERVED = { player = true, min_elo = true }

-- Case-insensitive header lookup: PGN tag names are conventionally TitleCase,
-- but we accept any casing on the command line.
local function header(g, name)
  local want = name:lower()
  for k, v in pairs(g:headers()) do
    if k:lower() == want then return v end
  end
  return nil
end

function plugin.process(input, ctx)
  local g = input.game

  local function matches(value, pattern)
    if pattern == nil or pattern == "" then return true end
    return (value or ""):match(pattern) ~= nil
  end

  -- Any non-reserved key names a header to match the pattern against.
  for _, arg in ipairs(ctx.args:each()) do
    if not RESERVED[arg.key:lower()] and arg.value ~= "" then
      if not matches(header(g, arg.key), arg.value) then return false end
    end
  end

  local player_re = ctx.args:get("player")
  if player_re and not (matches(header(g, "White"), player_re) or matches(header(g, "Black"), player_re)) then
    return false
  end

  local min = ctx.args:number("min_elo", 0)
  if min > 0 and tonumber(header(g, "WhiteElo") or "0") < min then
    return false
  end

  return input
end

return plugin
