-- The unit tests drive the windows directly; Main only exists so that boot succeeds.
function Main()
	while true do
		WaitForMessage()
	end
end
return Main
