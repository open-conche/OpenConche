function Main()
	coroutine.yield(1, 2)
	gAfter = true
	WaitForMessage(1000)
end
return Main
