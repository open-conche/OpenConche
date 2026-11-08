-- animationscript.lua
--
-- The per-sprite animation script. Every animated sprite runs this chunk first in its own Lua state, then the
-- script text of its sprite sheet (which defines DoAnim and friends). The natives it calls are registered by the
-- engine in the same state: WaitLuaNextFrame, SetCurrentFrame, SetAlpha, Load, GetScript, Pause, IsPlaying, Die,
-- RemoveSprite, SetLayer and Random.
--
-- Reconstructed from the compiled chunk in the Mac build (re/mac/lua/animationscript.lst); tools/
-- check_animationscript.py compiles this file with the patched luac and compares it with that listing, which
-- is why statements sit on particular lines and the globals are the ones the compiled chunk uses.
--
-- Frame numbers in frames{} lists are 1-based. Delays are in milliseconds.

local framedelay = 16

alpha = 1
alphadelta = 0
alphacount = 0

-- delay(ms): the time every later frame is held for.





function delay(ms)
	framedelay = ms
end

-- pause(ms): wait without changing the frame.


function pause(ms)
	WaitLuaNextFrame(ms)
end

-- showFrame(): hold the current frame for the frame delay.

function showFrame()
	WaitLuaNextFrame(framedelay)
end

-- fadeout(n) / fadein(n): spread an alpha change over the next n frames shown by frames{}.


function fadeout(numberOfFrames)
	alphadelta = -alpha / numberOfFrames
	alphacount = numberOfFrames
end




function fadein(numberOfFrames)
	alphadelta = (1 - alpha) / numberOfFrames
	alphacount = numberOfFrames
end

-- setAlpha(a): set the sprite's alpha at once.
function setAlpha(a)
	alpha = a
	SetAlpha(a)
end

-- frames{...}: show each listed frame (1-based) for the frame delay. A pending fade takes one step per frame.



function frames(framelist)
	count = table.getn(framelist)
	for i = 1, count do
		frame = framelist[i]
		if alphacount ~= 0 then
			alpha = alpha + alphadelta
			SetAlpha(alpha)
			alphacount = alphacount - 1
			if alphacount == 0 then
				alphadelta = 0
			end
		end
		SetCurrentFrame(frame - 1)
		WaitLuaNextFrame(framedelay)
	end
end

-- loop(times, list): frames(list), repeated.



function loop(times, framelist)
	for i = 1, times do
		frames(framelist)
	end
end

-- run{ xml, image = , mask = , anim = }: switch the sprite to another sheet and run one of its functions
-- (DoAnim by default) inline. Does nothing when the sheet cannot be loaded.



















































function run(table)
	local xmlToRun, functionToRun, image, mask

	xmlToRun = table[1]

	image = table.image
	if not image then
		image = "" end


	mask = table.mask
	if not mask then
		mask = "" end


	functionToRun = table.anim
	if not functionToRun then
		functionToRun = "DoAnim" end


	if Load(xmlToRun, image, mask) then
		local script = GetScript() .. "; " .. functionToRun .. "()"
		local fn = loadstring(script)
		if fn then
			fn()
		end
	end
end

-- die(): end this sprite's script.

function die()
	Die()
end

-- halt() / stop(depth): pause the sprite's script (and its children's, to the given depth; -1 is all).

function halt()
	Pause(true, -1)
end

-- isPlaying(depth): true while this sprite's script, or a child's within depth, is running.



function isPlaying(depth)
	if not depth then depth = -1 end
	return IsPlaying(depth)
end

function endReferences()
end



function stop(depth)
	if not depth then
		depth = -1 end

	Pause(true, depth)
end

-- random(min, max): min <= n < max.


function random(min, max)
	return Random(min, max)
end
