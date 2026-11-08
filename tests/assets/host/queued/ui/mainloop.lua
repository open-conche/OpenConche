-- A message delivered while Main is running goes to gMessageQueue; WaitForMessage then returns it without yielding.
-- The queue tables first go through table.insert/table.remove (as Pause and WaitForMessage do), which keeps a cached
-- size that a raw append must update.
function Main()
	table.insert(gMessageQueue, "filler")
	table.insert(gMessageQueue, "filler")
	table.remove(gMessageQueue)
	table.remove(gMessageQueue)
	Pause(10)
	TestDeliver("q1")
	TestDeliver("q2")
	gQueued = table.getn(gMessageQueue)
	local m = WaitForMessage()
	gType = m.type
	gName = m.name
	local m2 = WaitForMessage()
	gName2 = m2.name
	gLeft = table.getn(gMessageQueue)
	gDone = true
end
return Main
