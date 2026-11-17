-- enhance_choc3.lua
--
-- The enhancement overlay for Chocolatier 3 (and the Reforged variant, which has the same module id) (docs/design/enhancements.md section 5).
-- Assigns Enhance.onWheel / Enhance.beforeCommand and locals only; never a game global.

local kMaxCount = 9999999

-- The register popup's quantity: the wheel adds the notches to `count` (never below 0) the way typing changes it, so
-- the game's own Update clamps to what can be bought or sold and refreshes the subtotal.
local function registerWheel(notches)
	if type(GetLabel) ~= "function" or type(EnhanceSetText) ~= "function" then
		return nil
	end
	if not (EnhanceWindowExists and EnhanceWindowExists("count")) then
		return nil
	end
	local count = Enhance.clamp(Enhance.number(GetLabel("count")) + notches, 0, kMaxCount)
	return EnhanceSetText("count", string.format("%d", count))
end

function Enhance.onWheel(notches, names)
	if Enhance.has(names, "ui_register") then
		return registerWheel(notches)
	end
	if Enhance.has(names, "questlog") then
		if notches > 0 then
			return "scrollUp"
		end
		return "scrollDown"
	end
	return nil
end
