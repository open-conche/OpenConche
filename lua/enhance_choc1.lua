-- enhance_choc1.lua
--
-- The enhancement overlay for Chocolatier 1 (docs/design/enhancements.md section 5).
-- Assigns Enhance.onWheel / Enhance.beforeCommand and locals only; it never replaces or wraps a game function. The one game
-- global it writes is the ledger's scroll position, gCurrentLedgerTop, which the game's own arrows also just add to or subtract from.

local MAX_QUANTITY = 9999

-- A global of the game that is a function / a table, or nil: the overlay only reads them.
local function gfn(name)
	local f = rawget(_G, name)
	if type(f) == "function" then
		return f
	end
	return nil
end

local function gtable(name)
	local t = rawget(_G, name)
	if type(t) == "table" then
		return t
	end
	return nil
end

-- ---- ledger --------------------------------------------------------------------------------------------------

-- One scroll step: -1 up, +1 down. The game's own arrows set the bounds: the up arrow is built only when
-- gCurrentLedgerTop > 1 and the down arrow only when there is more below.
local function ledgerStep(direction)
	local arrow = "control/scrollup_up"
	if direction > 0 then
		arrow = "control/scrolldown_up"
	end
	if not EnhanceHasButtonGraphic(arrow) then
		return false
	end
	gCurrentLedgerTop = gCurrentLedgerTop + direction
	UpdateDynamicWindow("ledgertabs")
	return true
end

local function wheelLedger(notches)
	if type(gCurrentLedgerTop) ~= "number" or not gfn("UpdateDynamicWindow") or not gfn("EnhanceHasButtonGraphic") then
		return nil
	end
	local direction = -1
	local count = notches
	if notches < 0 then
		direction = 1
		count = -notches
	end
	for _ = 1, count do
		if not ledgerStep(direction) then
			break
		end
	end
	return true
end

-- ---- trade screen --------------------------------------------------------------------------------------------

-- The trade screen's items, kind ("buy" or "sell") and character. The game keeps them in gDialogTable, which any
-- dialog opened from the screen (the "not enough cash" notice, the haggle) overwrites; the last whole table seen is kept
-- for as long as the screen's fields exist. Every press and wheel on the screen runs through here first, so the table is
-- seen before such a dialog can replace it.
local lastTrade = nil

local function tradeContext()
	if not EnhanceWindowExists("receipt") then
		return nil
	end
	local d = gtable("gDialogTable")
	if d and type(d.items) == "table" and (d.type == "buy" or d.type == "sell") then
		lastTrade = { items = d.items, kind = d.type, character = d.character }
	end
	local t = lastTrade
	if t then
		local first = t.items[1]
		if type(first) ~= "table" or type(first.name) ~= "string" or not EnhanceWindowExists(first.name) then
			return nil
		end
	end
	return t
end

-- The pending quantity typed or wheeled into an item's field; 0 for an idle label.
local function pendingOf(item)
	local ok, label = pcall(GetLabel, item.name)
	if not ok then
		return 0
	end
	return Enhance.number(label)
end

local function anyPending(items)
	for _, item in ipairs(items) do
		if pendingOf(item) > 0 then
			return true
		end
	end
	return false
end

-- The item whose row is under the pointer: its field, its price tag or (a platter row) its platter is among the window
-- names. The icon of a sack row has no name, so over that alone the wheel does nothing.
local function rowItem(items, names)
	for _, item in ipairs(items) do
		if type(item) == "table" and type(item.name) == "string" then
			if Enhance.has(names, item.name) or Enhance.has(names, item.name .. "P") or Enhance.has(names, item.name .. "_platter") then
				return item
			end
		end
	end
	return nil
end

local function wheelRow(notches, names)
	if not (gfn("GetLabel") and gfn("SetLabel") and gfn("GetString") and gfn("UpdateDynamicWindow")) then
		return nil
	end
	local trade = tradeContext()
	if not trade then
		return nil
	end
	local items, kind = trade.items, trade.kind
	local item = rowItem(items, names)
	if not item then
		return nil
	end
	local limit = MAX_QUANTITY
	local owned = 0
	if kind == "sell" then
		local sim = gtable("gSim")
		if not sim or type(sim.inventory) ~= "table" then
			return nil
		end
		owned = sim.inventory[item.name] or 0
		limit = Enhance.clamp(owned, 0, MAX_QUANTITY)
	end
	local quantity = Enhance.clamp(pendingOf(item) + notches, 0, limit)
	local label
	if quantity > 0 then
		label = tostring(quantity)
	elseif rawget(_G, "gNumberPadTarget") == item.name then
		-- The pad is typing into this field: an emptied field is blank, as after Backspace, so the next digit starts it.
		label = ""
	elseif kind == "sell" and owned <= 0 then
		label = "-"
	else
		label = GetString(kind)
	end
	SetLabel(item.name, label)
	UpdateDynamicWindow("receipt")
	return true
end

-- The game's own commit for the pending quantities: what the number pad's OK does.
local function commitPending(trade)
	local hide = gfn("HideNumberPad")
	if hide then
		hide()
	end
	if trade.kind == "buy" then
		local market = gtable("LMarket")
		if market and type(market.Buy) == "function" then
			market.Buy()
		end
	else
		local shop = gtable("LShop")
		if shop and type(shop.Sell) == "function" then
			shop.Sell(trade.character)
		end
	end
end

-- The trade screen's OK button: the unnamed close button whose idle graphic is the game's yes button.
local function isTradeOk(closes)
	if not closes or not gfn("EnhanceButtonGraphic") then
		return false
	end
	local yes = gtable("yesButtonGraphics")
	local graphic = EnhanceButtonGraphic()
	if not yes or not graphic or graphic ~= yes[1] then
		return false
	end
	return true
end

local function closeTrade(name, original, trade)
	local items, kind = trade.items, trade.kind
	return function()
		if anyPending(items) then
			-- The number pad's OK button plays this on every press, refused or not.
			local play = gfn("PlaySound")
			if play then
				play(kind)
			end
			commitPending(trade)
			if kind == "buy" and anyPending(items) then
				-- The purchase was refused (the game showed its notice and kept the quantities): stay on the screen.
				-- Only a purchase can be refused: a sale leaves a quantity alone only on a row with nothing to sell.
				return
			end
		end
		if original then
			original()
		end
		Enhance.close(name)
	end
end

-- The trade screen's quantity fields are made with length=4, so the original holds three typed digits while the number pad
-- reaches four (and the wheel stops at MAX_QUANTITY). Numbers-only fields hold `length` digits.
function Enhance.textLimit(name, length, ignore)
	local numbersOnly = rawget(_G, "kNumbersOnly")
	if type(numbersOnly) == "string" and ignore == numbersOnly and length == 4 then
		return length
	end
	return nil
end

function Enhance.onWheel(notches, names)
	if type(notches) ~= "number" or notches == 0 or type(names) ~= "table" then
		return nil
	end
	if Enhance.has(names, "ledgertabs") then
		return wheelLedger(notches)
	end
	return wheelRow(notches, names)
end

function Enhance.beforeCommand(name, original, closes)
	local trade = tradeContext()  -- also remembers the screen's table before this press can replace it
	if not trade or not isTradeOk(closes) or not gfn("GetLabel") then
		return nil
	end
	return closeTrade(name, original, trade)
end
