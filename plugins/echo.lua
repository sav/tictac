-- echo.lua — pass games through unchanged; demo of the lifecycle.
local plugin = {}

plugin.meta = {
  name        = "echo",
  version     = "1.0.0",
  description = "Pass games through unchanged; demo of the lifecycle.",
  args = {
    verbose = { type = "bool", default = false, help = "Log every game." },
  },
}

function plugin.init(ctx)
  ctx.state.count = 0
end

function plugin.process(input, ctx)
  ctx.state.count = ctx.state.count + 1
  if ctx.args:bool("verbose", false) then
    ctx.log.info("game %d: %s vs %s", ctx.index,
      input.game:header("White") or "?", input.game:header("Black") or "?")
  end
  return input
end

function plugin.finish(ctx)
  ctx.log.info("echoed %d game(s)", ctx.state.count)
end

return plugin
