-- DynamicWindow test body: counts its builds and makes one Text.
dynBuilds = (dynBuilds or 0) + 1
Text{name = 'dyn_text', x = 0, y = 0, w = 100, h = 20, label = '#build ' .. dynBuilds}()
