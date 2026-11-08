-- DynamicWindow test body that asks for its own rebuild while it is being built.
nestedBuilds = (nestedBuilds or 0) + 1
UpdateDynamicContent('nested')
Text{name = 'nested_text', x = 0, y = 0, w = 100, h = 20, label = '#nested'}()
