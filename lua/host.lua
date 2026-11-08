-- host.lua
--
-- Engine-side additions loaded right after the prelude (script-host design section 4, step 5).
-- Everything here is ours; the prelude itself is loaded unchanged.

-- DumpTable(t): logs a table's contents through DebugOut, nested tables indented, keys in a stable order.
function DumpTable(t, indent)
	indent = indent or ""
	if type(t) ~= "table" then
		DebugOut(indent .. tostring(t))
		return
	end
	local keys = {}
	for k in pairs(t) do
		table.insert(keys, k)
	end
	table.sort(keys, function(a, b)
		local ta, tb = type(a), type(b)
		if ta ~= tb then
			return ta < tb
		end
		if ta == "number" or ta == "string" then
			return a < b
		end
		return false
	end)
	for _, k in ipairs(keys) do
		local v = t[k]
		if type(v) == "table" then
			DebugOut(indent .. tostring(k) .. " = {")
			DumpTable(v, indent .. "  ")
			DebugOut(indent .. "}")
		else
			DebugOut(indent .. tostring(k) .. " = " .. tostring(v))
		end
	end
end

-- The one deliberate change to the prelude: movie splashes (the three logos) are skipped. A splash with only a
-- graphic, such as DisplaySplash("", "image/title_background", 1), still runs.
local PreludeDisplaySplash = DisplaySplash
function DisplaySplash(splashMovie, splashGraphic, time)
	if splashMovie ~= nil and splashMovie ~= "" then
		return
	end
	return PreludeDisplaySplash(splashMovie, splashGraphic, time)
end
