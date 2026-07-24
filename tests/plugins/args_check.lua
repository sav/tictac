-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- args_check.lua -- pin down the ctx.args accessors against a fixed argument
-- spec (see the args_accessors case in tests/CMakeLists.txt). Every assertion
-- here is about the accessors themselves, so the plugin ignores the games
-- entirely and runs its checks from init().

local plugin = {}

local function eq_array(v, expect)
  if type(v) ~= "table" then return false end
  if #v ~= #expect then return false end
  for i = 1, #expect do
    if v[i] ~= expect[i] then return false end
  end
  return true
end

function plugin.init(ctx)
  local a = ctx.args

  -- A single value is returned bare; a repeated key yields an array in
  -- command-line order.
  assert(a:get("one") == "1", "get: single value must not be wrapped in a table")
  assert(eq_array(a:get("many"), { "a", "b" }), "get: repeated key must yield an array")
  assert(eq_array(a:number("nums"), { 1, 2 }), "number: repeated key must yield an array")

  -- A missing key returns the default, or nil when none was given.
  assert(a:get("absent") == nil, "get: absent key must be nil")
  assert(a:get("absent", "def") == "def", "get: default must cover an absent key")
  assert(a:number("absent") == nil, "number: absent key must be nil")
  assert(a:number("absent", 7) == 7, "number: default must cover an absent key")

  -- `key=` counts as absent everywhere except bool, where it is an explicit
  -- false, so a caller can disable a flag without dropping it from the spec.
  assert(a:get("empty") == nil, "get: 'empty=' must read as absent")
  assert(a:get("empty", "def") == "def", "get: 'empty=' must fall back to the default")
  assert(a:number("empty", 7) == 7, "number: 'empty=' must fall back to the default")
  assert(a:list("empty") == nil, "list: 'empty=' must read as absent")
  assert(a:bool("empty") == false, "bool: 'empty=' must be an explicit false")

  -- bool's accepted spellings; a bare flag means true.
  assert(a:bool("flag") == true, "bool: a bare key must be true")
  assert(a:bool("yes") == true, "bool: 'yes' must be true")
  assert(a:bool("off") == false, "bool: 'off' must be false")
  assert(a:bool("flag") == a:bool("yes"), "bool: a bare key must match an explicit true")
  assert(a:bool("absent") == nil, "bool: absent key must be nil")
  assert(a:bool("absent", true) == true, "bool: default must cover an absent key")

  -- A malformed value is not covered by the default: it reports and returns nil.
  assert(a:number("junk") == nil, "number: trailing junk must be rejected")
  assert(a:number("junk", 7) == nil, "number: default must not mask a bad value")
  assert(a:number("word") == nil, "number: a non-numeric value must be rejected")
  assert(a:bool("maybe") == nil, "bool: an unrecognised value must be rejected")

  -- number accepts the forms from_chars does, including a negative and a float.
  assert(a:number("neg") == -3, "number: must accept a negative value")
  assert(a:number("frac") == 1.5, "number: must accept a fractional value")

  -- list splits each present value on commas and always returns an array.
  assert(eq_array(a:list("tags"), { "a", "b", "c" }), "list: must split on commas")
  assert(eq_array(a:list("one"), { "1" }), "list: a single value must still be an array")
  assert(a:list("absent") == nil, "list: absent key must be nil")

  -- require returns like get, but raises when the key is absent.
  assert(a:require("one") == "1", "require: must return the value when present")
  assert(not pcall(function() return a:require("absent") end),
    "require: an absent key must raise")
  assert(not pcall(function() return a:require("empty") end),
    "require: 'empty=' must raise like an absent key")

  -- each yields every key=value pair in command-line order, keeping repeats and
  -- empties: a bare flag reads as "true", and "empty=" as an empty string.
  local expect = {
    { key = "one",   value = "1" },
    { key = "many",  value = "a" },
    { key = "many",  value = "b" },
    { key = "nums",  value = "1" },
    { key = "nums",  value = "2" },
    { key = "empty", value = "" },
    { key = "flag",  value = "true" },
    { key = "yes",   value = "yes" },
    { key = "off",   value = "off" },
    { key = "tags",  value = "a,b,c" },
    { key = "junk",  value = "20abc" },
    { key = "word",  value = "abc" },
    { key = "maybe", value = "perhaps" },
    { key = "neg",   value = "-3" },
    { key = "frac",  value = "1.5" },
  }
  local got = a:each()
  assert(#got == #expect, "each: length " .. tostring(#got) .. ", want " .. tostring(#expect))
  for i, want in ipairs(expect) do
    assert(got[i].key == want.key,
      "each: entry " .. i .. " key '" .. tostring(got[i].key) .. "', want '" .. want.key .. "'")
    assert(got[i].value == want.value,
      "each: entry " .. i .. " value '" .. tostring(got[i].value) .. "', want '" .. want.value .. "'")
  end
end

function plugin.process(input)
  return input
end

return plugin
