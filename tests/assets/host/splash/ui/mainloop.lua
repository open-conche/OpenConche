function Main()
	DisplaySplash("splash/logo.swf", "splash/logo", 4000)
	gAfterMovie = GetTimer()
	DisplaySplash("", nil, 1)
	gDone = true
end
return Main
