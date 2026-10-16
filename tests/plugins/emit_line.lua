-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- emit_line.lua -- write one tagged line per game to a ctx.open path. Two
-- instances pointed at the same file must both land in it: reopening a path
-- hands back the writer already open on it rather than truncating it.

local plugin = { meta = { name = "emit_line" } }

function plugin.init(ctx)
  ctx.scope.w = ctx.open(ctx.args:get("out", "emit_line.txt"))
  ctx.scope.tag = ctx.args:get("tag", "a")
end

function plugin.process(input, ctx)
  ctx.scope.w:writef("%s %s\n", ctx.scope.tag, input.game:header("White") or "?")
  return input
end

return plugin
