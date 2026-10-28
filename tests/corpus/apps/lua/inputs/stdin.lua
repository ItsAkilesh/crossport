-- File: tests/corpus/apps/lua/inputs/stdin.lua
-- Purpose: Read standard input as lines, numbers and the rest
-- Author: Akilesh
-- Date: 2026-10-28

local first = io.read("l")
local a, b = io.read("n", "n")
io.read("l")
local count, longest = 0, 0
for line in io.lines() do
  count = count + 1
  if #line > longest then longest = #line end
end
print(first, a + b, count, longest)
print(io.read("a") == "", io.read("l"))
