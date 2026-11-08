-- prelude.lua
--
-- Reconstruction of the window prelude that the original Chocolatier 3 engine
-- embeds as precompiled Lua 5.0 bytecode (chunk name "@pflib/src/core/ScreenInit.lua",
-- Mac binary file offset 0x1da7e0, 0x3aa2 bytes). Reconstructed from the
-- disassembly in re/mac/lua/ScreenInit.lst (workbench/re/lua50_chunks.py); see
-- docs/re/embedded-lua.md.
--
-- Rules followed: function names, parameter names, local names, constants and
-- control flow are the original's (the chunk keeps its debug info). Globals
-- that the original assigns without `local` stay globals (inMovie, d, divisor,
-- message, gYieldResult, typename, gLastMessage ...); that is deliberate.
-- Only the base, string and table libraries are available: no `math`.
--
-- The functions that are NOT defined here are natives registered by the engine
-- (PushModal, PopWindow, GenericCreate, PostCreate, SetWindowName, RealSetFocus,
-- DoFitToChildren, DoSelectLayer, HandleGetMessage, PostMessage,
-- CreateCloseMessage, DoPopModal, ClearModalChildren, GetWindowDepth, GetTimer,
-- StartMovie, StopMovie, IsMoviePlaying, ImageLoaded, UpdateWindow, _GetString,
-- _SetBitmap, _SetState, _RegisterHotkey, DebugOut ...).
--
-- Comments here are ours; the original has comment lines in the gaps between
-- the statements (the line numbers in the bytecode skip over them), whose text
-- is not recoverable.

LUA_PATH = '?;../?;../../?'

-- Horizontal and vertical alignment flags (combine with +).
kHAlignLeft = 0
kHAlignCenter = 1
kHAlignRight = 2
kVAlignTop = 0
kVAlignCenter = 4
kVAlignBottom = 8

-- Button kinds.
kPush = 0
kToggle = 1
kRadio = 2

kAllLayers = -1

-- Position / size sentinels, interpreted by the window code.
kCenter = 80000
kMax = 160000
kDefault = 128

kPushButtonAlignment = kHAlignCenter + kVAlignCenter
kRadioButtonAlignment = kHAlignLeft + kVAlignCenter
kToggleButtonAlignment = kHAlignCenter + kVAlignCenter

-- Message types (consecutive numbers starting at kGeneric = 0).
kGeneric = 0
kCloseWindow = kGeneric + 1
kDefaultAction = kCloseWindow + 1
kButtonPress = kDefaultAction + 1
kPressAnyKey = kButtonPress + 1
kQuitNow = kPressAnyKey + 1
kModalClosed = kQuitNow + 1
kTextEditChanged = kModalClosed + 1
kCommandOnly = kTextEditChanged + 1
kSliderValChanged = kCommandOnly + 1
kSliderMouseUp = kSliderValChanged + 1
kSliderPageUp = kSliderMouseUp + 1
kSliderPageDown = kSliderPageUp + 1
kUserMessageBase = 1000

-- Names of engine state values (used with SetState / GetState).
kCmdLine = "cmdline"
kLevel = "level"
kSubLevel = "sublevel"
kPause = "pause"
kMute = "mute"
kBreakNow = "breaknow"
kScore = "score"
kGameMode = "gamemode"
kGameOver = "gameover"
kNoMoreContent = "nomorecontent"
kDownloadButton = "downloadbutton"
kUpsellFrequency = "upsellfrequency"
kRestartButton = "restartbutton"
kSystemMode = "systemmode"
kMaxLevel = "maxlevel"
gReturnValue = nil
kCheatMode = "cheatmode"
kComputerId = "computerid"
kInstallKey = "installkey"
kGameName = "gamename"
kGameVersion = "version"
kEncryptionKey = "encryptionkey"
kHiscoreLocalOnly = "hiscorelocal"
kHiscoreAnonymous = "hiscoreanon"

idtable = {}
gParent = {}
gCurrentDefaultStyle = {}
gCurID = 100
gBeginGroup = 0

