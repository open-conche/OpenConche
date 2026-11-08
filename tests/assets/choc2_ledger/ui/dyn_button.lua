-- DynamicWindow test body: a button whose command rebuilds the window it lives in.
buttonBuilds = (buttonBuilds or 0) + 1
Button{name = 'rebuilder', x = 0, y = 0, w = 40, h = 20, graphics = {}, command = function() UpdateDynamicContent('self') end}()
