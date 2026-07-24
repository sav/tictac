-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- api_smoke.lua -- exercise the Game/Board/Move Lua API on a fixed game
-- (Scholar's mate). Every assertion checks a known value, so a failure means a
-- binding regressed. Run against fixtures/scholars.pgn.

local plugin = {}

function plugin.process(input, ctx)
  local g = input.game

  -- Game
  assert(g:result() == "1-0", "result: " .. tostring(g:result()))
  assert(g:header("White") == "Morphy", "header White: " .. tostring(g:header("White")))
  assert(g:moveCount() == 7, "moveCount: " .. tostring(g:moveCount()))

  -- headers() returns the whole tag set as a table, keyed by tag name.
  local h = g:headers()
  assert(h.White == "Morphy", "headers White: " .. tostring(h.White))
  assert(h.Black == "NN", "headers Black: " .. tostring(h.Black))
  assert(h.Event == "Scholar's Mate", "headers Event: " .. tostring(h.Event))
  assert(h.Result == "1-0", "headers Result: " .. tostring(h.Result))
  local n = 0
  for _ in pairs(h) do n = n + 1 end
  assert(n == 7, "headers count: " .. tostring(n))

  -- Board (initial position via startBoard)
  local start = g:startBoard()
  assert(start:sideToMove() == "white", "start sideToMove")
  assert(#start:legalMoves() == 20, "start legal moves: " .. tostring(#start:legalMoves()))
  assert(start:piece("e1") == "K", "e1 should be white king")
  assert(start:piece("e4") == nil, "e4 should be empty")

  -- makeMove is immutable: it returns a new board, leaving the original intact.
  local after = start:makeMove("e4")
  assert(after:fen() ~= start:fen(), "makeMove must not mutate in place")
  assert(start:sideToMove() == "white", "start board must be untouched")
  assert(after:sideToMove() == "black", "after e4 it is Black to move")

  -- Move
  local moves = g:moves()
  assert(#moves == 7, "moves length: " .. tostring(#moves))
  assert(moves[1]:san() == "e4", "move 1 san: " .. tostring(moves[1]:san()))
  assert(moves[1]:uci() == "e2e4", "move 1 uci: " .. tostring(moves[1]:uci()))
  assert(moves[1]:from() == "e2" and moves[1]:to() == "e4", "move 1 from/to")

  -- The final position (the default board cursor) is checkmate.
  assert(input.board:isCheckmate(), "final position must be checkmate")

  return input
end

return plugin