-- Color(r, g, b [, a]): components 0..255 -> {r, g, b, a} in 0..1; alpha 1 if omitted.
function Color(r, g, b, a)
	if a then
		return {r / 255, g / 255, b / 255, a / 255}
	else
		return {r / 255, g / 255, b / 255, 1}
	end
end

-- FColor(r, g, b [, a]): the same with components already 0..1.
function FColor(r, g, b, a)
	if a then
		return {r, g, b, a}
	else
		return {r, g, b, 1}
	end
end

-- Push / Pop: maintain the stack of parent windows (index 1 is the top).
function Push(window)
	table.insert(gParent, 1, window)
end

function Pop()
	table.remove(gParent, 1)
end

-- Declared here in the original and never referenced by any function.
local currentWindow

-- DoWindow(window): body of every typed window constructor; creates the
-- native window, builds its children, then pops it.
function DoWindow(window)
	window.beginGroup = gBeginGroup
	gBeginGroup = 0
	GenericCreate(window)
	MakeDialog(window)
	PopWindow()
end

-- DoDialogCommand(i, cmd): table.foreachi callback; runs one child closure.
function DoDialogCommand(i, cmd)
	cmd()
end

-- MakeDialog(dialogcommands): applies the window's name, runs each child
-- closure in order (table.foreachi), then restores the default style and
-- lets the engine finish the window (PostCreate).
function MakeDialog(dialogcommands)
	local currentStyle = gCurrentDefaultStyle

	if dialogcommands.name and dialogcommands.name ~= "" then
		SetWindowName(dialogcommands.name)
	end

	table.foreachi(dialogcommands, DoDialogCommand)

	typename = dialogcommands.typename

	-- Children may have changed the default style (SetStyle / AppendStyle).
	SetDefaultStyle(currentStyle)

	if typename then
		-- Note: typename is a global, read back after the children ran.
		PostCreate(dialogcommands, typename)
	end
end

-- Creating(type, table): hook called by the window code; empty by default.
function Creating(type, table)
end

-- SetBeginGroup(style): marks the next window as the start of a group.
function SetBeginGroup(style)
	gBeginGroup = 1
end

-- BeginGroup(): child-list entry that calls SetBeginGroup when run.
function BeginGroup()
	return function()
		SetBeginGroup()
	end
end

-- GetTag(tab, tag [, traverseParents]): look a tag up in tab, then in the
-- current default style (first call), then along tab.parent.
function GetTag(tab, tag, ...)
	if tab[tag] then
		return tab[tag]
	end
	local traverseParents = arg[1]
	if not traverseParents then
		return GetTag(gCurrentDefaultStyle, tag, true)
	end
	if tab.parent then
		return GetTag(tab.parent, tag, true)
	end
	return nil
end

-- SetDefaultStyle(style): replace the current default style table.
function SetDefaultStyle(style)
	gCurrentDefaultStyle = style
end

-- SetStyle(style): child-list entry that installs style when run.
function SetStyle(style)
	return function()
		SetDefaultStyle(style)
	end
end

-- AppendStyle(style): like SetStyle but chains onto the current style.
function AppendStyle(style)
	return function()
		style.parent = gCurrentDefaultStyle
		SetDefaultStyle(style)
	end
end

-- SetFocus(name): focus the named window.
function SetFocus(name)
	RealSetFocus(name)
end

-- SetDefault(default): accepted and ignored; returns a closure that does nothing.
function SetDefault(default)
	return function()
	end
end

-- DistributeX(number): `number` x positions evenly spread over gDialogWidth.
-- (divisor and d are globals in the original.)
function DistributeX(number)
	divisor = number + 1
	d = {}
	for i = 1, number do
		d[i] = i / divisor * gDialogWidth
	end
	return d
end

-- DistributeY(number): the same for y over gDialogHeight (locals this time).
function DistributeY(number)
	local divisor = number + 1
	local d = {}
	for i = 1, number do
		d[i] = i / divisor * gDialogHeight
	end
	return d
end

gClosedWindows = {}

