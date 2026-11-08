-- FillWindow test: builds one Text named "filled" into the window being filled.
local t = { typename = 'Text', name = 'filled', label = '#filled', x = 0, y = 0, w = 20, h = 10 }
GenericCreate(t)
PostCreate(t, 'Text')
PopWindow()
