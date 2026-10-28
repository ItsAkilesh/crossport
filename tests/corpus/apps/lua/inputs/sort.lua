-- File: tests/corpus/apps/lua/inputs/sort.lua
-- Purpose: Sort a deterministic pseudo-random sequence several ways and print checksums
-- Author: Akilesh
-- Date: 2026-10-28

local seed = 12345
local function next_number()
  seed = (seed * 1103515245 + 12345) % 2147483648
  return seed
end
local values = {}
for i = 1, 5000 do values[i] = next_number() % 1000 end
table.sort(values)
print(values[1], values[2500], values[5000])
local words = {}
for i = 1, 200 do words[i] = string.format("w%03d", next_number() % 500) end
table.sort(words, function(a, b) return a > b end)
print(table.concat(words, ",", 1, 10))
local counts = {}
for _, w in ipairs(words) do counts[w] = (counts[w] or 0) + 1 end
local keys = {}
for k in pairs(counts) do keys[#keys + 1] = k end
table.sort(keys)
print(#keys, keys[1], counts[keys[1]])
