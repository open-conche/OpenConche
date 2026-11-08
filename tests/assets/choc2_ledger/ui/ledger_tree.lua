-- A Chocolatier 2 ledger as ui/ledger.lua builds it (without the pictures), with the map/port buttons beside it.
Button{name = 'to_map', x = 158, y = 26, w = 40, h = 40, graphics = {}}()
Text{name = 'to_map_label', x = 150, y = 70, w = 60, h = 20, label = '#map'}()
Button{name = 'to_port', x = 158, y = 26, w = 40, h = 40, graphics = {}}()
Text{name = 'to_port_label', x = 150, y = 70, w = 60, h = 20, label = '#city'}()
Ledger{name = 'ledger', x = 0, y = 290, w = 800, h = 310, data = gSim, showQuestY = 0, hideQuestY = 53,
  Window{name = 'quest', x = 235, y = 53, w = 587, h = 129,
    Text{name = 'quest_text', x = 44, y = 62, w = 497, h = 39, label = gQuestLabel or '#'}},
  Window{x = 258, y = 100, w = 540, h = 208,
    Window{name = 'contents', x = 21, y = 40, w = 498, h = 157,
      DynamicWindow{name = 'messages', x = 0, y = 0, w = 498, h = 157, contents = 'ui/tab_messages.lua', init = false},
      DynamicWindow{name = 'factories', x = 0, y = 0, w = 498, h = 157, contents = 'ui/tab_factories.lua', init = false},
      DynamicWindow{name = 'inventory', x = 0, y = 0, w = 498, h = 157, contents = 'ui/tab_inventory.lua', init = false}},
    BeginGroup(),
    Button{name = 'btn_messages', type = kRadio, x = 1, y = 3, w = 100, h = 30, graphics = {}},
    Button{name = 'btn_factories', type = kRadio, x = 170, y = 3, w = 100, h = 30, graphics = {}},
    Button{name = 'btn_inventory', type = kRadio, x = 396, y = 3, w = 100, h = 30, graphics = {}}},
  Window{x = 0, y = 72, w = 263, h = 237,
    DynamicWindow{name = 'badge', x = 0, y = 0, w = 263, h = 237, contents = 'ui/badge.lua'}}}()
