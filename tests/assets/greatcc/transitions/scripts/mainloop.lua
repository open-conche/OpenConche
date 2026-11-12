-- A synthetic Main for the transition tests: waits for a message every tick (so queued command strings run), and when the
-- test sets gDo it calls DoTransition(gDo) and records the result.
function Main()
	while true do
		WaitForMessage(0)
		if gDo ~= nil then
			local id = gDo
			gDo = nil
			gRet = DoTransition(id)
			gDoneCount = (gDoneCount or 0) + 1
		end
	end
end
return Main
