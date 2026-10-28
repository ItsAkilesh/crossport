-- File: tests/corpus/apps/lua/inputs/strings.lua
-- Purpose: Exercise string.format, patterns and the string library
-- Author: Akilesh
-- Date: 2026-10-28

print(string.format("%5.2f|%-8s|%x|%X|%o|%e|%g|%c", 3.14159, "ab", 255, 255, 8, 12345.6789, 0.0001, 65))
print(string.format("%q", 'he said "hi"\n\0end'))
print(string.format("%10.4s|%+d|% d|%05d|%.3g|%a", "truncate", 5, 5, 42, 1234567.0, 1.0))
print(("hello world"):gsub("o", "0"))
print(("key=value; other=thing"):match("(%w+)=(%w+)"))
for k, v in string.gmatch("a=1, b=2, c=3", "(%w+)=(%w+)") do io.write(k, ":", v, " ") end
print()
print(("abc"):rep(3, "-"), ("abc"):upper(), ("ABC"):lower(), ("abc"):reverse(), #("abc"):rep(1000))
print(("x"):byte(), string.char(72, 105), ("hello"):sub(2, -2), ("hello"):find("l+"))
print(string.format("%s %s %s", nil, true, {} ~= nil))
print(tostring(1e15), tostring(123456789012345678), 2^53, 1e300 * 1e10, -(0/0) ~= -(0/0))
print(string.pack("<i4", 100):byte(1, -1))
