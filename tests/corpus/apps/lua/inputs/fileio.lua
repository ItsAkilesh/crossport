-- File: tests/corpus/apps/lua/inputs/fileio.lua
-- Purpose: Write, append, seek and read back files, and check error reporting for a missing one
-- Author: Akilesh
-- Date: 2026-10-28

local f = assert(io.open("out.txt", "w"))
f:write("line one\n", "line two\n", 3, " ", 4.5, "\n")
f:close()
f = assert(io.open("out.txt", "a"))
f:write("appended\n")
f:close()
for line in io.lines("out.txt") do print("[" .. line .. "]") end
f = assert(io.open("out.txt", "rb"))
print(f:seek("end"), f:seek("set", 5), f:read(3), f:read("n"), f:read("l"))
f:close()
print(io.open("does-not-exist.txt"))
print(pcall(io.lines, "does-not-exist.txt"))
local big = assert(io.open("big.bin", "wb"))
for i = 1, 5000 do big:write(string.char(i % 256)) end
big:close()
print(#assert(io.open("big.bin", "rb")):read("a"))
os.remove("out.txt")
print(io.open("out.txt"))
