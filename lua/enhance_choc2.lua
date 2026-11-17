-- enhance_choc2.lua
--
-- The enhancement overlay for Chocolatier 2 (docs/design/enhancements.md section 5).
-- Assigns Enhance.onWheel / Enhance.beforeCommand, Enhance.smoothMoney and locals only; never a game global.

-- The ledger's money counts in 20 steps of 50 ms (as Chocolatier 3's) where the original takes 7 steps of 100 ms.
Enhance.smoothMoney = true

-- The number pad (ui/buysell.lua), wherever the pointer is on the dialog: the wheel adds the notches to the field the pad names in gNumberPadTarget, then the
-- pad's own callback applies its clamp and recomputes the subtotal.
local function padWheel(notches)
	local target = gNumberPadTarget
	local callback = gNumberPadCallback
	if type(target) ~= "string" or type(callback) ~= "function" or type(GetLabel) ~= "function" or type(SetLabel) ~= "function" then
		return nil
	end
	if not (EnhanceWindowExists and EnhanceWindowExists(target)) then
		return nil
	end
	local count = Enhance.clamp(Enhance.number(GetLabel(target)) + notches, 0, 9999999)
	SetLabel(target, string.format("%d", count))
	callback()
	return true
end

-- The wheel over the ledger's messages or inventory tab presses the tab's own arrow (seven lines or a page, clamped
-- by the game); an arrow that is hidden or missing is a quiet no-op. The factories tab has no arrows.
function Enhance.onWheel(notches, names)
	if padWheel(notches) then
		return true
	end
	if Enhance.has(names, "ledger") and (Enhance.has(names, "messages") or Enhance.has(names, "inventory")) then
		if notches > 0 then
			return "scroll_up"
		end
		return "scroll_down"
	end
	return nil
end
