-- Yielding inside pcall raises across the C boundary in Lua 5.0; the script catches it and carries on.
function Main()
	gOk, gErr = pcall(WaitForMessage, 1000)
	gCaught = true
	WaitForMessage(1000)
end
return Main
