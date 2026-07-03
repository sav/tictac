-- filter.lua — keep games matching header criteria.
--   --plugin "filter.lua player=^Fischer min_elo=2700"
local plugin = {
  meta = {
    name = "filter",
    -- Declared for --help/validation; the runtime does not consume args yet.
    args = {
      white   = { type = "string", help = "Pattern matched against the White header." },
      black   = { type = "string", help = "Pattern matched against the Black header." },
      player  = { type = "string", help = "Pattern matched against either player." },
      min_elo = { type = "number", default = 0, help = "Minimum WhiteElo to keep." },
    },
  },
}

function plugin.process(input, ctx)
  local g = input.game
  local function matches(header, re)
    return not re or (g:header(header) or ""):match(re)
  end

  if not matches("White", ctx.args:get("white")) then return false end
  if not matches("Black", ctx.args:get("black")) then return false end

  local player_re = ctx.args:get("player")
  if player_re and not (matches("White", player_re) or matches("Black", player_re)) then
    return false
  end

  local min = ctx.args:number("min_elo", 0)
  if min > 0 and tonumber(g:header("WhiteElo") or "0") < min then
    return false
  end
  return input
end

return plugin
