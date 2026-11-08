#pragma once

#include "assets/Content.h"

namespace choco::assets {

// Every file whose path ends with ingredients.xml (suffix match, sorted), root <root>, children <ingredient>.
std::vector<KeyValues> loadIngredients(const AssetFS& fs, Warnings* warnings = nullptr);
// The same for products.xml and <product>.
std::vector<KeyValues> loadProducts(const AssetFS& fs, Warnings* warnings = nullptr);

// Script paths named by ports.xml (<port><name>x</name></port>) / quests.xml (<file>x.lua</file>). Each name resolves to
// <dir>/<name>/<name>.lua or else <dir>/<name>.lua, where dir is the list file's path up to the first '/' at index 4 or
// later (as in the original); names that do not exist are skipped (with a warning).
std::vector<std::string> loadPortFileList(const AssetFS& fs, Warnings* warnings = nullptr);
std::vector<std::string> loadQuestFileList(const AssetFS& fs, Warnings* warnings = nullptr);

}  // namespace choco::assets
