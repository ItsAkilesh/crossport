print("program read from stdin", 6 * 7)
local t = {}
for i = 1, 3 do t[#t + 1] = i * 2 end
print(table.concat(t, " "))
