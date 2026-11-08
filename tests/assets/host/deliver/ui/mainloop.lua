function Main()
	local m
	repeat
		m = WaitForMessage()
	until m
	gKind = type(m)
	gType = m.type
	gName = m.name
	gFoo = m.foo
	repeat
		m = WaitForMessage()
	until m
	gSecond = true
end
return Main
