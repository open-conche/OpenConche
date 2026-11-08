-- A command in gCommandQueue runs when WaitForMessage resumes without one.
gRan = 0
function Main()
	table.insert(gCommandQueue, function() gRan = gRan + 1; return "ran" end)
	WaitForMessage()
	gAfter = gYieldResult
	WaitForMessage()
end
return Main
