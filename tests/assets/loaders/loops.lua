-- The game's Lua has `continue`: jump to the end of the loop body.
local r = {}
for i = 1, 5 do
	if i == 2 or i == 4 then continue end
	table.insert(r, i)
end
local j = 0
while j < 6 do
	j = j + 1
	if j == 3 then continue end
	table.insert(r, 10 + j)
end
local k = 0
repeat
	k = k + 1
	if k == 2 then continue end
	table.insert(r, 20 + k)
until k >= 3
for _, v in ipairs({ 1, 2, 3 }) do
	if v == 2 then continue end
	table.insert(r, 30 + v)
end
-- a continue that must close an upvalue captured in the body
local fns = {}
for i = 1, 3 do
	local c = i * 10
	fns[i] = function() return c end
	if i == 2 then continue end
end
table.insert(r, fns[1]() + fns[2]() + fns[3]())
return table.concat(r, ",")
