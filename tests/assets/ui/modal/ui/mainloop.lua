-- Main opens a dialog modally and records what DoModal returned.
function Main()
	gResult = DoModal("ui/dlg.lua")
	gDone = true
	while true do
		WaitForMessage()
	end
end
return Main
