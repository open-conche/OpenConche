-- A synthetic stand-in for the game's entry script: returns a Main that waits (a real one would show the splashes).
function Main()
	while true do
		Yield()
	end
end
return Main
