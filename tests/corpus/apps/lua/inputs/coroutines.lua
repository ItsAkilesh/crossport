-- File: tests/corpus/apps/lua/inputs/coroutines.lua
-- Purpose: Coroutines, closures and pcall, which stress setjmp-based error handling
-- Author: Akilesh
-- Date: 2026-10-28

local function generator(n)
  return coroutine.wrap(function() for i = 1, n do coroutine.yield(i * i) end end)
end
local total = 0
for v in generator(10) do total = total + v end
print("sum of squares", total)
local raised, payload = pcall(function() error({code = 42}) end)
print(raised, payload.code)
print(select(2, pcall(function() error("plain", 0) end)))
print(select(2, pcall(function() local x = nil; return x.field end)))
local ok, err = pcall(string.rep, "x", -1)
print(ok, err == "")
local co = coroutine.create(function(a) local b = coroutine.yield(a + 1); error("inside " .. b) end)
print(coroutine.resume(co, 1))
print(coroutine.resume(co, "co"))
print(coroutine.status(co))
