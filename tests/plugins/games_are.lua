-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- games_are.lua -- assert exactly which games reached the pipeline, by White
-- header, in order: `expect=Alice,Erin`. A plain count cannot tell "the right
-- game was dropped" from "a different one was", which is what the parse-error
-- recovery tests need to pin down. expect= may be empty, meaning no games.

local plugin = {}

function plugin.init(ctx)
  ctx.scope.seen = {}
end

function plugin.process(input, ctx)
  table.insert(ctx.scope.seen, input.game:header("White") or "?")
  return input
end

function plugin.finish(ctx)
  local expect = ctx.args:get("expect", "")
  local want = {}
  for name in tostring(expect):gmatch("[^,]+") do
    table.insert(want, name)
  end
  local got = ctx.scope.seen
  local gotStr = table.concat(got, ",")
  local wantStr = table.concat(want, ",")
  assert(gotStr == wantStr,
    string.format("games: expected [%s], got [%s]", wantStr, gotStr))
end

return plugin