-- WaitForCloseMessage(id): pump messages until the modal `id` reports closed.
-- Closes of other modals are remembered in gClosedWindows; a close that
-- arrived earlier is consumed here.
function WaitForCloseMessage(id)
	while true do
		local message = WaitForMessage()
		if message ~= nil then
			gLastMessage = message
			if message.type == kModalClosed and message.name == id then
				break
			elseif message.type == kModalClosed then
				gClosedWindows[message.name] = true
			end
		end
		if gClosedWindows[id] == true then
			gClosedWindows[id] = nil
			break
		end
	end
end

-- DoModal(fileToRead): run a dialog script modally; returns gReturnValue.
function DoModal(fileToRead)
	local id = PushModal(fileToRead)

	ReadFile(fileToRead)
	DoFitToChildren()

	WaitForCloseMessage(id)

	local temp = gReturnValue
	gReturnValue = nil
	return temp
end

-- DoMainWindow(fileToRead): like DoModal for a window that stays up while
-- child windows come and go; returns once the window depth drops below the
-- depth recorded after PushModal.
function DoMainWindow(fileToRead)
	PushModal(fileToRead)
	local depth = GetWindowDepth()

	ReadFile(fileToRead)
	DoFitToChildren()

	while true do
		local message = WaitForMessage()
		if message ~= nil then
			gLastMessage = message
			if GetWindowDepth() < depth then
				break
			end
		end
	end
end

-- ModalReturn(value): set the value DoModal/DisplayDialog will return.
function ModalReturn(value)
	gReturnValue = value
end

-- DisplaySplash(splashMovie, splashGraphic, time): modal splash screen shown
-- as a movie or as a still image, ended by timeout, close, or key press.
function DisplaySplash(splashMovie, splashGraphic, time)
	local id = PushModal("DisplaySplash")

	local translate = GetTag({}, "translate")
	if not translate then
		translate = false
	end
	local allowInput = GetTag({}, "allowInput")
	if not allowInput then
		allowInput = false
	end

	inMovie = StartMovie(splashMovie, translate, allowInput)

	local endTime = GetTimer() + time
	local disableAbort = GetTag({}, "disableAbort")
	if not disableAbort then
		disableAbort = false
	end

	if inMovie == false then
		-- No movie started: show the still image instead.
		local overlay = GetTag({}, "splashoverlay")
		if not overlay then
			overlay = Group({})
		end
		if not splashGraphic then
			return
		end

		MakeDialog({
			Bitmap{
				name = 'splashimage',
				image = splashGraphic,
				overlay
			}
		})

		UpdateWindow()
		Pause(50)
		UpdateWindow()
		Pause(50)
	end

	-- UNCERTAIN: the original assigns `time` here (not endTime); `time` is
	-- not read again, so this has no effect. Kept as in the bytecode.
	if inMovie then
		time = 50
	end

	-- The wait loop only runs while a movie plays or the image is loading.
	-- (The bytecode is an `if` around a `while true do`, not a `while cond`.)
	if inMovie == true or ImageLoaded('splashimage') == true then
		while true do
			local message = WaitForMessage(1)

			if not inMovie and endTime < GetTimer() then
				break
			elseif message ~= nil then
				if message.type == kCloseWindow then
					break
				end

				if message.type == kModalClosed then
					if message.name == id then
						break
					else
						gClosedWindows[message.name] = true
					end
				end

				if disableAbort == false and message.type == kPressAnyKey then
					break
				end
			end

			if inMovie == true and IsMoviePlaying() == false then
				break
			end
		end
	end

	if inMovie == true then
		StopMovie()
	end

	PopModal(id)
end

gCommandQueue = {}
gMessageQueue = {}

-- FlushMessages(): drain every pending message, then empty both queues.
function FlushMessages()
	local message, command
	repeat
		message, command = HandleGetMessage(1)
		if message then
			-- UNCERTAIN: the original has an empty (probably commented-out)
			-- body here; the bytecode is a test and an empty branch.
		end
	until not message

	gCommandQueue = {}
	gMessageQueue = {}
end

