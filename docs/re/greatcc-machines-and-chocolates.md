# Great Chocolate Chase (GREATCC): chocolate-making machines, chocolates, carriables, upgrades

Tags: **F** = fact (read from the Ghidra decompile of the Mac i386 binary, from `re/greatcc/symbols.txt`, from file bytes, or from the plain-text Mac assets; cited as `Class::Method` or `file`), **I** = inference, **U** = unknown.
Sources: `re/greatcc/decomp/{VMachine,VMachineButton,VMixer,VIngredient,VPress,VInfuse,VTopping,VSauce,VDrink,VDelivery,VDisposal,VConveyor,VChocolate,VChocolateLayer,VChocolateVendor,VOrderInfo,VCarriable,VUpgrade,VUpgradeManager,VSuspendable,VValue}.c`, small excerpts of `VStation`, `VPlayer`, `VCustomer`, `VDisplay`, `ShopWindow`, `VSprite`; assets under the Mac bundle's `assets/` (`locations/machines.lua`, `locations/defines.lua`, `locations/<loc>/{chocolates,stations}.lua`, `locations/<loc>/upgrades.xml`, `values.xml`, `levels/*.xml`, `anim/machines/**`, `anim/chocolates/**`). Scratch (decoded `.anm` scripts): `re/greatcc/scratch/machines/`.
Locations and level ranges (from the `<location>` tag of `levels/levelNN.xml`, **F**): `iquitos` 1-10, `grytviken` 11-20, `namche` 21-30, `krakatoa` 31-40, `kamchatka` 41-50, `gaberoun` 51-60.

## 0. Big picture

* **F** The game has **no recipe table**. A "chocolate" is a sprite (`VChocolate`) holding up to five layer sprites (`VChocolateLayer`) in fixed slots. Machines add layers; a customer's order is another `VChocolate` built (by `VChocolateVendor`) from a `VOrderInfo`; serving is a slot-by-slot equality test.
* **F** Machines are `VMachine` subclasses, one per station type, created by `VStation::LoadFromTable` from the `machines` sub-table of the station's Lua entry. All machine timing comes from **animation scripts inside the `.anm` files** (frame delays and `unlock()`/`drop()` callbacks), plus a per-machine frame delay (`VMachine+0x274`, 30 ms; mixer 20 ms) and a handful of `Suspend(ms)` waits in C++. `values.xml` has **no machine-speed entries** except the sauce/drink cooldown and the delivery interval.
* **F** Sauce and drink are not chocolates: they are plain `VCarriable` objects with a type tag string (`"sauce"`, `"drink"`), produced by the sauce/drink machine on a cooldown, and matched to orders by the tag only.
* **F** `VDelivery` is **not** an ingredient restock: it is the zeppelin order machine (a periodic chocolate request that the player fills by walking a matching chocolate to the box station). `VDisposal` is the trash (no penalty in this class). `VConveyor` is an arc belt that carries finished chocolates past the six counters and auto-hands them to customers.
* **F** Upgrades are per location (`locations/<loc>/upgrades.xml`), persisted per location as a byte-per-upgrade blob; machines read their state straight from `VUpgradeManager::IsActive(name, category)`; numeric tuning goes through `values.xml` `<modify upgrade=... category=...>` entries evaluated lazily by `VValue::GetValue`.

## 1. Enumerations (`locations/defines.lua`, F)

| Group | Names = values |
|---|---|
| Station types (`eStationType`) | `kEmpty` 0, `kCounter` 1, `kMixer` 2, `kIngredient` 3, `kBarPress` 4, `kTopping` 5, `kSauce` 6, `kInfuse` 7, `kDrink` 8, `kDisposal` 9, `kDelivery` 10, `kDisplay` 11, `kConveyor` 12 |
| Chocolate layers (`eChocolateLayer`) | `kEmptyLayer` -1, `kBucketLayer` 0, `kIngredientLayer` 1, `kChocolateLayer` 2, `kInfusionLayer` 3, `kToppingLayer` 4 |
| Bar (shape) types (`eChocolateType`) | `kNothing` -1, `kBar` 0, `kTruffle` 1, `kNovelty` 2, `kInfusion` 3 |
| Order types (`eOrderType`) | chocolate 0, sauce 1, drink 2 (strings `kchocolate/chocolate`, `ksauce/sauce`, `kdrink/drink`, case-insensitive; anything else -1) `VOrderInfo::GetOrderTypeFromString` |
| Other | `DarkChocTint = FColor(0.6,0.6,1.0)` (RGB; used as the `tint` of every dark shape) |

String parsers (all lower-case the input first, F): `VChocolate::GetTypeFromString` accepts `bar|kbar`, `novelty|knovelty`, `truffle|ktruffle`, `infusion|kinfusion` (unknown -> -1). `VChocolateLayer::GetTypeFromString` accepts `bucketlayer|kbucketlayer|bucket|kbucket` (0), `chocolatelayer|kchocolatelayer|chocolate|kchocolate` (2), `ingredientlayer|kingredientlayer|ingredient|kingredient` (1), `toppinglayer|ktoppinglayer|topping|ktopping` (4), `infusionlayer|kinfusionlayer|infusion|kinfusion` (3); unknown -1.

## 2. Chocolates

### 2.1 `VChocolateLayer` (0x21c bytes; `VCarriable`/`VObject` subclass) **F**

| Offset | Field | Source |
|---|---|---|
| +0x210 | `option` (which mix / filling / infusion / topping, 0..2) | Lua `option`, default **0** |
| +0x214 | `layerType` | Lua `layerType`, default **0** |
| +0x218 | `barType` | Lua `barType`, default **0** (so a layer that omits it is a `kBar` layer) |

* `VChocolateLayer::LoadFromTable` (`VLuaStyle::GetNumber<int>(key, 0, -1)` - the `0` is the default, `-1` the style-inheritance depth) reads those three keys, then calls the sprite's `SetLayer` with the mapping **layerType -> sprite z-layer: 2 (chocolate/shape) -> 1, 1 (ingredient) -> 2, 3 (infusion) -> 3, 4 (topping) -> 4, anything else (bucket) -> 0**; then the normal `VObject::LoadFromTable` (assets, `x`, `y`, `tint`, ...). The constructor `VChocolateLayer(layer,bar,option)` applies the same mapping and sets +0xd7 (`leaf`) = 1, so a click on a layer goes to its container; `VChocolate` is a leaf too.
* `Compare(other)` = other non-null **and** all three of `layerType`, `barType`, `option` equal (`VChocolateLayer::Compare`). `CopyTo` copies the three ints, then the `VObject` look (asset states, position).
* Draw order is therefore bottom-to-top: bucket (0) / **shape** (1) / **ingredient (filling)** (2) / **infusion** (3) / **topping** (4). The shape is the base, the filling sprite is drawn over it, toppings last.

### 2.2 `VChocolate` (0x264 bytes) **F**

A `VCarriable(1)` (needs 1 hand). Sprite on z-layer 2 with the empty texture `anim/transparent`; its type tag (`VObject+0x194`) is `"chocolate"`; name `chocolate_<n>` (static counter `mChocCnt`, reset to 0 each level load in `ShopWindow::LevelInit`).

| Offset | Field | Init |
|---|---|---|
| +0x214 + 8*i | `shared_ptr<VChocolateLayer> slot[i]`, i = 0..4 (layerType index) | null |
| +0x23c | `top`: highest occupied slot index | -1 |
| +0x240 | `barType` (-1 until a shape/infusion layer is added) | -1 |
| +0x244 | `shared_ptr<VChocolateVendor>` | ctor arg |
| +0x24c | `ingredientId` (bucket filling choice, set by `AddIngredient`) | -1 |
| +0x250 | `mixOption` (0 dark / 1 milk / 2 white; set by `GetEmptyChocolate`) | -1 |
| +0x254 | `decay` float, "percent life left" | **100.0** |
| +0x258 | fade `VAlphaEffect` | null |
| +0x260 | `decayEnabled` bool | false |

Methods (all **F** unless marked):

