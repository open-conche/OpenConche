-- A stand-in for the game's scripts/console.lua: the dialog window with its one text field and the two buttons that call DoCommand.
MakeDialog
{
	RWindow
	{
		name = "console_window",
		x = 10, y = 10, w = 350, h = 150,
		TextEdit
		{
			x = 3, y = 5, w = 300, h = 30,
			name = "command_textedit",
			length = 200,
		},
		Button
		{
			name = "console_ok",
			x = 20, y = 80, w = 100, h = 30,
			label = "OK",
			command = function() DoCommand(GetLabel("command_textedit")) end
		},
		Button
		{
			name = "console_close",
			x = 160, y = 80, w = 100, h = 30,
			label = "Close",
			command = function() DoCommand("closeconsole") end
		},
	},
}
SetFocus("command_textedit")
