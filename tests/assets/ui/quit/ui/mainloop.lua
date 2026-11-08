function Main()
	PushModal("ui/quitter.lua")
	PostMessage(CreateNamedMessage(kQuitNow, "Quit"))
	while true do WaitForMessage() end
end
return Main
