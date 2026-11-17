-- enhance.lua
--
-- The shared part of the enhancement overlay (docs/design/enhancements.md). Loaded only when the `enhancements`
-- switch is on, right after host.lua. Ours: it never assigns a game global and never wraps one.
--
-- The engine calls Enhance.onWheel(notches, names) for a mouse wheel turn and Enhance.beforeCommand(name, original,
-- closes) when a button with a command or `close` is pressed. Neither may yield. A game's file
-- (enhance_<game id>.lua) assigns them.

Enhance = {}

-- Wheel over the windows named `names` (front to back), `notches` positive up.
-- nil/false: not handled; true: handled; a string: press the button of that name once per notch.
function Enhance.onWheel(notches, names)
	return nil
end

-- A button with a command or `close` was pressed. nil leaves the press as it is; a function replaces it and is
-- delivered like the command (it may open dialogs, call original() and Enhance.close(name)).
function Enhance.beforeCommand(name, original, closes)
	return nil
end

-- A typed-into TextEdit asks how many characters it holds: `length` and `ignore` are the field's own style values. A
-- number of 1 or more replaces the original `length - 1`; anything else keeps it.
function Enhance.textLimit(name, length, ignore)
	return nil
end

-- Whether `name` is in the array `names`.
function Enhance.has(names, name)
	if type(names) ~= "table" then
		return false
	end
	for _, n in ipairs(names) do
		if n == name then
			return true
		end
	end
	return false
end

-- tonumber(label), or 0 when it is not a number.
function Enhance.number(label)
	return tonumber(label) or 0
end

-- v limited to lo..hi.
function Enhance.clamp(v, lo, hi)
	if v < lo then
		return lo
	end
	if v > hi then
		return hi
	end
	return v
end

-- What a `close` button's press does (EnhanceClose).
function Enhance.close(name)
	return EnhanceClose(name)
end
