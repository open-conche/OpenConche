-- A Main that only waits; the ledger tests drive the natives with `lua` commands.
function Main()
	while true do
		WaitForMessage()
	end
end
return Main
