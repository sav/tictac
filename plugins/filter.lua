-- filter.lua — keep games matching header criteria.
--   --plugin "filter.lua white=^Fischer min_elo=2700"
local plugin = { meta = { name = "filter" } }

function plugin.process(input, ctx)
  local g = input.game
  local white_re = ctx.args:get("white")
  if white_re and not (g:header("White") or ""):match(white_re) then
    return false
  end
  local black_re = ctx.args:get("black")
  if black_re and not (g:header("Black") or ""):match(black_re) then
    return false
  end
  local min = ctx.args:number("min_elo", 0)
  if min > 0 and tonumber(g:header("WhiteElo") or "0") < min then
    return false
  end
  return input
end

return plugin