* `GetNumLayers` = number of non-null slots among 1..4 (the bucket slot 0 is not counted).
* `IsBucket` = `top < 2` (so an empty chocolate, a bare bucket, and a bucket with ingredient all count as buckets; any chocolate with a shape layer is not).
* `CanAddLayer(L)`: L < 3 -> `top < L`; L == 3 (infusion) -> `top < 1`; L == 4 (topping) -> `top >= 2 && L > top` (i.e. `top` is 2 or 3). Consequences: bucket (slot 0) can only start an empty chocolate; the **ingredient** machine accepts a bare bucket only (`top == 0`); the press accepts a bucket with or without ingredient (`top < 2`); the **infusion** machine accepts only a bucket **without** ingredient (`top < 1`; `AddIngredient` sets `top = 1`); toppings need a shaped chocolate or an infusion bar (`top` 2 or 3).
* `AddIngredient(id)`: sets `ingredientId = id`, `top = 1`, and, if the bucket layer has an `icon` child, calls `icon.AddAssetLeaf("overlay", "option_<id>", 0)` (F, disassembly): a new leaf child of the icon showing the `overlay` asset's state (the barrel icon shows the filling). It does not switch the icon's own asset.
* `AddLayer(shared_ptr layer)`: no-op for null. If the global tint map `mChocTint[layerType]` has an entry, copies it into the layer's colour. Replaces any sprite already in that slot (removes the old child), stores the new layer, adds it as a child, `top = max(top, layerType)`. If `top > 1` sets +0x210 = 1. **If the layer is slot 2 (shape) or 3 (infusion): the bucket (slot 0) is removed and nulled (`top` is not lowered; it is already 2 or 3); if `ingredientId >= 0` the ingredient layer (slot 1) is created from the vendor with `AddLayer(layer.barType, 1, ingredientId)` (F, disassembly: the three arguments are the new layer's `barType`, 1 and the id); if the layer is an infusion layer and slot 2 is empty a shape layer is created automatically with `AddLayer(layer.barType, 2, mixOption)` (F, disassembly); finally `barType = layer.barType`.** So shaping converts "bucket(+filling icon)" into real layers and fixes the shape.
* `AddLayer(barType, layer, option)`: asks `VChocolateVendor::GetLayer` for a cloned prototype (§2.4); if it exists, `Init`s it and calls the `shared_ptr` overload; returns the layer (null if the vendor has no such prototype). Used by the Press (shape), Infuse (infusion layer on bucket), Topping, and by the vendor when building orders.
* `RemoveLayer(L)`: removes the child, nulls the slot, then lowers `top` while `top` points at an empty slot.
* `Compare(other)` (**the exact match rule**): other non-null, then for each slot i = 0..4: if `other.slot[i]` is null then `this.slot[i]` must be null; else `other.slot[i].Compare(this.slot[i])` must be true (so a null on `this` side fails). I.e. **all five slots must be equal triples (layerType, barType, option)**; nothing else is compared (no `mixOption`, no ingredient id, no decay state, no price).
* `CompareWithLayer(other, L, option)`: false unless `other` non-null, `option >= 0`, `L > 0`. Then slots 0..L-1 must match exactly as in `Compare` (iterating `other`'s slots), and `this.slot[L]` must exist with `option == option`. Used by `ShopWindow::FindOrderWithLayer` to decide which held chocolate a customer's order is "heading toward" (see §4.4). Quirk (**F, I for impact**): a held chocolate that still has its bucket in slot 0 can never satisfy this (the order has a null slot 0), so for layer >= 1 on buckets it always fails and the machine falls back to the default hand.
* `GetCost()` (price of one chocolate): `layerPrice = value layer_price` (default 0 if missing; shipped 10), `q = value layer_quality_modifier` (default 1.0 if missing; shipped **0.1**). For each present slot i = 1..4: add `layerPrice`; if the matching **quality upgrade** is active (category `station`) add `q * layerPrice`. Upgrade names: slot 1 -> `kIngredientQ<option+1>`, slot 2 -> `kMixerQ<mixOption+1>` (note: uses `+0x250`, not the layer option), slot 3 -> `kInfusionQ<option+1>`, slot 4 -> `kToppingQ<option+1>`. The bucket is free. Examples with shipped values: shape only 10; shape+filling+topping 30; infusion bar (shape+infusion) 20; each active matching Q adds 1.0. A **vendor-built (order) chocolate has `mixOption = -1`**, so the mixer quality term never applies to it - but the price is taken from the *served* (player-made) chocolate (`VCustomer::ServeCustomer`), which has `mixOption` set.
* `Update(paused)`: only if not paused, not `IsBucket`, `decay > 0`, `chocolate_decay_rate > 0` (value default 0 -> **no decay**) and `decayEnabled`: `decay -= elapsedMs * chocolate_decay_rate`. The first time it reaches <= 0 it starts a `VAlphaEffect(fadeOut, 1.0, duration = chocolate_fade_time (default 1000, shipped 4000 ms), start -1.0)` on itself and adds 1 to value `decayed_this_level`. The whole block runs only while `decay > 0` before the subtraction, so the fade and the `decayed_this_level` increment happen **exactly once** per chocolate. Then `VSprite::Update`.
* `HasDecayed()`: `decay <= 0` **and** (no fade effect, or the fade effect is no longer active). So a chocolate is "decayed" 4 s after the timer ran out.
* `ToString()`: `"(" + mix name (dark/milk/white by +0x250) + ",ingredient:<id>" (if id >= 0) + shape token (",bar" ",truffle" ",novelty" ",infusion" by +0x240) + ",filling:<option of slot 3>" (if slot 3 present) + ",topping:<option of slot 4>" (if present) + ")"` (slot 1 and slot 2 are not printed). Used by `VDisposal` for the `TrashUsedItemID` game-state string.
* `CancelEffects` also cancels the fade effect.
* `decayEnabled` is set to true only by `VChocolateVendor::GetEmptyChocolate(mix, true)`, which the mixer always calls with `true` (asm of `VMixer::ProcessChocolate`). Order chocolates built by `GetChocolate` have it false, so they never decay.
* Decay in the shipped data: `chocolate_decay_rate` is set only by `ShopWindow::LevelInit` (story mode): `100.0 / level.chocolatedecay` when the level XML has a non-zero `<chocolatedecay>` (ms). Only **levels 51-60 (Gaberoun)** have it: 15000 ms in six of them, 20000 ms in four. So a shaped chocolate lives `chocolatedecay` ms (counted only while its sprite is updating, not paused), then fades for 4000 ms, then disappears from whichever holder has it (below). **F**: `VPlayer::Update` detaches a decayed chocolate from either hand; `VMachine::Update` detaches it from the machine (no sound); `VConveyor::UpdateBeltItems` deletes it from the belt; `VDisplay::Update` removes it from the case.

### 2.3 How a chocolate is drawn (layer compositing) **F/I**

* The `VChocolate` itself draws nothing (`anim/transparent`); every `VChocolateLayer` is a child sprite with its own z-layer (§2.1) and its own `x`,`y` from the Lua style (almost always 0,0; the three `bar_topping_*` use `y = 7`; Grytviken's three `infusion_*` layers `x = 7`).
* Each layer prototype in `chocolates.lua` names a sheet and a frame via `assets = { <group> = { { state="default", file="<sheet>", base=<n> } } }`: shape sheets `bar_shape`, `truffle_shape`, `infusion_shape`, `<novelty>_shape` (frames 0/1/2 = dark/milk/white; dark also has `tint = DarkChocTint` (0.6,0.6,1.0)); filling sheets `bar_ingred`, `truffle_ingred`, `<novelty>_ingred` (`base` = the location's `ingred1..3` frame index); `infusion_layer` (`base` = `infusion1..3`); toppings `generic_topping` (`base` = `topping1..3`). The novelty sheet name is a per-location Lua variable (`scarab`, `whale`, `inca`, `egg`, `volcano`, `bell`).
* The bucket prototype (`barType = kNothing`, `layerType = kBucketLayer`) is the barrel sprite (`anim/chocolates/barrel/barrel`) with a child `icon` (`leaf`, hidden until used): asset group `choc` has states `option_0..2` (frames 13/14/15 of `barrel_icons` = dark/milk/white swatch) and group `overlay` has `option_0..2` (frame = the location's `ingred1..3`). `GetEmptyChocolate` shows the icon and sets `choc` to `option_<mix>`; `AddIngredient` sets `overlay` to `option_<ingredient>`.
* The global tint map `mChocTint` (applied inside `AddLayer`) is filled only by the `ShopWindow` command string `choctint <layer> r g b` (space separated, see `greatcc-debug-tools.md` 1.3; parsed in `ShopWindow` command handling; no shipped Lua/XML uses it, **F** by grep) - dead in practice.
* `gfx/choc_0.png .. choc_9.png` are **not** chocolate art: they are the player-portrait images on the choose-player screen (`scripts/chooseplayer.lua:98`). **F**.

### 2.4 `VChocolateVendor` (0xb4 bytes) - prototype library, capability pools, random order builder **F**

Created by `ShopWindow::LoadChocolates` from the location's `chocolates` table (`game.lua` `location_<name>.chocolates = <loc>Chocolates`), one per `ShopWindow`.

* **`LoadFromTable`**: clears the vendor, then for every entry that is a table (non-table entries such as `baseDir` are skipped): reads `barType`, `layerType`, `option` (each default 0 via `GetNumber<int>(key,0,-1)`), creates an empty `VChocolateLayer`, runs its `LoadFromTable`, `Preload("all","all")`s it and stores it at `proto[barType][layerType][option]`. Keys of the entry are irrelevant (the file names them `truffle_shape_dark`, `bar_topping_2` ...).
  * The bucket is `proto[-1][0][0]` (no `option` key -> 0).
* **`GetLayer(barType, layerType, option)`**: finds the prototype, makes a fresh `VChocolateLayer`, `CopyTo`s the prototype into it, calls its `Init` (vtable +0x28) and returns it; null if any key is missing.
* **`GetEmptyChocolate(mix, decayEnabled)`**: new `VChocolate`, sets `decayEnabled`, fetches the bucket layer `(−1, 0, 0)`, `AddLayer`s it, sets `mixOption = mix`, shows the layer's `icon` child and sets its `choc` asset to `option_<mix>`. Returns the chocolate.
* **Capability pools** (what the player's machines can currently make; also what random orders may ask for): two parallel structures are kept: *pool A* (`+0x20` per-barType/layer option sets and `+0x50` set of bar types) and *pool B* (`+0x68`, `+0x80`). `UpdateCapability(flag)` calls `UpdatePool(mode, active, barType, layer, option)` with mode 2 when `flag` is true (both pools) else 1 (pool B only). **Every shipped call site passes `true`** (`ShopWindow::UpgradeComplete`, `LevelInit`, endless level-up), so the two pools are always identical. Rules (each `active` = `VUpgradeManager::IsActive(name,"station")`):

  | Upgrade | Effect on pools |
  |---|---|
  | `kMixer1/2/3` | mix options 0/1/2 (layer 2) for all four bar types |
  | `kIngredient1/2/3` | filling options 0/1/2 (layer 1) for all bar types |
  | `kTopping1/2/3` | topping options 0/1/2 (layer 4) for all bar types |
  | `kInfusion1/2/3` | infusion options 0/1/2 (layer 3) for bar type 3 only |
  | `kPress1/2/3` | bar types 0 (bar) / 1 (truffle) / 2 (novelty) available |
  | `kInfusion1` | also bar type 3 (infusion) available |

* `CanCreate(chocolate)`: true iff pool B contains the chocolate's `barType` and, for every layer slot 1..4 present, pool B for that `[barType][slot]` contains the layer's `option`. Used by `VCustomer` to decide whether an order is makeable (VCustomer lines ~785/1524; behaviour of the caller is the customer analyst's).
* `SetMaxLayers(n)`: clamp to 1..3 (default 3 at construction). Story mode calls it with the level's `maxlayers` (`CSLevel+0x44`, from level XML). `UpdateMaxLayers(minutes)` (endless): `>= endless_three_layers` -> 3, `< endless_two_layers` -> 1, else 2 (both values are 0 in `values.xml`, so endless is always 3).
* **Display influence** (`GetDisplayInfluence(layerIndex, default)`): the display case (`VDisplay`) registers each displayed non-bucket chocolate with `AddDisplayChocolate` (sets: key 0 = its mix option, key 2 = its barType, keys 1/3/4 = the options of its layers 1/3/4; the bucket slot excluded) and `RemoveDisplayChocolate` erases them. These are **sets, not counts**: removing one chocolate erases that option even if another displayed chocolate has it (**F** quirk). `GetDisplayInfluence`: if the set for `layerIndex` is non-empty, roll `rand % 101` (`0x65`); if `roll <= pct` where `pct` = value `display_influence_mix` (30) / `_ingredient` (20) / `_shape` (30) / `_infusion` (20) / `_topping` (20) for index 0/1/2/3/4, return a uniformly chosen member of the set (index `rand % size`), else return `default`. **It is applied even when the order specifies the option explicitly** (see §2.6).
* `ClearDisplayChocolates` empties the sets; `ClearVendor` (the start of `LoadFromTable`) empties them too, with the prototypes, pool A and a string-to-int map (+0x38) that nothing else reads. The roll is `rand % 101 <= pct` (single precision), so a percentage of 0 still hits on a roll of 0; a one-member set needs no second draw. An empty set for the index gives the default without a draw.

### 2.5 `VOrderInfo` (36 bytes of ints) - the order descriptor **F**

| Offset | Meaning | Default | XML attribute |
|---|---|---|---|
| +0 | order type (0 chocolate, 1 sauce, 2 drink) | -1 (default ctor) or ctor arg | `type` |
| +4 | shape/barType; **-2 = random** | -2 | `shape` (unknown string -> -2) |
| +8 | layer 0 (bucket) option; never set by the parser | -2 | - |
| +0xc | layer 1 (ingredient) option | -2 | `<layer type="kIngredientLayer" option=N/>` |
| +0x10 | layer 2 (chocolate) option; parsed but **ignored by `GetChocolate`** | -2 | `<layer type="kChocolateLayer" .../>` |
| +0x14 | layer 3 (infusion) option | -2 | `<layer type="kInfusionLayer" .../>` |
| +0x18 | layer 4 (topping) option | -2 | `<layer type="kToppingLayer" .../>` |
| +0x1c | mix option (0/1/2) | -1 | `mix` (non-empty -> `to_int`) |
| +0x20 | wanted layer count | -1 | `layers` (see below) |

* Meaning of per-layer values: **-2 = layer not requested**, **-1 = layer requested with a random option**, **>= 0 = requested with that option**. The parser sets +8+4*layer only for layer types > 0: no `option` attribute (or empty) -> -1, else `to_int`.
* `layers` attribute: parsed as `n = to_int`; result = 1 if `n <= 1`, else `min(n, 3)` (the file reads the attribute several times; net effect clamp 1..3; a missing attribute gives 1). (`VOrderInfo::ParseNode`.) No shipped level uses `layers`; shipped XML uses only `type`, `shape`, `mix`, and `<layer type option>`.
* Sauce and drink orders (`<order type="kSauce"/>`, `<order type="kDrink"/>`) carry no other fields (types 1 and 2).
* Shipped order forms (F, counted in `levels/*.xml`): 1,921 chocolate, 731 sauce, 130 drink orders; drinks occur only in levels 12-20 and 44-50 (Grytviken and Kamchatka) and match the `kDrink1` enable levels (§7.4).

### 2.6 Building a chocolate from an order: `VChocolateVendor::GetChocolate(info, flag)` **F** (structure), **I** where noted

Called by `VCustomer::AddOrder` (scripted type-0 orders, `flag` true), `VCustomer::GenerateOrders` (random, `flag` false) and `VDelivery::GenerateOrder` (`flag` false for the level's zeppelin order, true for the fully random fallback; read from the disassembly's pushed constants). `flag` selects the pool the layer draws come from (false -> pool A, true -> pool B; identical in practice, §2.4); the random shape always comes from the B pool's bar set. Returns null, before a `VChocolate` is made (so the chocolate counter does not move), if `info.type != 0` (an unknown type, -1, ends here too).

1. Budget `n = info.layers`; if `n < 1`: `n = 1` when `maxLayers < 2`, else `rand % maxLayers + 1`.
2. `barType = info.shape`; if `barType != 3` (infusion): `barType = GetDisplayInfluence(2, barType)` (**an explicit shape can be replaced by a displayed one**). If `barType == -2`: random member of the available bar-type set (`+0x80`), or -1 if the set is empty.
3. Shape/mix layer (slot 2): if `info.mix < 0`: `AddLayer(GetRandomLayer(barType, 2, flag))`; else `AddLayer(barType, 2, GetDisplayInfluence(0, info.mix))` (explicit mix can also be replaced by a displayed mix). `n -= 1`.
4. For slots 1, 3, 4 in order (slot 2 is skipped):
   * slot value -2 (not requested): for slot 3 on a **bar type 3** (infusion) chocolate an infusion layer is always added (`GetRandomLayer(3, 3, flag)`, `n -= 1`); otherwise nothing.
   * requested (>= -1): skip slot 1 when `barType == 3` (infusion bars have no filling); skip slot 3 when `barType != 3`; value -1 -> `AddLayer(GetRandomLayer(barType, slot, flag))`, else `AddLayer(barType, slot, GetDisplayInfluence(slot, value))`; `n -= 1` unless the layer came back null (the code adds `-1 + (layer == null)`).
5. After the loop (F, disassembly and control-flow graph): if `n < 1` return. Else the leftover budget buys random layers, **without looking at what the slots already hold**: if `info.filling < 0` (-2 not asked for, or -1 asked at random, so a random filling asked for in the loop is built a second time here) and `barType != 3`: when `n < 2` and `info.topping < 0` one coin is drawn (`rand & 1`) and an odd one skips the filling; otherwise (no coin when `n >= 2` or when the topping was pinned) a random filling (`GetRandomLayer(barType, 1, flag)`) is added over any earlier one and, with `n - 1 < 1`, the function returns. Then `info.topping >= 0` returns (pinned toppings are final); else a random topping (`GetRandomLayer(barType, 4, flag)`) is added the same way. So with a pinned order there is never any extra layer, and a random order of budget 3 is a random shape, a random filling and a random topping in that order.
6. `GetRandomLayer(barType, layer, flag)`: `idx = (layer == 2) ? 0 : layer`; `opt = GetDisplayInfluence(idx, -1)`; if `opt < 0` pick a uniform random member of pool `[barType][layer]` (-1 if empty); return a clone of the prototype `[barType][layer][opt]` (null if none).

Consequences: **a level-XML chocolate order is an exact specification of (shape, mix, and the listed layers) and nothing else** - e.g. `<order type="kChocolate" shape="kBar" mix="0"/>` asks for a plain dark bar with no filling and no topping (budget 0 after the shape). Listed `<layer>`s are always added regardless of the budget.

## 3. Carriables and carrying

* **F** `VCarriable(hands)` (0x214 bytes, `VObject` subclass): the only member is `+0x210 = hands needed`. Every carriable in the game uses `hands = 1`: `VChocolate`, the sauce bottle, the drink cup, and the "wanted item" objects a customer holds. (`VCarriable::Create(1)` in `VSauce::AttachItem`, `VDrink::AttachItem`, `VCustomer::AddOrder`; `VChocolate` ctor passes 1.) Two-hand handling exists in `VPlayer::AttachObject` (a 2-hand object fills both slots and gets anchor `bucket`) but no shipped object uses it.
* **F** The type tag is `VObject+0x194`: `"chocolate"` for `VChocolate`; `"sauce"` / `"drink"` copied from the `type` key of the machine style's `bottle` child by `CopyTo` (`machine_sauce_style.children.bottle.type = "sauce"`, `machine_drink_style...type = "drink"`).
* **F (interface, `VPlayer`)**: two hand slots (`+0x2a8`, `+0x2b0`); `NumHandsFree` = number of empty slots; `AttachObject(carriable)` fails (returns false) if `hands > NumHandsFree`, else fills slot `+0x2a8` first, then `+0x2b0`. So **the player holds at most two items, any mix of chocolates, sauces and drinks.** Hand ids for `DetachObject`/`GetObject`: **2 and 3 = slot `+0x2a8`, 1 = slot `+0x2b0`**, 0 = "preferred". `GetObject(hand, &outHand)` (F): hand 0 asks the preferred slot (`+0x2a4`, 1 or 2) and, when that gave nothing, asks again; an empty slot turns the preference (2 <-> 1) and gives null; a found item writes its hand id (2 or 1) to `outHand`. `GetObject<VChocolate>` returns null for a sauce or a drink but still writes `outHand` and does not turn the preference.
* **F** Decayed chocolates are dropped from the hands by `VPlayer::Update` every frame (§2.2).
* **F** There is **no combining of carriables** (no "pour sauce on a chocolate"): sauce and drink are stand-alone items. "Combining" is purely layer addition on a chocolate inside a machine (§5). Invalid combinations are simply refused by `CanAddLayer`/`ProcessChocolate` returning false (nothing happens; the option stays selected, see §4.3).
* **F** Who accepts what:
  * Customer: `VCustomer::ServeCustomer(item)` - type tags must be equal (`item+0x194 == wanted+0x194`, else false); chocolate -> `served.Compare(wanted)` and price `served.GetCost()`; sauce -> `sauce_price` (15); drink -> `drink_price` (15); all three add to the customer's accumulating payout (`VCustomer+0x2a4`). On success: `AddCombo(2)`, patience `+= order_patience_bonus` (20.0), the next wanted item becomes current, `customer_leaves.ogg` plays and the game-state `++OrderCompleted` is raised. (Payout/tips are the customer analyst's.)
  * Disposal: takes anything (§5.9). Display case: takes any carriable (§5.11). Conveyor: chocolates that are not buckets only (§5.10). Machines: see §5.

## 4. `VMachine` base class

### 4.1 Fields (VMachine : VSuspendable : VObject : VSprite; 0x27c bytes) **F**

| Offset | Field | Notes |
|---|---|---|
| +0x218 | `suspended` (VSuspendable) | true while a `Suspend`/`SuspendWhilePlaying` wait is pending |
| +0x224 | `weak_ptr<VStation>` | set by `SetStation` |
| +0x22c | `option` | selected choice id, **-1 = none** |
| +0x230 | `enabled` | at least one selection is available (set by the subclass `Init`) |
| +0x234 | `map<str, shared_ptr<VMachineButton>>` | keys are the lower-cased entries of `selections` (`button1`..) |
| +0x24c | `shared_ptr<VChocolate>` held chocolate | |
| +0x254 | `processed` | set true by `Activate`, false by `Attach/DetachChocolate` |
| +0x258 | number of `selections` entries | counted in `LoadFromTable` |
| +0x25c / +0x260 | forward / reverse animation names | set to `"DoAnim"` / `"Reverse"` by `Activate` |
| +0x264 | `state` (sequence step) | 0 idle; 1.. per machine |
| +0x268 | `locked` | true from `Activate` until `Unlock` |
| +0x26c | hover-glow effect | |
| +0x274 | frame delay (float ms) | **30.0** default; `VMixer` sets **20.0** |

`VMachine`'s constructor also sets the sprite depth (`spriteLayer`) to 7 and `clickable`; `VMachine::SetStation` makes it not a leaf and clickable.
| +0x278 | `initialised` | `VMachine::Init` runs its body once |

Virtual slots used (derived from call sites; **I** for the names): +0x124 `Reset`, +0x128 `Suspend(ms)`, +0x12c `SuspendWhilePlaying(sprite)`, +0x130 `OnResume`, +0x134 `ProcessChocolate`, +0x138 `ChooseOption(id)`, +0x13c `Activate`, +0x140 `OnArrival(player)`, +0x144 `AttachChocolate`, +0x148 `DetachChocolate`.

### 4.2 `VSuspendable` sequencing (how all machine animations are chained) **F**

* `Suspend(ms)`: record `now`, set `suspended`, remember `ms`. `SuspendWhilePlaying(sprite)`: set `suspended` and remember the sprite.
* `VSuspendable::Update` (every frame): if `suspended` and (the watched sprite is not playing or is done, or `now - start >= ms`) -> clear the wait fields and call `OnResume()`; if `OnResume` returns true, `suspended` is cleared; if it returns false (it scheduled another wait) `suspended` stays set. The first update after a soft pause moves the stored start time to `now` less the time the wait had already run before the pause began, so the paused time is not counted; during the pause the wait is not stopped (one that would end inside the pause resumes inside it). `VSuspendable::Reset`: when suspended it clears the flag, calls `OnResume` (+0x130) and tells the tutorial listeners; then `CleanUpListeners` and the wait fields are cleared.
* Each machine's `OnResume` is a step switch on `state` (+0x264) and returns 0 after scheduling the next wait, 1 when finished. After the final step the machine calls `Reset()`.
* `VMachine::OnResume` (base) = `Reset()`, return 1.

### 4.3 Click, buttons, walking **F** (flow), **I** where marked

1. **Machine buttons** (`VMachineButton : VButton`, z-layer 10, name `"machine button"`): created in `VMachine::LoadFromTable` for every entry of the Lua `selections` table (key lower-cased -> map key). Per entry (all keys are read by `VMachineButton`/`VObject`): `id` (the option number passed to `ChooseOption`), `x`,`y` (screen position), `name`, `baseDir`, `assets` (the shared `machine_button_assets`), `icon` (asset state name in group `icon`, a filling/topping/shape name such as `mint`, `bar`, `dark`), optional `stripe` (state in group `stripe`).
   * `Init` (not `VObject::Init`; the button's base art comes from `SetUpgraded`): `AddAssetLeaf("icon", <style string icon>, 1)`; if `icon == "dark"` its colour is set to (0.5, 0.5, 1.0, 1.0). If `stripe` is non-empty, `AddAssetLeaf("stripe", <style string stripe>, 1)` makes a second child and the **icon is hidden** (a button with a stripe shows the stripe).
   * `SetUpgraded(bool)` (called by the machine `Init` with the quality-upgrade state): first **removes every child of the button** (`RemoveChildren`: the icon, the stripe and the label go), switches the button asset to group `button`, state `default`, remembers the flag, and if true sets the button colour to (0, 1, 1, 1) (cyan tint). **I:** visual marker for "quality upgrade owned". The machines' `Init` then calls the button's `Init` again for each shown button, which adds the icon and stripe children back (the first `Init`, at load, is the one that is removed).
   * Hover: `OnMouseEnter`/`OnMouseLeave` forward synthetic events (8 / 0x10) to the machine (so the machine's glow follows); hovering more than **501 ms** (`OnMouseHover(ms >= 0x1f5)`) swaps the **stripe for the icon** (`SwapVisual(true)`: the icon is shown and fades in over **300 ms**, the stripe fades out), leaving swaps back (`SwapVisual(false)`: the stripe is shown and fades in, the icon fades out); the swap needs both children, so a button without a stripe does nothing; each fade starts from the alpha of the one before it; `Update` hides a child whose fade-out has ended. (`VAlphaEffect::Create`'s first argument is *fade in*.) The buttons of the mixer and the press have no stripe in the shipped data, so their icon is always shown. A `VHoverGlow(0.05, ...)` is added to the button; the machine itself gets `VHoverGlow(0.15, ...)` and each button has the machine's glow removed.
   * `OnClick`: plays `audio/sfx/buttonclick.ogg`, calls `machine.ChooseOption(id)`.
   * Assets `button2` and `frame` (`gold_frame`) in `machine_button_assets` are never referenced by the C++ (**F**: no string use in `VMachineButton`).
2. **`VMachine::ChooseOption(id)`**: if `!locked` then `option = id`; then raises a synthetic **click** event (event id 4) on the machine regardless. `VSprite::DoEvent(4)` calls the machine's `OnClick`.
3. **`VMachine::OnClick`** -> `VStation::OnMachineClick(name)`: pushes the machine's name on the station's pending list (`+0x258`), then raises event 4 on the station -> `VStation::OnClick`: picks the station's `playerPoint` `ZonePoint` nearest (Euclidean) to the player's current position (read via vtable +0x94 = `VSprite::GetPosition`) and calls the player's vtable +0x13c = **`VCharacter::QueueMovePoint(point)`** (not `TravelTo`); raises game state `++TotalValidClicks`. The point is appended to the player's move queue: a click while the player is walking does not replace the trip (the current walk and its arrival finish first, then queued points run in order; the queue is never cleared). **So pressing a button or clicking a machine means "walk to the station, then act"; the choice (`option`) is stored in the machine immediately.**
4. **On arrival** (`VPlayer::AtDestination` -> `VStation::OnArrival(player, zonepoint)`): if the station has exactly one enabled machine (`UpdateAvailable` counts machines whose `+0x230` is true; exactly 1 -> that machine is the station's "single machine", `+0x248`), call its `OnArrival(player)`; otherwise call `OnArrival` on each machine named in the pending click list (this is how station 8's two/three machines sharing one position resolve which was clicked). The customer on a counter station also gets `OnArrival`.
5. `VMachine::OnArrival(player)` (the base behaviour, used for output pickup): if a chocolate is held, the machine is not locked, and `player.NumHandsFree >= chocolate.hands (1)`: `DetachChocolate(true)` (plays `pickup_drop.ogg`) and `player.AttachObject(chocolate)`.
6. **Option lifetime**: `option` stays until used (`Activate` sets it to -1), or until the subclass `OnArrival` cannot use it (then it is reset to -1, see each machine). `VMachine::Reset` ends with: if `option >= 0` and `!processed`, call `ProcessChocolate` again (so a choice made while the machine was busy is applied right after).
7. **Walk target when the player is already there** (F, `sprites` 5.1): the queued point is popped on the next `VCharacter::Update` and `VPlayer::TravelTo` finds the sprite exactly on the point, so `AtDestination` -> `VStation::OnArrival` runs at once with no walk (and the button/option stored in step 3 is consumed in that frame).
8. **What happens when output is not collected**: nothing; the machine keeps the item indefinitely (it blocks new input except where noted: Press/Infuse/Ingredient/Topping allow a *swap*; the mixer needs the previous bucket collected, §5.1). In Gaberoun decay eventually removes it (§2.2). There is no queue: the machine holds at most one chocolate and one selected `option`.

### 4.4 `VMachine::GetWhichHand(player, layer)` and `ShopWindow::FindOrderWithLayer` **F** (structure), **I** (use)

Used by the ingredient and topping machines to choose which carried chocolate to feed. Let `c0 = GetObject<VChocolate>(0)` (with its hand id `i0`) and `c1 = GetObject<VChocolate>(i0 != 2 ? 2 : 1 ...)` (the other hand; the decompile writes `(i0 != 2) + 1`). If neither can `CanAddLayer(layer)` -> -1. If only one can -> its hand. If both can: ask `ShopWindow::FindOrderWithLayer(c0, c1, layer, option)`: iterate stations `station01..station06` that are counters (`stationType == 1`) with a customer whose current wanted item exists; for the wanted chocolate `w` test `w.CompareWithLayer(c0, layer, option)` then `w.CompareWithLayer(c1, ...)`; the first chocolate that "heads toward" an order wins. The result hand is the hand of the chocolate `FindOrderWithLayer` returns when it is `c0` or `c1`; **any other result gives -1, not `c0`** (F: `VMachine::GetWhichHand`'s last branch). `ShopWindow::FindOrderWithLayer(c0, c1, layer, option)` (F, decompile and disassembly): for `station01..06` in order, a station that is a counter with a customer attached whose wanted item (`VCustomer+0x314`) is not null (the customer's state is not read) and is a chocolate `w`: `w.CompareWithLayer(c0, layer, option)` true returns `c0`, else `w.CompareWithLayer(c1, ...)` true returns `c1`; after the six counters the answer is `c0` when it is not null, else `c1` (which may be null).

### 4.5 `VMachine::Init`, `Reset`, `Activate`, `Update` **F**

* `Init` (once): set asset `base`/`default`, call `Reset`, `SetDelay((uint)+0x274, recursive = true)` (sets the frame delay on this sprite and every child; implemented as running `delay(N);` in each sprite's script), `Preload("all","all")`. Subclass `Init`s call it only when `enabled` (or in endless mode, where every machine is initialised but hidden unless enabled), then `SetVisible(enabled)`.
* `Reset`: `state = 0`, `Unlock()`, `+0x218 = 0`, `VSuspendable::Reset`; for every child sprite: `anim_*` children are hidden and stopped, `static_*` children shown; then the pending-option rule above.
* `Activate`: hides all `static_*` children; **steam booster**: sets `+0x25c = "DoAnim"`, `+0x260 = "Reverse"` and calls `SetDelay(+0x274 * (kSteam1 active ? 0.9 : 1.0), true)` (category `gameplay`); calls the station's `VSuspendable::Suspend(0)` (vtable +0x128: a `VStation` is a `VSuspendable`, and its `Update` is `VSprite::Update`, so nothing ever resumes it by the clock); `state = 1`, `locked = true`, `processed = true`, `option = -1`. **While a machine works its station is suspended, and `VStation::OnArrival` does nothing for a suspended station** (no machine, customer or tutorial notification; only the facing at the end still applies).
  * Only animation scripts that call `getdelay()` follow the delay; scripts with a literal `delay(60)` etc. are unaffected (the sauce/drink and delivery scripts use literals, so the booster does not speed them; `DoAnim_fast`/`Reverse_fast` variants exist in those `.anm` but nothing selects them).
* `Unlock` (not virtual): if locked: `locked = false` and `station.Reset()` (vtable +0x124 = `VSuspendable::Reset`: the station is no longer suspended, so arrivals are taken again). `VPress::Unlock` and `VInfuse`'s are different functions, bound only to the scripts' `unlock()`; `VMachine::Reset` and `VMixer::OnResume` call the base `Unlock`.
* `AttachChocolate(choc, spriteLayer, playSound)`: `processed = false`; accepts only if the slot is empty or already holds that chocolate and `choc` non-null; stores it, sets its sprite z-layer to `spriteLayer` (2 for the mixer), adds it as a child, sets its anchor to `"chocolate"` (the `.anm` named anchor in the machine's base sheet places it), plays `pickup_drop.ogg` if `playSound`. Returns success.
* `DetachChocolate(playSound)`: `processed = false`, removes the child, clears the slot, plays `pickup_drop.ogg` if asked, returns the chocolate (or null).
* `Update`: if the held chocolate `HasDecayed()` -> `DetachChocolate(false)` (it is dropped silently). Then `VSuspendable::Update`.
* Style keys read through `VObject::LoadFromTable` (F, strings in `VObject::LoadFromTable`): `leaf`, `visible`, `mouse`, `clickable`, `continuous`, `layer`, `type`, `spriteLayer`, `name`, `anchor`, `x`, `y`, `vflip`, `hflip`, `blend`, `alpha`, `tint`, `autochild`, `autoAsset`, `autoState` (plus `assets`, `children`, `scale`, `baseDir` handled in the same loader). The machine styles' `chocolate = {x,y}` entry is **not read by any string in `VMachine`/`VObject`** (`F`: absent from the strings of those functions); positions come from the `.anm` anchor `chocolate`/`bucket` (**I**: dead data).

## 5. The machines

Common: all but the sauce/drink machines are visible only if at least one of their `selections` is enabled by upgrades (`enabled`), listen to hover with the glow above, and use the sequence mechanism of §4.2. Timings below use **d** = frame delay (30 ms; mixer 20 ms; with the steam booster `0.9 d`, i.e. 27 / 18 ms) for scripts that call `getdelay()`; literal-delay scripts are written as `N frames x ms`. All frame counts/delays were read from the `SCRP` chunk of each `.anm` (`re/greatcc/scratch/machines/*.txt`); a "frame" is one entry of the `frames{}` list (repeats count). Sounds are `audio/sfx/<name>.ogg`.

Animation callbacks: `unlock()` and `drop()` in a `.anm` script are Lua functions that `VPress::Init` / `VInfuse::Init` register in the sprite's script (`unlock` -> `Unlock`, `drop` -> `Drop`); `Drop` gives the temporary fill layer `VSpriteMove(position + (0, 46), 200 ms, accel 0, delay 0)` and `VWipeEffect(direction 1, reveal false, 200 ms)` and plays `barrel_trap_door.ogg` (F, disassembly).

### 5.1 Mixer (`kMixer`, station 14) - makes a bucket **F**

* Buttons: 3, ids 0/1/2 = dark/milk/white, enabled by `kMixer1/2/3` (`VMixer::Init` loops `i = 1..N`: `button<i>` visible iff `kMixer<i>` active; `SetUpgraded(kMixerQ<i>)`). Child names: `anim_arm`, `anim_tube` (assets `option_0..2`), `anim_spray` (`option_0..2`), `panel`. Default frame delay **20 ms**.
* **Input**: nothing. `ProcessChocolate` needs `option >= 0` and an **empty** machine: `GetEmptyChocolate(option, decayEnabled=true)`, `AttachChocolate(bucket, 2, true)`, remember the bucket's `icon` sprite (+0x27c), then `Activate`. (No `VValue` stat here.)
* `OnArrival`: first, if a bucket is held and `processed`: `VMachine::OnArrival` (hand it over if a hand is free, only when unlocked); then, if unlocked and the machine itself is not suspended, `ProcessChocolate()` (a visit before the sequence has wound down collects the bucket but starts nothing; `Reset` then applies the choice) (so one visit can collect the old bucket and start a new one). Because `ProcessChocolate` needs an empty slot, an uncollected bucket **blocks** the mixer.
* **Sequence** (state: action, duration):
  1. `Activate` (state 1): tube and spray sprites get state `option_<mix>` through `SwitchState` (their asset has no `default` state); for mix 0 (dark) both are tinted (0.5,0.5,0.5,1), otherwise white; `anim_arm` visible, plays `DoAnim`, `robot_arm.ogg`; wait for the arm: 31 frames x d = **620 ms** (558 steam).
  2. state 2: `spray.ogg`; tube visible, `DoAnim`, wait for it: 7 frames x 60 = **420 ms**.
  3. state 3: spray visible, plays `DoAnim` **continuously** (5 frames x 60 = 300 ms loop); the bucket's icon (+0x27c) is shown and wiped in with `VWipeEffect(1,1,800)`; `Suspend(800 ms)`. (`Activate` hides the icon and would tint it for `option == 0`, but `VMachine::Activate` has just reset `option` to -1, so the tint never applies.)
  4. state 4: **`Unlock`** (the bucket can now be picked up); spray stopped and hidden; tube plays `Reverse` (420 ms), wait.
  5. state 5: `robot_arm_2.ogg`; tube hidden; arm plays `Reverse` (620 ms), wait.
  6. `Reset`.
  Total **2,880 ms** (steam 2,756 ms); `Unlock` at **1,840 ms** (steam 1,778 ms).
* Stats: none.

### 5.2 Ingredient (`kIngredient`, station 13) - adds a filling to a bucket **F**

* Buttons: 3 (ids 0/1/2; Lua `icon` = filling icon name, `stripe` = colour swatch asset name), enabled by `kIngredient<i>`, upgraded flag `kIngredientQ<i>`. Also sets `static_top1/2` and `anim_top1/2` to asset state `upgrade<i>` for each enabled `i` (so the number of lit tanks follows the highest enabled index).
* `ProcessChocolate`: needs a held chocolate not suspended, `chocolate.CanAddLayer(1)` (a bare bucket) and `option >= 0` -> `chocolate.AddIngredient(option)`, remember the bucket's `icon.overlay` sprite (+0x27c), `Activate`.
* `OnArrival` (**F**, disassembly of `VIngredient::OnArrival` and `VTopping::OnArrival`, which are the same code with `GetWhichHand` layer 1 and 4): nothing while locked. `ProcessChocolate()` first, and done if it started. Else, with `option < 0`: a machine that holds a chocolate hands it to the avatar when a hand is free (`DetachChocolate(true)`, `AttachObject`); an empty machine takes the chocolate of the hand `GetWhichHand(player, layer)` names (`DetachObject(hand)`, `AttachChocolate(choc, spriteLayer 1, true)`); in every case that branch ends with `option = -1`. The empty-machine branch calls `DetachObject` with the `GetWhichHand` result untested, and `DetachObject` takes its hand as unsigned (`eHand`; the disassembly compares with `cmp $1; jbe`), so -1 takes the "no item" path: it turns the preferred hand (`+0x2a4`, 2 to 1 or else to 2) and returns null, nothing leaves her hands; the empty machine has nothing to attach. With `option >= 0` (processing failed): `hand = GetWhichHand(player, layer)`; `-1` clears `option`; else `DetachChocolate(true)` (the old output, possibly null), `DetachObject(hand)`, `AttachChocolate(carried, spriteLayer 0, true)`, `AttachObject(old output)`, `ProcessChocolate()` (the swap uses sprite layer 0, the empty-machine path 1). A choice with an unfitting chocolate in the machine and none in hand therefore clears the choice and hands nothing back on that visit.
* **Sequence**: `Activate`: `anim_intro`, `anim_top1`, `anim_top2` visible and play `DoAnim`; overlay icon hidden; `machine_noise.ogg`; wait for `anim_intro`: 22 ticks (11 frames, each doubled) x d = **660 ms**. State 2: overlay icon shown with `VAlphaEffect(fade in, 1.0, **1000 ms**)`; `anim_intro` hidden, `anim_pump` shown and plays `DoAnim` continuously (27 frames x d); `glug_machine.ogg`; `Suspend(1000 ms)`. State 3: `anim_pump` hidden, `anim_intro` plays `Reverse` (660 ms) with the two `anim_top*`; **`Unlock` at the start of this step**; wait for `anim_intro`. Then `Reset` (`OnResume` returns `!suspended`).
* Total **2,320 ms** (steam 2,188 ms); `Unlock` at **1,660 ms** (steam 1,594 ms). Stats: none.

### 5.3 Press (`kBarPress`, station 12) - shapes a bucket into bar / truffle / novelty **F**

* Buttons: 3, ids 0/1/2 = bar / truffle / novelty (Lua `icon = "bar"`, `"truffle"`, `<novelty>`), enabled by `kPress<i>` (kPress1 is auto-enabled from the first level of every location, kPress2/3 per location), `SetUpgraded(false)` always.
* `ProcessChocolate`: needs not suspended, a held chocolate with `CanAddLayer(2)` (bucket, with or without filling), `option >= 0`. Remembers `shape = option` (+0x27c); creates a temporary empty layer `VChocolateLayer(0, -1, -1)` with sprite layer 1, **copies the bucket layer into it** (`CopyTo` takes the bucket's own sprite layer 0 over the 1; the fill/barrel look used by `anim_fill`), adds it to the machine under anchor `bucket`; stores its `icon` (+0x28c) for the wipe; names the fill animation `DoAnim_<mixOption>`; **adds the shape layer to the held chocolate now** (`chocolate.AddLayer(barType = option, layer 2, option = mixOption)`; the vendor must have the prototype `[option][2][mix]`), hides the chocolate until `Unlock`, resets its anchor to `chocolate`, then `Activate`. Value `chocolates_made_this_level` += 1.
* `OnArrival` (F, disassembly): when not locked: `ProcessChocolate()` first, and done if it started. Otherwise `c = GetObject<VChocolate>(0, &hand)`; if `c` is null or `!c.CanAddLayer(2)`, `c = GetObject<VChocolate>(hand != 2 ? 2 : 1, &hand)` (the other hand). If the machine holds nothing or a processed output, and `c` exists and `c.CanAddLayer(2)`: `DetachChocolate(true)` from the machine, `DetachObject(hand)` from the avatar, `AttachChocolate(c, spriteLayer 1, true)`, `c.SetAnchor("bucket")`, `AttachObject(old output)` (null when the machine was empty), `ProcessChocolate()`. In every other case (locked, no suitable chocolate, the machine holds an unprocessed bucket): `option = -1`, then `VMachine::OnArrival` (pick up the output). A bucket put in with no choice therefore stays in the press until a later choice and visit.
* **Sequence**: `Activate`: after the base `Activate` has hidden every `static_*` child, `static_door`, `static_largeweight` and `static_wheel` are shown again; the fill icon is wiped (`VWipeEffect(0,0,300)`), `anim_fill` plays `DoAnim_<mix>` (6 frames x 60 = 360 ms per mix, frames 1-6 / 7-12 / 13-18); `Suspend(300 ms)`. State 2: statics hidden; `anim_door` plays `DoDoor`; `anim_largeweight` plays `DoAnim`; `anim_wheel` plays `DoAnim`; wait for `anim_largeweight`.
  * `press_large.DoAnim`: frames 1-5 (5 x d), **`unlock()`**, frames 6-31 (26 x d). `Unlock` = `VMachine::Unlock`, shows the shaped chocolate again, hides `anim_fill`, plays `stamp.ogg`.
  * `press_misc.DoDoor`: 6 frames x 20 ms, `drop()` (barrel falls, `barrel_trap_door.ogg`), 20 hold frames x 20 ms, 5 frames x 20 ms (total 620 ms). `VPress::Update` swaps the machine base frame (`SetFrame`) between 3 and 4: 4 when `anim_largeweight` is at frame 5, else 3 (**I**: the stamp-down pose).
  * Final `OnResume`: value `num_bars` += 1 if `shape == 0`, `num_truffles` += 1 if `shape == 1` (both persisted, saved, achievements); novelty not counted; then `Reset`.
* `VPress::Reset` (also run by `Init`): fill animation `DoAnim_0`, shape -1, the temporary fill layer is removed, the base frame is 3 (`SetCurrentFrame(3)`); then `VMachine::Reset`.
* Total **1,230 ms** (steam 1,137 ms); `Unlock` at **450 ms** (300 + 5 x d; steam 435 ms). 

### 5.4 Infusion (`kInfuse`, station 10) - turns a bucket into an infusion bar **F**

* Buttons: 3 (ids 0/1/2; icons from the location's `infusion1..3`), enabled by `kInfusion<i>`, upgraded flag `kInfusionQ<i>`. Style has an offset `x=10, y=-25`.
* `ProcessChocolate`: needs not suspended, held chocolate with `CanAddLayer(3)` (a bucket **without** filling), `option >= 0`. Copies the bucket layer into a temporary fill sprite (+0x27c, sprite layer 2, anchor `bucket`), stores its icon (+0x284) for the wipe; calls `chocolate.AddLayer(barType 3, layer 3, option)` (which removes the bucket and auto-creates the infusion shape layer from `mixOption`); re-parents the chocolate under the `anim_top_working` child (anchor `chocolate`) so it moves with the machine animation; `Activate`. Value `chocolates_made_this_level` += 1. `OnArrival` (disassembly) is the press's swap with layer 3: `c` = the chocolate of the preferred hand, else of the other, that `CanAddLayer(3)`; when the machine is empty or its output is `processed` and `c` exists: `DetachChocolate(true)`, `DetachObject(hand)`, `AttachChocolate(c, spriteLayer 2, true)`, `c.SetAnchor("bucket")`, `AttachObject(old output)`, `ProcessChocolate()`; otherwise `option = -1` and `VMachine::OnArrival`.
* **Sequence**: `Activate`: statics hidden; fill icon wiped (300 ms); `Suspend(300)`; `infusion_machine.ogg`. State 2: `anim_top_working` (`infuse_misc.DoAnim`: frames 8-32 (25 x d), **`unlock()`**, frames 33-38 (6 x d)), `anim_base` (`base_working.DoAnim`: 31 x d) and `anim_door` (`DoDoor`: 7 x 20 ms, `drop()`, 27 x 20 ms) play; wait for `anim_base` (**I**: the third `SuspendWhilePlaying` target string is `anim_base`). Final step: if `processed`, the chocolate is moved back from `anim_top_working` to the machine (anchor `chocolate`); value `num_infusions` += 1 (saved); `Reset`.
* Total **1,230 ms** (steam 1,137); `Unlock` at **1,050 ms** (300 + 25 x d). The `VInfuse::Update` is just the base.

### 5.5 Topping (`kTopping`, station 11) - adds a topping to a shaped chocolate **F**

* Buttons 3 (ids 0/1/2, icons `topping1..3`), enabled `kTopping<i>`, upgraded flag `kToppingQ<i>`.
* `ProcessChocolate`: held chocolate `CanAddLayer(4)` and `option >= 0` -> `chocolate.AddLayer(barType = chocolate.barType, layer 4, option)`, keep the new layer (+0x27c), `Activate`. No stat value.
* `OnArrival`: same hand-over / `GetWhichHand(player, layer 4)` / swap structure as the ingredient machine (§5.2). The machine never "owns" the chocolate across visits except as the output slot.
* **Sequence**: `Activate`: `anim_funnel` plays `DoAnim` (`topping_misc`: 31 frames x d = 930 ms, not waited on), `anim_particles` plays `DoParticles` (30 frames x 20 ms = **600 ms**; alpha set to 1.0), `topping_machine.ogg` and `topping.ogg` together; the new topping layer fades in with `VAlphaEffect(in, 1.0, **810 ms** (0x32a))`; wait for `anim_particles`. State 2: particles fade out (300 ms); `Suspend(300)`. Then `processed = true`, `Reset` (which unlocks).
* Total **900 ms** (not affected by the steam booster since the waits are literal); `Unlock` at the end (900 ms).

### 5.6 Sauce (`kSauce`) and Drink (`kDrink`) machines **F**

* Lives on station 8 (`stationType = kEmpty`; the three machines are separate `machines` entries whose own style `stationType` is `kSauce`/`kDrink`): `machine_sauce` (default position), `machine_sauce_2` (`x=30,y=20`, second sauce station) and `machine_drink` (`x=30,y=20`, same spot as sauce 2).
* Enabling (`Init`): `machine_sauce` (name != `machine_sauce_2`): `kSauce1`. `machine_sauce_2`: `kSauce3` **and not** `kDrink1`. `machine_drink`: `kDrink1`. Disabled machines are hidden (except endless mode keeps them initialised). Initial bottle: on the first `Init` (`initialised == 0`) the cooldown is set to 0 and a bottle is created and shown (a full bottle at level start).
* **Bottle**: `AttachItem` creates a `VCarriable(1)`, copies the machine style's `bottle` child (type tag `"sauce"` / `"drink"`, sprite `sauce_bottle` / `drink_cup` with an animated `steam` child on the cup) into it, adds it as a child under anchor `bottle` (`sauce_base.anm` anchor). `Refill` = (if not suspended) `AttachItem`, hide it, `Activate`.
* **`OnArrival`**: needs not locked, a bottle present, and `NumHandsFree > 0`: the bottle goes to the player's hand; machine slot cleared; **cooldown starts**: `+0x284 = GetInt(sauce_cooldown, 5000)` ms and `+0x28c = GetReal(sauce_cooldown, 5000)` (the total used for the progress bar); `static_fill` hidden, `anim_fill` shown. (Drink: same with `drink_cooldown`; its fields sit 4 bytes lower.) With shipped values: `sauce_cooldown` **15000 ms**, **-0.5** with `kSauce2` (7500 ms); `drink_cooldown` 15000 (the `kDrink2` modifier names an upgrade that no `upgrades.xml` defines, so it never applies).
* **`Update`** (cooldown > 0): `cooldown -= elapsedMs`; if the value's current total changed (upgrade bought mid-cooldown) the remaining time is rescaled by `newTotal/oldTotal`; the `anim_fill` frame is set to `int(numFrames * (1 - remaining/total))` (a fill progress bar). At `<= 0`: cooldown 0, `static_fill` frame 13 shown, `anim_fill` hidden, **`Refill()`** (new bottle appears after the animation below). Also (when `state == 1` and the clink not yet played) once `anim_dispenser` is past frame **22** `sauce_clink.ogg` plays once.
* **Sequence** (`Activate` from `Refill`; 0x13c): `static_fill` is set to frame 13 and shown (the emptied tank), `anim_dispenser` and `anim_top` are shown and play `DoAnim` (31 frames x 30 ms = **930 ms**, literal), `sauce_arm_extend.ogg`; wait for `anim_top`. State 2: dispenser hidden / static dispenser shown; `anim_fill` plays `Reverse` (32 x 30 = **960 ms**), `anim_top` hidden, `static_top` frame 2, `anim_spray` is shown and plays `DoSauceFill` (8 x 60 = 480 ms) for sauce, or `DoDrinkFill` (7 x 60 = 420 ms) for drink, plus (drink only) `anim_fill_steam` playing `DoDrinkSteam` (5 x 30 ms) **looping**; `sauce_spray.ogg`; wait for `anim_fill`. The drink's `static_top` shows frame 29 here (28 after `Reset`) where the sauce's shows 2 (1). (**I**: nothing in the C++ shows `anim_fill_steam`, and `Reset` hides every `anim_` child, so the steam never appears.) State 3: `static_fill` frame 12; spray hidden; `static_top` hidden, `anim_top` plays `Reverse` (930 ms); **the new bottle is shown and `Unlock` is called**; `sauce_arm_retract.ogg`; wait for `anim_top`. Then `processed = true`, `Reset` (`Reset` sets `static_fill` to frame 12 and shows it only while no cooldown runs, shows `anim_fill` only while one runs (frame 0 when none), and sets `static_top` to frame 1; the clink flag is cleared). `OnArrival` needs a free hand (`0 <` free for the sauce, the bottle's hand count `<=` free for the drink: the same for a one-hand bottle) and is not guarded by the machine's own wait; `Refill` does nothing while the machine is suspended, so a cooldown that ends while the machine is still finishing its sequence (the bottle was taken at the unlock, step 3) leaves the machine without a bottle for good.
  Total refill animation **2,820 ms**; bottle available after **1,890 ms**.
* Drink: `kDrink1` enable levels per location: Grytviken 12, Kamchatka 44, Namche 90 (never), Iquitos 60 (never), Krakatoa 60 (never), Gaberoun 61 (never); consistent with the levels that contain `kDrink` orders.
* Stats: `num_sauce_sold` is incremented by `VCustomer::ServeCustomer` (not here); there is no stat for drinks.

### 5.7 Delivery (`kDelivery`, station 7) - the zeppelin order box **F**

* Class `VDelivery : VMachine` (0x2bc bytes). Enabled by `kDelivery1` (or endless). `LoadFromTable`: creates the `BZeppelin` (`zeppelin` sprite, owned by `ShopWindow`) and a `VThoughtBubble` from the `bubble` style (`machine_delivery_style.bubble`, anchor `thought`, hflip, states `happy/content/neutral/annoyed/angry` = frames 0..4 of `thoughtbubble`).
* State `+0x2b8`: 0 waiting for the next order, 1 order showing, 2 boxing, 3 done awaiting pickup.
* **Timer**: `Reset` sets the wait to `GetSum(delivery_interval, delta, clamp)` where `delta = -V/2 + rand % V` (signed int division) and `V = GetInt(delivery_variance, 2)`; shipped `delivery_interval` **30000** and `delivery_variance` **5000**, both `-0.5 x` with `kDelivery2` (15000 / 2500). So the first/next order appears after ~27.5-32.5 s (13.75-16.25 s with the upgrade). `Update` counts it down only while the level is not paused, `state == 0`; at 0, if `!ShopWindow::LevelIsComplete()`: `GenerateOrder` (pop the front of the level's `<zeppelin>` order list, `GetChocolate(info, false)`; when the list is empty, a fully random chocolate `GetChocolate(VOrderInfo(0), true)`), show the thought bubble with the wanted chocolate (`VThoughtBubble::AddOrder`, tutorial check `tutorial_zepplin`), mood 1, state 1. Otherwise (level complete) the machine just `Reset`s.
* **Patience** of the order: starts 100.0, decreases **1.0 per second** (`0.001 x elapsedMs`; no patience-decay modifier applies here). Mood thresholds: `> 80` mood 1, `<= 80` 2, `<= 60` 3, `<= 40` 4, `<= 20` 5, `<= 0` the order expires: bubble hidden and `Reset` (a new interval starts, no penalty in this class).
* **`OnArrival`**: needs the machine not waiting (`suspended`, +0x218; `locked` is not read), the box output slot empty, the delivery state 1 and an order wanted: test the carried chocolates against the wanted one with `Compare` - hand 2 first, then hand 1; the first equal one is taken. Then: `zeppelin_served_this_level` += 1; for `barType` bar or novelty (0 or 2) the chocolate sprite is scaled to **0.8** (the box is small); `AttachChocolate(choc, 1, true)`; `state = 2`; bubble hidden; `Activate`; `ShopWindow::OrderComplete(choc.GetCost(), position (622, 246), no customer)` (money and combo handled there; the zeppelin pays the cost with no customer modifiers in this class).
* **Update** (**F**): with the machine disabled only `VMachine::Update` runs. While `state == 1` and the held chocolate is visible, it is hidden once `anim_boxing_back` is past frame 57 (0-based). The countdown runs while not softly paused, the delivery state is 0 and the timer is above 0: `timer -= GetElapsed()`; below 1 it is 0 and, when the level is not complete, `GenerateOrder` and (an order and a bubble) `AttachThought(true)`, mood 1, state 1; otherwise `Reset`. The bubble's fade, once it is no longer in progress: a fade-out hides and clears the bubble. In state 1 the patience falls `0.001 x elapsed` per update (not gated by the soft pause), mood by the thresholds above, and at 0 the bubble fades out (`AttachThought(false)`) and `Reset` runs. Last, `ShopWindow::WaitOnZeppelin(zeppelin != null && zeppelin.state != 0 || deliveryState != 0)`, which keeps the level from ending while a delivery is under way. `AttachThought(fadeIn)`: a `VAlphaEffect(fadeIn, 1.0, 300 ms, start, 0)` with start 0 (in) or 1 (out), or the alpha of the effect still running; fading in shows the bubble, clears it, flips the wanted chocolate horizontally (the bubble is flipped), adds it as an order (`chocolate` anchor, enabled) and raises the tutorial check `tutorial_zepplin` (the shipped table has that tutorial commented out); then the bubble's effects are cancelled and the fade added. `GenerateOrder`: nothing while something is wanted or without a vendor; the first scripted order with `GetChocolate(info, false)` (popped), or with the list empty `GetChocolate(VOrderInfo(chocolate), true)`. `Init` (once, when enabled or endless): the order list is cleared and, in story mode, the level's `<zeppelin>` orders are copied in; the airship and the bubble are initialised; `VMachine::Init`.
* **Sequence**: `Activate`: `anim_boxing_front` plays `DoFront` (31 x 60 ms), `anim_boxing_back` plays `DoAnim` (31 x 60 ms = **1,860 ms**), `box_station.ogg`, `BZeppelin::Pickup()`; wait for `anim_boxing_back`. While it runs, once `anim_boxing_back` is past frame **57** the chocolate is hidden (it is in the box). `OnResume`: `processed = true`, base frame 2, anims hidden, the chocolate detached (`DetachChocolate(true)`, which clears `processed` again), `BZeppelin::ReadyForPickup(machine)`, state 3, value `num_deliveries` += 1 (saved); the machine stays locked until the airship resets it (see `BZeppelin` in *meta* 4.6). (With no zeppelin it only `Reset`s: state 3 and the `num_deliveries` increment are in the branch that has a zeppelin.)
* Stats: `zeppelin_served_this_level`, `num_deliveries` (used by quests/achievements).

### 5.8 Disposal (`kDisposal`, station 15) **F**

* `VDisposal : VStation` (not a `VMachine`; 20 methods; `VStation::Create` for type 9 gives it a `VHoverGlow(0.18, true, true, false)`). `Init`: enabled by `kDisposal` (on from level 1 in every location); when enabled it switches to asset `base`/`default` and runs `VObject::Init` (not `VStation::Init`, so the tabletop, the machine count and the `mouse` flag are left alone); the station is shown only when enabled. `OnArrival(player, point)`: reads game state `TrashUsedItemID` (taken as empty when it is `"-1"`); `DetachObject(2)` then `DetachObject(1)`; for each thrown item: unless a tutorial is in effect `num_disposed` += 1 (saved), always `disposed_this_level` += 1; appends `,` (when the string is not empty) + the item's type tag + `VChocolate::ToString` when it is a chocolate. Then, **whether or not anything was thrown**: `Activate` (plays the `active` child's own play function once and `disposal.ogg`), game state `++TrashUsed` and `TrashUsedItemID` := the string; last, `VStation::OnArrival(player, point)` (the facing). Disposal throws away **everything the player carries, sauces and drinks included, with no money penalty here**.
* `Activate` is also called by the conveyor when an item falls off the end (§5.10).

### 5.9 Conveyor (`kConveyor`, station 17) **F**

* Style (`stations.lua`, Gaberoun): `arcStart = {612,278}`, `arcEnd = {103,375}`, `arcCenter = {410,790}`, `arcVelocity = 5`, `stations = { "station06", {533,247}, "station05", {457,237}, "station04", {375,236}, "station03", {297,246}, "station02", {225,269}, "station01", {166,306}, "station15", {145,329}, "drop", {103,326} }` (alternating name and point). The belt is visible only when `kConveyor1` is active (enabled at the location's level: Iquitos/Grytviken 60 (never), Namche 27, Krakatoa 38, Kamchatka 42, Gaberoun 1). `kConveyor2` ("turnaround") is **not read by any C++ string** (F): the C++ flag `+0x2e0` that would make items loop is set only to false in the constructor, so the turnaround never happens (I).
* Geometry: polar coordinates around the centre. Angle `theta = atan2(cy - y, x - cx)` (screen y down); start angle 1.195 rad and radius 550.4, end angle 2.208 rad and radius 516.2 for the Gaberoun values (computed); each listed station gets its own (radius, angle). Item position = centre + (r cos theta, -r sin theta); the radius is interpolated between the zones around the item (`CalculateRadius`), falling back to the arc start/end radius.
* **Items**: only chocolates. A new item's zone name is `station07` (so the first zone it passes, ahead of any other, counts as a change), its angle the start angle. `OnArrival(player, zonepoint)`: base `VStation::OnArrival`, then take the carried chocolate that is **not a bucket** (hand 2, else hand 1), `DetachObject`, `AddItemToBelt` (new item at the start angle, appended at the list back, sprite z-layer 1 as a child of the belt, conveyor `DoAnim` (9 frames x 100 ms: the belt-motor wobble) restarts; value `conveyor_max_this_level` is raised to the longest belt count seen). Sauce/drink cannot be put on the belt. Anything else (buckets, empty hands) is ignored.
* **Motion** (`UpdateBeltItems`, each frame, items processed in list order, front = farthest): `dtheta = ((arcVelocity / r)^2 * elapsedMs) / (thetaEnd - thetaStart)` with `r` the current radius; the item's new angle is `theta + dtheta`; the **previous item's new angle** (initially 0) is compared: if `|new - prev| < 0.08` the item is held back: `dtheta` is reduced by 0.0015 (toward zero) and if that makes it <= 0 the item stays put. So items keep >= ~0.08 rad apart. With `arcVelocity = 5` an unobstructed item takes about **11.7 s** to cross (numeric integration of the formula with a linear radius; approximate, I). Past the end angle (`theta > thetaEnd`) the item is **removed in that update and `VDisposal::Activate` is called on `station15`** (`dtheta x 5` is added to its angle first, which nothing sees; the item is lost, no penalty here). The decay check (step 1 above) happens only when an item passes a new zone: a chocolate that has decayed is dropped at the next zone, not at the moment it decays.
* **Serving**: each station name in `stations` is a zone with an angle. `GetStationZones(theta)` returns the nearest zone ahead and nearest behind; when an item's "behind" zone differs from the zone recorded on the item (it just passed a zone), the item tries to serve that station once:
  1. If the chocolate `HasDecayed()` the item is deleted.
  2. Else look up the station (by zone name) and its customer; if it has a customer, check the downstream counters `1 .. (stationID - 1)`: if some other station has a customer in mood 1-3 whose patience value is **strictly lower** than this customer's, skip (let the item continue to the more urgent customer). Otherwise call the customer's vtable +0x188 and if it accepts remove the item from the belt. **F**: +0x188 is `VCustomer::ComplimentaryService(shared_ptr<VObject>)` (the neighbours: +0x184 `ServeCustomer(shared_ptr<VCarriable>)`, +0x18c `GenerateOrders`; the argument is the belt item, a `VObject`): true when the customer's state is 1 to 3, giving `complimentary_patience_bonus` and `chomp.ogg`; the item is **not** compared with the order and pays nothing, it is a free sample. A refusal (false) falls through to the record of the zone: the item keeps riding.
  3. Record the zone on the item either way and keep moving.
  (Where exactly the "not accepted" branch rejoins is read from the control-flow graph, **I**.)
* `OnClick` = `VStation::OnClick`. The belt sprite has a child `belt` (`continuous`, `anim_conveyor`: 3 frames x 100 ms loop) and `base` (`conveyor_base`).

### 5.10 Display case (`kDisplay`, station 16) **F**

* `VDisplay : VStation` (0x2c8 bytes; `VStation::Create` for type 11 gives it a `VHoverGlow(0.18, false, true, true)`). Slots: `std::vector<VCarriable>` (+0x2a0), the number held (+0x2ac), the capacity (+0x2b0), the picked item (+0x2b4) and the click listener (+0x2bc). `Init`: **`kDisplay1` active** switches the asset to `base`/`default` (`center_table`) and the station is shown only then (no `VObject::Init`, no `VStation::Init`), so level 1 has no table and from level 2 (Iquitos) it is there and `ShopWindow::HasCenterTable` is true; creates the `VDisplayListener`; capacity `2 - (kDisplay2 == 0)`, the slot vector resized to it. `LoadFromTable`: removes the children, empties and resizes the slots, clears the vendor's display sets, then `VStation::LoadFromTable`.
* `AttachCarriable(item)` (when `count < capacity`): the first empty slot; child; anchor `chocolate<slot+1>`; `leaf` off, `clickable` on; sprite layer = the vector's size (the capacity); the listener is added; a `VHoverGlow(0.25, true, true, true)`; a chocolate is registered with `AddDisplayChocolate` (sauces and drinks are not chocolates: nothing); `++DisplayUsed`. `DetachCarriable(item)`: `leaf` on, `clickable` off, listener removed, `CancelEffects`, `RemoveDisplayChocolate`, the slot emptied, `count` down, child removed.
* `OnClick`: with a single slot and nothing picked, the pick becomes slot 0 (null when empty); then `VStation::OnClick` (the avatar is sent). A click on an item (the listener's `OnClick`) sets the pick to that item and raises the click on the station.
* `OnArrival(player, point)`: `VStation::OnArrival` first. Then, with a player: **if the pick is set and her free hands cover its hand count: take it** (`DetachCarriable`, `AttachObject`); otherwise, **if `count < capacity`: put** what she carries (`GetObject(0)`: the preferred hand, then the other; `DetachObject(that hand)`, `AttachCarriable`; during a tutorial the item joins the sprite manager's exclusive set). The pick is cleared. So with one slot, a click on the table takes the held item back (and puts nothing); with two, a click on the table only puts, and an item is taken by clicking it.
* `Update(softPaused)`: unless softly paused, every chocolate on the table that `HasDecayed` is detached (dropped); then `VStation::Update`.
* Station 16 has four `playerPoint`s (north/east/west/south).

## 6. Drinks and sauces as order types

* **F** The XML `<order type="kSauce"/>` / `kDrink` produce `VOrderInfo` type 1 / 2. `VCustomer::AddOrder` for type != 0 creates a `VCarriable(1)`, sets its type tag to `"sauce"`/`"drink"`, assigns the asset `sauce`/`drink` (state `default`) for the thought bubble, scales the sauce icon by 0.8 (`0x3f4ccccd`), and appends it to the customer's wanted list (`VCustomer+0x30c`).
* **F** Matching is **by tag only**: any sauce bottle satisfies any sauce order, any drink any drink order (`ServeCustomer`). Price: `sauce_price` 15 and `drink_price` 15 (each added to the customer's payout total before the sale/tip modifiers applied in `VCustomer::Payout`, which is the customer analyst's). Complimentary-item bonuses (`drink_patience_bonus` 30, `complimentary_patience_bonus` 25, `complimentary_pickup_prob` 30, `sauce_combobonus` 3.0, `order_patience_bonus` 20) are consumed outside this area (customer code).
* **F** Production: §5.6 (cooldown machine). There is no ingredient cost, no stock count and no limit other than the cooldown and the player's two hands.
* **F** Chocolate orders count in achievements by value `num_bars/num_truffles/num_infusions/num_deliveries/num_disposed/num_sauce_sold`; there is no `num_drinks`.

## 7. Upgrades

### 7.1 `locations/<loc>/upgrades.xml` schema (read by `VUpgradeManager::LoadUpgrades` + `VUpgrade::LoadFromNode`) **F**

```
<upgrades>
  <upgradetype type="aesthetic|station|gameplay">      # category; any lower-cased string is accepted
    <upgrade name="kXxx" hidden="true|false">          # name is lower-cased; hidden compared case-insensitively to "true"
      <cost>float</cost>                               # default 0
      <minlevel>int</minlevel>                         # default 0
      <maxlevel>int</maxlevel>                         # default INT_MAX
      <enabled>int</enabled>                           # default INT_MAX (never): auto-granted at level >= this
      <requires>kYyy</requires>                        # repeatable; same category as this upgrade
      <image>, <header>, <content> ...                 # any other child: stored as text (UI strings/paths), key = tag
      <object name="spriteName" [recursive="false"] [effect="false"] [+2 string attrs, I: asset/state]/>   # repeatable
    </upgrade>
  </upgradetype>
</upgrades>
```

* Every `<upgrade>` gets a **sequential index** (0-based, in file order across all `upgradetype`s, counting every upgrade including hidden ones); this index is the persistence bit position (§7.3). `<upgrade>` elements directly under `<upgrades>` (no type) are accepted with category `""`; none ship.
* `<object>` (bound object): resolved to a sprite by `VSpriteManager::FindSpriteByName(name)` after the level's sprites exist (`ShopWindow::LevelInit` calls `VUpgradeManager::ResolveBoundObjects`); one that was not found is skipped from then on. The attributes are `name`, `asset`, `state` (read from the string table of the binary, **F**), `effect` and `recursive`; the last two default true (`attr != "false"`). **Effect when an upgrade changes (`VUpgrade::Activate(activate, flag, spider)`)**: the state is set first (activating gives 4 for a preview, else 3; deactivating gives back the remembered state); then for each resolved sprite in name order: deactivating, or an object with neither `asset` nor `state`, re-`Init`s it (vtable +0x28) so that stations/machines/the backdrop re-evaluate their upgrades; with only `state` the sprite's `SwitchState(state, "default")`, with both `SwitchAsset(asset, state, "default")`, with only `asset` nothing (unused by the shipped XML; **I** for which string is which, from the order of the reads); then, if `effect` is true and a `VUpgradeSpider` was supplied, the spider is told `SetRecursive(recursive)` and processes that sprite (the shop's preview glow, *meta* 5.3). The spider is also told the upgrade (`SetUpgrade`) before the loop. Machines themselves do not depend on this: they poll `IsActive` in their own `Init`. Object names used: `stationNN`, `machine_<kind>`, `machine_sauce_2`, `machine_drink`, `avatar`, `background_object`, `background_static_N`, `steam_booster`, `phonograph`.
* Shipped counts: 55-56 upgrades per location (Grytviken 54, Gaberoun 55), 18 `upgradetype` blocks (3 per file).

### 7.2 `VUpgrade` state **F**

`VUpgrade` (0x80 bytes): name (+8), category (+0xc), `requires` set (+0x10), `enabled` (+0x28, INT_MAX), `minlevel` (+0x2c, 0), `maxlevel` (+0x30, INT_MAX), index (+0x34), `cost` float (+0x38), `state` (+0x3c), previous state (+0x40), bound objects map (+0x48), `hidden` (+0x5c), other children text map (+0x60), owning manager weak ptr (+0x78).

| State | Meaning (from the predicates) |
|---|---|
| 0 | not available (default) |
| 1 | unlocked / available to buy (`UnlockUpgrade`) |
| 2 | available (never assigned in the model code; I: UI selection) |
| 3 | **owned, persistent** (`EnableUpgrade`) - the only state that is saved |
| 4 | **temporarily active** (`EnableTempUpgrade`), not saved; remembers the previous state and restores it on `DisableTempUpgrades` |

`IsActive` = state 3 or 4. `IsAvailable` = state not 0 and not 3 (so 1, 2, 4). `InRange(level)` = `minlevel <= level <= maxlevel`. `IsHidden` = the `hidden` attribute (cleared by `RevealUpgrade`). `SetState(s)`: if different, remember the old state unless the old one was 4. `Activate(activate, flag, spider)`: when activating, the new state is `4 - (flag == false)`: `EnableUpgrade` passes `flag = 0` and gets state **3**; `EnableTempUpgrade` passes `flag = 1` and gets state **4**. When deactivating (`activate == false`) the state returns to the remembered previous state. In both cases the bound objects are then processed (§7.1) and the optional `VUpgradeSpider` is told.

### 7.3 `VUpgradeManager` (singleton, 0x4c bytes) **F**

* Members: `map<category, map<name, shared_ptr<VUpgrade>>>` (+8), current `VLevel` (+0x20), current level number (+0x28), upgrades path (+0x2c), set of temp-active upgrades (+0x38), `noSave` (+0x30).
* `IsActive(name, category)`, `IsAvailable`, `GetUpgrade`, `UnlockUpgrade`, `RevealUpgrade`, `EnableUpgrade(name, cat, save, spider)`, `EnableTempUpgrade`, `DisableTempUpgrades(nameFilter, catFilter, spider)` (empty filter matches all) lower-case both names before lookup. A missing upgrade is simply inactive (this is why `kDrink2` is harmless).
* `IsAvailable(name,cat)` additionally requires **all `requires` upgrades active** (`CheckRequirements` is the same test on a given upgrade).
* `EnableUpgrade(save)`: `Activate(true, permanent)` (state 3), erase it from the temp set, and if `save` call `UpdateUserState` (write the byte). `UpdateUserState`: unless `noSave`, get `RState::GetBinaryBlock(<name>)` from the user settings and write `block[index] = (state == 3)` then flush. Block name: `"upgrades"` when no `VLevel` is set, else **`"upgrades_<location>"`** (the `VLevel` string at +0x1c, the location id that also builds the path `locations/%s/upgrades.xml`). Therefore **persistence is per location and per user** (kStation1 bought in Iquitos is a different bit from Gaberoun's).
* `LoadCurrentUser`: unless `noSave`: read the same block and for every upgrade with `index >= 0`, `InRange(level)` and byte `== 1` -> `SetState(3)`.
* `LoadUpgrades(path)`: `ClearUpgrades` then parse as §7.1.
* `GetVisibleOfType(category)` = number of non-hidden, **non-active** upgrades of that category (the shop's "x left" count).
* **`SetCurrentLevel(levelNumber, path, p4, p5, spider)`** (the level-start application): store the level number; if `path` is non-empty store it and `LoadUpgrades`; `LoadCurrentUser`; then for every upgrade that is **not active and whose `requires` are all active**: if `(p5 < 0 || enabled <= p5)` **and** `enabled <= levelNumber` -> `EnableUpgrade(save = true)` (auto-grant); else if `(p4 < 0 || minlevel <= p4)` **and** `InRange(levelNumber)` -> `UnlockUpgrade` (state 1 = purchasable). **Order of iteration** is category then name (map order), so requirements chain only if they sort earlier or are already active; a later pass (e.g. the next `SetCurrentLevel`) completes the rest (**I**).
* Overload with a `VLevel`: stores it, calls the above with `VLevel+0xc` as the number, then, if the extra bool is true, walks the level's own list of granted upgrades (`VLevel+0x34`, (name, category) pairs) and `EnableUpgrade(save = false)` each (level-scoped grants such as quest or tutorial grants; the owning analyst for `VLevel` should confirm the list's source).
* A level's auto-grants are saved (`save = true`) unless the manager is in no-save mode, and `LoadCurrentUser` reads back every saved flag whose upgrade is in range of the level: a player who has reached a later level of a location owns that level's grants (and what was bought) when an earlier level of the location is played again. The manager looks an upgrade up by lower-cased name in the category's map (`operator[]`, which also leaves an empty entry for a name it does not know; nothing reads those).
* `ShopWindow::LevelInit` (story mode): after `VValueManager::Reload`, sets `chocolate_decay_rate = 100 / level.chocolatedecay` (if non-zero) and `level_customer_decay = level.customerdecay + 1.0` (if > 0), calls `VUpgradeManager::SetNoSave(isEndless)`, then `SetCurrentLevel(level, "locations/<loc>/upgrades.xml", -1, VLevelManager::GetCurrentLevel(), !isEndless, nullSpider)`; later `ResolveBoundObjects(spriteManager)`, `VChocolateVendor::UpdateCapability(true)`, `SetMaxLayers(level.maxlayers)` (story). The upgrade shop (`VUpgradeList::PurchaseUpgrades`, other analyst) pays and calls `EnableUpgrade(..., save = true)`; `ShopWindow::UpgradeComplete` then calls `UpdateCapability(true)`.
* Endless mode: every `CHOC_VENDOR_UPDATE_INTERVAL` = **250 ms** of level clock `ShopWindow` checks `endless_threshold_<k>` (minutes; `k = level % 10`, 1..9: 1, 3, 5, 7, 9, 11, 12, 13, 15 shipped, each plus the difficulty modifier) and when crossed bumps the pseudo-level (`VLevelManager::SetCurrentLevel(level+1)`) and calls `VUpgradeManager::SetCurrentLevel(level+1, "", -1, -1, nullSpider)` (so `enabled <= pseudoLevel` upgrades auto-unlock), then `VChocolateVendor::UpdateMaxLayers(minutes)` and `UpdateCapability(true)`.

### 7.4 What the shipped upgrades are (families, F) 

Rows give typical per-location values; only machine-enabling rows are listed exhaustively.

| Name | Category | Effect (where read) | Enable level / purchase |
|---|---|---|---|
| `kMixer1/2/3` | station | mix option 1/2/3 enabled (`VMixer::Init` buttons, `UpdateCapability`) | all hidden, free, `enabled`: Iquitos 1/4/6, Grytviken 11/15/17, Namche 1/23/26, Krakatoa 1/33/35, Kamchatka 1/42/45, Gaberoun 1/1/1 |
| `kMixerQ1/2/3` | station | +`layer_quality_modifier` x `layer_price` for mix option n (`GetCost`) | hidden, no cost/level: **awarded by quests** (`quests.lua` `reward`, `rewardcategory = "station"`) |
| `kIngredient1/2/3` | station | filling n enabled (button, pools) | `enabled`: Iquitos 3/5/7, Grytviken never (60), Namche 22/25/29, Krakatoa 31/34/36, Kamchatka 41/43/46, Gaberoun 1/1/1 (hidden; the `cost` element is 0 or 100 and is not paid for auto-granted ones, I) |
| `kIngredientQ*`, `kToppingQ*`, `kInfusionQ*` | station | quality price bonus for that option (`GetCost`) | awarded by quests |
| `kPress1/2/3` | station | bar / truffle / novelty enabled | `kPress1` `enabled`: 1 in Iquitos/Namche/Krakatoa/Gaberoun, 41 in Kamchatka, 21 in Grytviken (**never reached: Grytviken levels 11-20 ask only for `kInfusion` bars**, F from `levels/level11,15,20.xml`); `kPress2/3`: Iquitos 5/8, Namche 25/28, Krakatoa 32/36, Kamchatka 42/45, Gaberoun 1/1, Grytviken never |
| `kTopping1/2/3`, `kInfusion1/2/3` | station | topping / infusion option enabled; `kInfusion1` also enables infusion bars | per location (Topping1 150, others 100; Infusion1 200 or 0, others 100 or 0) |
| `kDisposal` | station | disposal visible | level 1 everywhere |
| `kSauce1` | station | sauce machine | 1 (Grytviken 11) |
| `kSauce2` | station | **`sauce_cooldown` x(1-0.5)** (via `values.xml`), also bound to `machine_sauce_2` | Gaberoun `enabled 1` free; elsewhere buy 150 / 300 / 550 / 650 (Iquitos 150, Grytviken 150, Namche 300, Krakatoa 550, Kamchatka 650) |
| `kSauce3` | station | second sauce machine (`machine_sauce_2`, disabled once `kDrink1` is active) | buy 500 / 500 / 700 / 1200 / 900-hidden / 1700 (Gaberoun) |
| `kDrink1` | station | drink machine (replaces sauce 2) | enabled at 12 (Grytviken), 44 (Kamchatka); others never |
| `kDelivery1` | station | zeppelin box station | `enabled`: Iquitos 60 (never), Grytviken 14, Namche 23, Krakatoa 35, Kamchatka 41, Gaberoun 1 |
| `kDelivery2` | station | **`delivery_interval` and `delivery_variance` x(1-0.5)** | buy 700 / 300 / 600 / 700 / 1600 (Gaberoun 1,600, min level 52) |
| `kConveyor1` / `kConveyor2` | station | belt visible / (unused turnaround) | `kConveyor1`: Namche 27, Krakatoa 38, Kamchatka 42, Gaberoun 1; `kConveyor2` effectively never |
| `kDisplay1` / `kDisplay2` | station | display case / capacity 2 | `kDisplay1` free at 2 (Iquitos), 11, 1, 5, 41, 5; `kDisplay2` 300-2000 |
| `kMovement1/2` | gameplay | `walkspeed` 4.0 +0.2 x4.0 each: 4.8 / 5.6 | 100-1000 / 300-2500 |
| `kCustoms1/2` | gameplay | `salemodifier` 1.0 + 0.05 each | 100-650 / 300-2500 |
| `kService1/2` | gameplay | `tipmodifier` 1.0 + 0.05 each | same price bands as customs |
| `kSteam1` | gameplay | machine animation delay x0.9 (`VMachine::Activate`) | 300-1500 |
| `kPhonograph1` | gameplay | `patience_decay_modifier` 1.0 - 0.1 | 300-3000 |
| `kStation1/2`, `kBackground1/2`, `kStatic1..4`, `kAnim1/2` | aesthetic | tabletop / floor / decoration art (`VStation::Init`: tabletop asset state `upgrade2` if `kStation2`, `upgrade1` if `kStation1`, else `default`; Kamchatka/Namche files swap which sheet is state 1 vs 2) | 100-2000; `kAnim*` hidden |

(The `minlevel` of each shop upgrade is the first level of that location where it is visible in the shop; many gameplay upgrades use a low `minlevel` so they are visible in every location's range.)

Modifier semantics (`VValue::GetValue`, **F**): `value = clamp(base + sum over active modifiers of (base x modifier.value), min, max)`; modifier kinds by attribute: `upgrade=` (+ `category=`) active upgrade, `plot=` active plot item, `byval=` (the current value of another named value as the factor), or unconditional. `GetSum(name, delta, clamp)` = `GetReal(name) + delta` clamped to the value's min/max. `GetInt`/`GetUint` floor the value.

## 8. Value names touched by this area

Read (`values.xml`, defaults when missing in brackets): `layer_price` [0] (10), `layer_quality_modifier` [1.0] (0.1), `sauce_price` [0] (15), `drink_price` [0] (15), `sauce_cooldown` [5000] (15000, kSauce2 -0.5), `drink_cooldown` [5000] (15000, kDrink2 -0.5 never active), `delivery_interval` [0] (30000, kDelivery2 -0.5), `delivery_variance` [2] (5000, kDelivery2 -0.5), `chocolate_fade_time` [1000] (4000), `chocolate_decay_rate` [0] (set per level), `display_influence_mix|ingredient|shape|infusion|topping` [0] (30/20/30/20/20), `endless_two_layers|endless_three_layers` [0] (0/0), `order_patience_bonus` (20), `walkspeed`, `salemodifier`, `tipmodifier`, `patience_decay_modifier` (other areas).
Written (stats): `chocolates_made_this_level` (Press, Infuse), `decayed_this_level` (chocolate), `disposed_this_level`, `num_disposed` (saved), `zeppelin_served_this_level`, `num_deliveries` (saved), `num_bars` (saved), `num_truffles` (saved), `num_infusions` (saved), `conveyor_max_this_level`, `num_sauce_sold` (customer). Quest conditions (`locations/quests.lua` `compvalue`) read `chocolates_made_this_level`, `conveyor_max_this_level`, `decayed_this_level`, `disposed_this_level`, `zeppelin_served_this_level` among others.
Game states raised: `++TrashUsed`, `TrashUsedItemID`, `++OrderCompleted` (customer), `++TotalValidClicks`, `++DisplayUsed`.

## 9. Sounds (all `audio/sfx/`, F) by trigger

`buttonclick` (machine button click), `pickup_drop` (attach/detach chocolate with sound), `robot_arm`, `spray`, `robot_arm_2` (mixer), `machine_noise`, `glug_machine` (ingredient), `stamp` (press unlock), `barrel_trap_door` (press/infuse `drop()`), `infusion_machine`, `topping_machine` + `topping` (topping start), `sauce_arm_extend`, `sauce_clink` (dispenser frame > 22), `sauce_spray`, `sauce_arm_retract` (sauce and drink), `box_station` (delivery), `disposal`, `customer_leaves` (customer), `phonograph` (anim script, I). See `docs/re/greatcc-audio.md` for the audio layer.

## 10. Data schemas decoded

### 10.1 `locations/machines.lua` (global style tables; F)

* `machine_button_assets` = `{ button, button2, frame, stripe (13 states stripe_00..12, `anim = false`), icon (≈44 states named after the filling/topping/shape; `anim = false`) }`; the icon files are `anim/machines/buttons/<file>.png`. `coffee` maps to `coffee_green`, `yogurt` and `sourcream` share one file.
* Machine style keys: `baseDir`, `name`, `type = "machine"`, `spriteLayer` (7 for the chocolate machines, 3 for sauce/drink/delivery), `chocolate = {x,y}` (see §4.5, dead), `assets.base`, `children` (a table whose children are named: `static_*` shown when idle, `anim_*` animated, plus `panel`, `shadow`, `door_front`, `tubes`, `pipes`... with `layer`, `x`, `y`, `scale`, `continuous`, `mouse`, `visible`, `leaf` and assets referencing a sheet and `base=`/`play=`), delivery style also has `bubble` (see §5.7), sauce/drink styles have `stationType` (6/8) and the `bottle` child (`type`, `anchor = "bottle"`, hidden), infusion has `x=10,y=-25`.
* Child names the C++ looks up (so they must exist): mixer `anim_arm`, `anim_tube`, `anim_spray`; ingredient `anim_intro`, `anim_pump`, `anim_top1/2`, `static_top1/2`; press `static_door`, `static_largeweight`, `static_wheel`, `anim_door`, `anim_wheel`, `anim_largeweight`, `anim_fill`; infusion `static_top`, `static_base`, `anim_top_working`, `anim_base`, `anim_door`; topping `anim_funnel`, `anim_particles`; sauce/drink `static_fill`, `anim_fill`, `static_top`, `anim_top`, `static_dispenser`, `anim_dispenser`, `anim_spray`, `anim_fill_steam` (drink), `bottle`; delivery `anim_boxing_front`, `anim_boxing_back`; disposal `active`; conveyor `belt`.
* `machine_booster` (upgrade `kSteam1`) and `machine_phono` (`kPhonograph1`) are decorative background objects (the booster sprite loops `DoAnim` 20 frames x 60 ms and `DoSteam` 5 frames x 90 ms; the phonograph plays `phonograph.ogg` at random 20-40 s gaps via its anim script).
* `machine_disposal_assets/children`: disposal base frame 0, child `active` plays `DoAnim` (7 frames x 70 ms).

### 10.2 `locations/<loc>/stations.lua`

* Variables at the top: `ingred1..3`, `topping1..3`, `infusion1..3` (icon names), `stripe1..3`, `novelty`. Per-location values (F, from the files): Gaberoun mint/orange/raspberry, lemon/cashews/amaretto, yogurt/currant/date, scarab; Grytviken yogurt/fig/lemon, raisins/pistachio/amaretto, greenapple/cinnamon/blackberry, whale; Iquitos banana/coffee/saffron, pistachio/ants/cashews, raspberry/orange/blueberry, inca; Kamchatka blueberry/orange/saffron, lemon/pistachio/raisins, sourcream/beets/caviar, egg (its `ingred2` and `topping1` keep the template values); Krakatoa banana/tea/yogurt, cashews/coconut/currant, blueberry/caramel/cherry, volcano; Namche cherry/ginger/greenchili, coconut/lemon/pistachio, lemon/almondpaste/fig, bell. (`chocolates.lua` repeats the same choices as frame indices.)
* Per station: `name = "stationNN"`, `stationType`, `stationID`, `spriteLayer`, `playerPoint = {zoneName, facing}` (display: four pairs), `custPoint`, `money = {x,y}`, `assets.tabletop` (3 states `default`/`upgrade1`/`upgrade2`; Kamchatka and Namche swap 1/2), `machines = { <name> = { style = <machine style>, x, y, name, selections = { buttonN = {...} } } }`. Counters 1-6 are `kCounter`; 7 delivery; 8 empty + sauce/sauce2/drink machines; 9 empty; 10 infusion; 11 topping; 12 press; 13 ingredient; 14 mixer; 15 disposal (`stationID = 15`; Iquitos' file uses 16 - a harmless data slip, I); 16 display (`stationID = 15` in the Gaberoun file, I: copy-paste); 17 conveyor.
* Button entries (`selections.buttonN`): `baseDir = "anim/machines/buttons/"`, `name`, `id` (0..2), `x`,`y` (screen px), `icon`, optional `stripe`, `assets = machine_button_assets`. Gaberoun positions: infusion (768, 371/417/462); topping (627,557), (671,543), (714,529); press (456,573), (506,570), (556,568); ingredient (257,562), (306,565), (357,568); mixer (75,518), (116,530), (162,542). Layout is a fixed 3-button row/column; there is no automatic layout; `Init` hides buttons whose upgrade is inactive (so a machine with 1 enabled option shows 1 button at its fixed spot).
* The conveyor entry holds `arcStart/arcEnd/arcCenter/arcVelocity/stations` (§5.9).

### 10.3 `locations/<loc>/chocolates.lua` (§2.4)

Per-location variables `novelty`, `ingred1..3` (frame indices into `*_ingred` / `barrel_icons`), `topping1..3` (frame indices into `generic_topping`), `infusion1..3` (frame indices into `infusion_layer`); the table `<loc>Chocolates = { baseDir = "anim/chocolates/", bucket = {...}, <name> = { layerType, barType, option, [x,y], [tint], assets = {...} } }` with 3 x 4 shape prototypes (bar, truffle, infusion, novelty x dark/milk/white), 3 x 4 filling prototypes (bar, truffle, novelty; infusion bars have none), 3 x 4 topping prototypes (bar, truffle, infusion, novelty), 3 infusion-layer prototypes (names `infusion_raspberry|orange|caramel` are historic, the art follows `infusion1..3`). Differences between locations: only the variable block at the top (plus Grytviken `x=7` on the 3 infusion layers; Iquitos names the bucket icon `bucket_icon` and omits its `anchor`).

### 10.4 `values.xml` / `locations/values.lua`

`locations/values.lua` defines `valueTable = { a = 15 }` (no reader found, I: dead). See §8 for the values.

### 10.5 Animation timing quick table (`.anm` scripts; F)

| Sheet | Script | Frames x delay |
|---|---|---|
| `mixer_arm` | `DoAnim`, `Reverse` | 31 x `getdelay()` |
| `mixer_tube_*` | `DoAnim`, `Reverse` | 7 x 60 |
| `mixer_pour_*` | `DoAnim` | 5 x 60 |
| `ingred_intro` | `DoAnim`, `Reverse` | 22 x `getdelay()` (11 frames doubled) |
| `ingred_pump` | `DoAnim` | 27 x `getdelay()` |
| `ingred_top_01/02/03` | `DoAnim`, `Reverse` | 30 / 31 / 31 x `getdelay()` |
| `press_fill` | `DoAnim_0/1/2` | 6 x 60 each |
| `press_large` | `DoAnim` | 5 x d, `unlock()`, 26 x d |
| `press_misc` | `DoAnim` / `DoDoor` | 31 x d / 6 x 20, `drop()`, 25 x 20 |
| `infuse_misc` | `DoAnim` / `DoDoor` | 25 x d, `unlock()`, 6 x d / 7 x 20, `drop()`, 27 x 20 |
| `base_working` | `DoAnim` | 31 x d |
| `topping_misc` | `DoAnim` / `DoParticles` | 31 x d / 30 x 20 |
| `sauce_dispenser`, `sauce_blue_top`, `sauce_red_top`, `cup_dispenser` | `DoAnim`, `Reverse` | 31 x 30 (`_fast`: 27, unused) |
| `sauce_fill` | `DoAnim`, `Reverse` | 32 x 30 |
| `sauce_base` | `DoSauceFill` / `DoDrinkFill` / `DoDrinkSteam` | 8 x 60 / 7 x 60 / 5 x 30 |
| `drink_cup` (steam child) | `DoAnim` | 18 x 60 loop |
| `delivery_working` | `DoFront`, `DoAnim` | 31 x 60 each |
| `zepp_pickup` | `DoAnim` | 22 x 30 |
| `conveyor_base`, `anim_conveyor` | `DoAnim` | 9 x 100, loop 3 x 100 |
| `disposal_base` | `DoAnim` | 7 x 70 |

The `.anm` files in `anim/machines/` and everywhere else are **byte-identical** between the Mac and Windows builds (198 files, F by `cmp`).

## 11. Lua/XML contract: what the C++ expects (summary)

* `game.lua`: `location_<loc> = { stations = <loc>Stations, chocolates = <loc>Chocolates, ... }` tables, read by `ShopWindow::LoadStations` / `LoadChocolates`.
* `<loc>Stations`: `baseDir`, `station_NN` entries (§10.2); each `machines` entry becomes a `VMachine` whose subclass is chosen by `VLuaStyle::GetNumber("stationType", stationDefault)` and whose map key is the lower-cased entry key (or the `name` field when present - `machine_sauce_2`).
* `<loc>Chocolates`: §10.3. The vendor must contain every `[barType][layerType][option]` the machines or orders can request; a missing prototype makes `AddLayer`/`GetLayer` return null silently (the press would then keep the bucket).
* `upgrades.xml`: §7. Names the C++ queries literally (case-insensitive): `kMixer<i>`, `kMixerQ<i>`, `kIngredient<i>`, `kIngredientQ<i>`, `kPress<i>`, `kTopping<i>`, `kToppingQ<i>`, `kInfusion<i>`, `kInfusionQ<i>`, `kSauce1`, `kSauce3`, `kDrink1`, `kDelivery1`, `kDisposal`, `kConveyor1`, `kDisplay1`, `kDisplay2`, `kStation1`, `kStation2`, `kSteam1` (gameplay); all in category `station` except `kStation*` (`aesthetic`) and `kSteam1` (`gameplay`). Upgrades referenced only by `values.xml` modifiers: `kSauce2`, `kDrink2` (absent), `kDelivery2`, `kMovement1/2`, `kCustoms1/2`, `kService1/2`, `kPhonograph1`.
* `levels/levelNN.xml`: `<order type shape mix>`, `<layer type option>`, `<zeppelin>` list of orders, `<chocolatedecay>` (ms), `<maxlayers>`, `<customerdecay>` (§2.2, §2.5, §5.7).
* `quests.lua`: `reward = "kXxxQn"`, `rewardcategory = "station"` grant quality upgrades (awarded through `EnableUpgrade`, owner: quest analyst).

## 12. Mac vs Windows

* **F** All XML read here (`values.xml`, six `upgrades.xml`, 60 level files: 68 files) decode (ZXML = u32 compressed length, u32 raw length, zlib) to the same text as the Mac copy modulo CR/LF and surrounding whitespace; all 198 `.anm` are byte-identical.
* **I** The Windows Lua is Lua 5.0 bytecode; I checked that every string constant of `machines.lua`, `defines.lua`, Gaberoun `chocolates.lua`/`stations.lua`, Iquitos `stations.lua`, `values.lua` occurs in the bytecode. Numeric constants were not compared. No Mac/Windows difference is known in this area.

## 13. Unknown / not determined

* (resolved) `VMachine` vtable +0x124/+0x128 on `VStation` are `VSuspendable::Reset` and `Suspend`: the station is suspended while a machine works (4.5).
* **U** The names of the two extra string attributes read by `VUpgrade::AddBoundObject` (probably asset and state names); no shipped upgrade uses them.
* **U** Where `VPress`/`VInfuse` `+0xc8` (`SetFrame`) pairs matter visually. `VConveyor+0x2e0` (turnaround) is never set, so it is always off.
* **U** Whether `shape`/`mix` explicit orders are really overridden by display influence in play (the code says yes, with probability pct/101 when a displayed chocolate has any option in that slot; the display case needs `kDisplay1`, from level 2-41 depending on location, so scripted early levels are unaffected).
* **U** `ShopWindow::OrderComplete(cost, point, customer)` payout rules for the zeppelin (modifiers).
* **U** The Press `shape` index `option` is used as the barType directly: it assumes press button ids 0/1/2 equal `kBar/kTruffle/kNovelty` (true in the shipped data).
* **U** Numeric Lua constants and `chocolateposse.lua` on Windows were not compared.

## Choices where the code does not say (I)

* The first argument of `VAlphaEffect::Create` is *fade in*: read from the defaults the button passes with it (start alpha 0 with `true`, 1 with `false`) and the chocolate's decay fade, which is created with `false`.
* A machine's `selections` entry is loaded with the `selections` table and then the machine's own table as parents; a key a button lacks (`type`, `baseDir`) is found there, so the `type` of a machine button reads `machine` after loading, not the constructor's `machine button`.
* A machine type with no class (none is left among the shipped stations) is left out of a station's machine list and logged once.
* Until the upgrade manager exists, `IsActive` for the machines and the vendor is answered from the location's `upgrades.xml` alone: every upgrade whose `requires` are active and whose `enabled` level has been reached is on (the part of `SetCurrentLevel` that grants by level, one pass in category and name order). Nothing is bought or saved. Level 1 therefore has `kMixer1`, `kPress1`, `kDisposal`, `kSauce1` and, from level 2, `kDisplay1`.
* The tutorial's exclusive-sprite step when the display case takes an item (`VDisplay::OnArrival`) and the tutorial check of the disposal go through the shop's `tutorialInEffect` (false until the tutorial manager exists). The game states `++TrashUsed`, `TrashUsedItemID` and `++DisplayUsed` are kept as strings in a store the tutorials will observe; the analytics (`DoMetrics`) are the only other reader. `ShopWindow::InitMetrics`, run by every level setup, starts them over: `LevelNumber` = the level, `TutorialEnd` and `TrashUsedItemID` = -1, and `TotalCustomers`, `TotalCustomersLost`, `OrderCompleted`, the three `...ChainPointsEarned`, `DisplayUsed`, `TrashUsed`, `TotalClicks` and `TotalValidClicks` = 0. `GetState` of a key never set is read as empty.
* The sound the original plays when the bucket arrives in the press (`AttachChocolate(..., true)`) and when the old bar is taken out (`DetachChocolate(true)`) is `pickup_drop.ogg` each; the hover glow of a button is removed from the button by the machine, as in `VMachine::LoadFromTable`.

## 14. Implementation notes for OpenConche

* No existing engine piece implements this game; the closest reusable parts are the sprite runtime and animation-script host (`src/sdk/Sprite.cpp`, `src/sdk/SpriteScript.cpp`) plus the `.anm` reader (`src/gfx/AnmSheet.cpp`). **GREATCC `.anm` files use a variant layout**: after `IMGN` there are 4 u32 (reg x, reg y, w, h) then two strings, and each `FRMS` frame is 6 i32 + one float 1.0 + six sentinel floats (-1e7) (13 u32) then the frame-name string and the anchor list (`u32 x, u32 y, string name`), versus the 8-u32 / 12-u32 Diner-Dash layout `AnmSheet` parses; `SCRP` follows `FRMS`. The scripts rely on natives `delay`, `frames{}`, `getdelay` (= the sprite's delay, §4.5), `unlock`, `drop`, `playsound`, `random`, and loops (`while(1)`, `for`).
* Implement machines as a sequencer over `{wait ms | wait for animation | callback}` steps (the `VSuspendable` model of §4.2) driven by the game clock (frozen while paused). Keep `state`, `locked`, `processed`, `option` exactly as in §4.1 - customers' and quests' logic reads `locked`/`processed` indirectly through pickup rules.
* `VValue` evaluation (`base + sum(base x modifier)`) must be lazy (re-evaluated on each read) because machine cooldowns and `GetCost` read upgrades live.
* Chocolate: store 5 slots of `(layerType, barType, option)` plus `mixOption`, `ingredientId`, `top`, `barType`, `decay`; implement `CanAddLayer`, `Compare`, `CompareWithLayer`, `GetCost` exactly (§2.2). Render with child sprites in z-layers shape < filling < infusion < topping.
* The vendor's random order builder (§2.6) and display influence use `TPlatform::Rand` (`rand % n`); for reproducible tests seed it through the existing RNG hook.
* Persist upgrades per user as a byte array per location keyed by the file-order index (§7.3); changing `upgrades.xml` order would break saves, so read the shipped file as is.
