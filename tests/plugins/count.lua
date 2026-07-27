-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- count.lua -- count the values received from the previous plugin and assert the
-- total equals `expect=` at finish. Used to observe how upstream returns (pass,
-- drop, stop, fan-out) shape the stream.
--
-- Only meaningful at -j1: with more workers each gets its own ctx.scope, so the
-- total is split across them and finish() asserts once per worker.

local plugin = {}

function plugin.init(ctx)
  ctx.scope.received = 0
end

function plugin.process(input, ctx)
  ctx.scope.received = ctx.scope.received + 1
  return input
end

function plugin.finish(ctx)
  local expect = ctx.args:number("expect")
  assert(expect ~= nil, "count.lua requires expect=")
  assert(ctx.scope.received == expect,
    string.format("count: expected %d, received %d", expect, ctx.scope.received))
end

return plugin
