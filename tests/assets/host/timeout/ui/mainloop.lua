-- WaitForMessage(100) with no message times out after 100 ms and returns nil.
function Main()
	gStart = GetTimer()
	gMsg = WaitForMessage(100)
	gEnd = GetTimer()
	gDone = true
end
return Main
