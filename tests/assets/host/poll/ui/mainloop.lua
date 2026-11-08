-- WaitForMessage() with no timeout polls: Main is resumed on every tick.
gCount = 0
function Main()
	while true do
		WaitForMessage()
		gCount = gCount + 1
	end
end
return Main