-- WaitForMessage(delay, deferQueue): the one place a script yields for input.
-- Returns the next message (nil on timeout). Queued messages are returned
-- first unless deferQueue is true. A command delivered with the message, or
-- the last queued command, is run and its result kept in gYieldResult.
function WaitForMessage(delay, deferQueue)
	local n = table.getn(gMessageQueue)
	if not deferQueue and 0 < n then
		message = gMessageQueue[n]	-- global, as in the original
		table.remove(gMessageQueue)
		return message
	end

	local message, command = HandleGetMessage(delay)
	if command then
		gYieldResult = command()
	else
		local n = table.getn(gCommandQueue)
		if 0 < n then
			command = gCommandQueue[n]
			table.remove(gCommandQueue)
			gYieldResult = command()
		end
	end
	return message
end

-- Yield(): yield the script coroutine.
function Yield()
	return coroutine.yield()
end

-- ReadFile(fileToRead): run a script from the assets directory.
function ReadFile(fileToRead)
	return dofile('assets/' .. fileToRead)
end

-- CloseWindow([param]): post a close message; optionally set the modal's result.
function CloseWindow(param)
	PostMessage(CreateCloseMessage())
	if param then
		ModalReturn(param)
	end
end

-- CustomCreator(s): hook; empty by default.
function CustomCreator(s)
end

-- GetString(id, p1 .. p5): localised string with up to five parameters.
function GetString(id, p1, p2, p3, p4, p5)
	if not p1 then p1 = "" end
	if not p2 then p2 = "" end
	if not p3 then p3 = "" end
	if not p4 then p4 = "" end
	if not p5 then p5 = "" end

	return _GetString(id, p1, p2, p3, p4, p5)
end

-- SetBitmap(name, image, scale): scale defaults to 1.
function SetBitmap(name, image, scale)
	if scale == nil then
		scale = 1
	end

	_SetBitmap(name, image, scale)
end

-- PopModal(r): close the top modal with result r.
function PopModal(r)
	DoPopModal(r)
end

-- SwapToModal(name): reuse the top modal for another dialog script.
function SwapToModal(name)
	ClearModalChildren()
	ReadFile(name)
	DoFitToChildren()
end

gDialogTable = {}

-- DisplayDialog(t): t[1] is the dialog script, the rest of t is its
-- parameters (read by the script through gDialogTable). Returns gReturnValue.
function DisplayDialog(t)
	local script = t[1]

	if not script or script == "" then
		DebugOut("DisplayDialog called with empty script; aborting")
		return
	end

	-- Note: unlike DoModal there is no DoFitToChildren() here.
	local id = PushModal(script)

	gDialogTable = t
	ReadFile(script)

	WaitForCloseMessage(id)

	local temp = gReturnValue
	gReturnValue = nil
	return temp
end

-- SelectLayer(layer): child-list entry selecting a layer.
function SelectLayer(layer)
	return function()
		DoSelectLayer(layer)
	end
end

-- Group(t): child-list entry that builds the children in t without a window.
function Group(t)
	return function()
		MakeDialog(t)
	end
end

-- FitToChildren(): child-list entry that resizes the window to its children.
function FitToChildren()
	return function() DoFitToChildren() end
end

-- Pause(time, waitForKey): wait `time` timer units. Messages that arrive are
-- put back on gMessageQueue, except a press-any-key when waitForKey is set,
-- which ends the pause early.
function Pause(time, waitForKey)
	if waitForKey == nil then
		waitForKey = false
	end

	local endTime = GetTimer() + time
	while GetTimer() < endTime do
		local message = WaitForMessage(endTime - GetTimer(), true)

		if message ~= nil then
			if waitForKey and message.type == kPressAnyKey then
				break
			else
				table.insert(gMessageQueue, message)
			end
		end
	end
end

-- SetState(key, value): booleans become 1/0; both go to the native as strings.
function SetState(key, value)
	if type(value) == "boolean" then
		if value then
			value = 1
		else
			value = 0
		end
	end

	_SetState(tostring(key), tostring(value))
end

-- RegisterHotkey(hotkey, buttonName): ignored unless both are given.
function RegisterHotkey(hotkey, buttonName)
	if hotkey and buttonName then
		_RegisterHotkey(hotkey, buttonName)
	end
end
