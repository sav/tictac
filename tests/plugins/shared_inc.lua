-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- shared_inc.lua -- increment a global counter in ctx.shared on every game. Two
-- instances in one pipeline write to the same table, so over N games the total
-- is 2*N, proving ctx.shared is shared across plugins and games. Asserts the
-- total equals `expect=` at finish when the argument is given.

local plugin = {}

function plugin.process(input, ctx)
  ctx.shared.n = (ctx.shared.n or 0) + 1
  return input
end

function plugin.finish(ctx)
  local expect = ctx.args:number("expect")
  if expect ~= nil then
    assert(ctx.shared.n == expect,
      string.format("shared: expected %d, got %d", expect, ctx.shared.n))
  end
end

return plugin
