#include "games/greatcc/core/LevelData.h"

#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <vector>

#include "assets/Xml.h"

namespace choco::games::greatcc {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
// str::to_int / to_float over an element's text.
int toInt(const std::string& s) { return std::atoi(s.c_str()); }
float toFloat(const std::string& s) { return static_cast<float>(std::atof(s.c_str())); }

std::string content(const pugi::xml_node& n) { return n.child_value(); }

OrderInfo parseOrder(const pugi::xml_node& n, bool fixBugs, bool* fixed) {
  OrderInfo::Attrs a;
  a.type = n.attribute("type").value();
  a.shape = n.attribute("shape").value();
  a.mix = n.attribute("mix").value();
  a.layers = n.attribute("layers").value();
  for (pugi::xml_node c : n.children()) {
    if (lower(c.name()) != "layer") continue;
    a.layerNodes.emplace_back(c.attribute("type").value(), c.attribute("option").value());
  }
  return OrderInfo::parse(a, fixBugs, fixed);
}

std::shared_ptr<CustomerData> parseCustomer(const pugi::xml_node& n, bool fixBugs, bool* fixed) {
  auto d = std::make_shared<CustomerData>();
  for (pugi::xml_node c : n.children()) {
    const std::string tag = lower(c.name());
    if (tag == "name") d->name = content(c);
    else if (tag == "start") d->start = toFloat(content(c));
    else if (tag == "end") d->end = toFloat(content(c));
    else if (tag == "type") d->type = content(c);
    else if (tag == "order") d->orders.push_back(parseOrder(c, fixBugs, fixed));
    else if (tag == "station") d->station = content(c);
    else if (tag == "decay") d->decay = toFloat(content(c));
    else if (tag == "tip") d->tip = toFloat(content(c));
  }
  return d;
}

// F1 (design section 7): `level28.xml` leaves one `<order>` open, so the parser meets the customer's `<station>` and end tag with the
// order still open. An `<order>` holds only `<layer>`s, so one still open is closed where the next element is not a `<layer>`, or at
// another element's end tag.
std::string closeOpenOrders(std::string_view xml, bool* changed) {
  std::string out;
  out.reserve(xml.size() + 16);
  std::vector<std::string> open;
  std::size_t pos = 0;
  while (pos < xml.size()) {
    const std::size_t lt = xml.find('<', pos);
    if (lt == std::string_view::npos) break;
    out.append(xml.substr(pos, lt - pos));
    std::size_t gt;
    if (xml.compare(lt, 4, "<!--") == 0) {
      const std::size_t end = xml.find("-->", lt);
      gt = end == std::string_view::npos ? xml.size() - 1 : end + 2;
    } else {
      gt = xml.find('>', lt);
      if (gt == std::string_view::npos) gt = xml.size() - 1;
    }
    const std::string_view tag = xml.substr(lt, gt - lt + 1);
    pos = gt + 1;
    if (tag.size() > 1 && tag[1] != '?' && tag[1] != '!') {
      const bool closing = tag[1] == '/';
      std::size_t b = closing ? 2 : 1, e = b;
      while (e < tag.size() && tag[e] != '>' && tag[e] != '/' && !std::isspace(static_cast<unsigned char>(tag[e]))) ++e;
      const std::string name(tag.substr(b, e - b));
      if (closing) {
        while (!open.empty() && open.back() != name && open.back() == "order") {
          out += "</order>";
          open.pop_back();
          if (changed) *changed = true;
        }
        if (!open.empty() && open.back() == name) open.pop_back();
      } else {
        if (!open.empty() && open.back() == "order" && name != "layer") {
          out += "</order>";
          open.pop_back();
          if (changed) *changed = true;
        }
        if (tag.size() < 2 || tag[tag.size() - 2] != '/') open.push_back(name);
      }
    }
    out.append(tag);
  }
  out.append(xml.substr(std::min(pos, xml.size())));
  return out;
}

}  // namespace

int chocolateShapeFromString(const std::string& s) {
  const std::string w = lower(s);
  if (w == "bar" || w == "kbar") return kShapeBar;
  if (w == "novelty" || w == "knovelty") return kShapeNovelty;
  if (w == "truffle" || w == "ktruffle") return kShapeTruffle;
  if (w == "infusion" || w == "kinfusion") return kShapeInfusion;
  return -1;
}

int chocolateLayerFromString(const std::string& s) {
  const std::string w = lower(s);
  if (w == "bucketlayer" || w == "kbucketlayer" || w == "bucket" || w == "kbucket") return kLayerBucket;
  if (w == "chocolatelayer" || w == "kchocolatelayer" || w == "chocolate" || w == "kchocolate") return kLayerChocolate;
  if (w == "ingredientlayer" || w == "kingredientlayer" || w == "ingredient" || w == "kingredient") return kLayerIngredient;
  if (w == "toppinglayer" || w == "ktoppinglayer" || w == "topping" || w == "ktopping") return kLayerTopping;
  if (w == "infusionlayer" || w == "kinfusionlayer" || w == "infusion" || w == "kinfusion") return kLayerInfusion;
  return -1;
}

int orderTypeFromString(const std::string& s) {
  const std::string w = lower(s);
  if (w == "sauce" || w == "ksauce") return kOrderSauce;
  if (w == "chocolate" || w == "kchocolate") return kOrderChocolate;
  if (w == "drink" || w == "kdrink") return kOrderDrink;
  return kOrderInvalid;
}

OrderInfo OrderInfo::parse(const Attrs& a, bool fixBugs, bool* fixed) {
  OrderInfo o;
  o.type = orderTypeFromString(a.type);
  if (o.type != kOrderChocolate) return o;  // sauce, drink and unknown orders read nothing else
  int shape = chocolateShapeFromString(a.shape);
  // F2 (design section 7): level26.xml has one order whose shape is the typo `kTruffler`.
  if (shape == -1 && fixBugs && lower(a.shape) == "ktruffler") {
    shape = kShapeTruffle;
    if (fixed) *fixed = true;
  }
  o.shape = shape == -1 ? kShapeUnspecified : shape;
  if (!a.mix.empty()) o.mix = toInt(a.mix);
  const int layers = toInt(a.layers);
  o.layerBudget = layers < 2 ? 1 : (layers > 3 ? 3 : layers);
  for (const auto& [type, option] : a.layerNodes) {
    const int t = chocolateLayerFromString(type);
    if (t <= 0 || t > 4) continue;
    o.layer[t] = option.empty() ? -1 : toInt(option);
  }
  return o;
}

LevelData::Result LevelData::load(int number, std::string_view xml, bool fixBugs, LevelData& out) {
  out = LevelData{};
  out.number = number;
  out.needPrelevel = number > 3;
  Result r;
  assets::Xml doc;
  bool ok = doc.parse(xml);
  std::string repaired;
  if (!ok && fixBugs) {
    bool changed = false;
    repaired = closeOpenOrders(assets::Xml::plainText(xml), &changed);
    if (changed && doc.parse(repaired)) {
      ok = true;
      r.fixedF1 = true;
    }
  }
  pugi::xml_node root = doc.root();
  if (!ok && root && std::string(root.name()) == "level") {
    // the original's reader is not known for a file that does not parse (design X8): what the parser had built when it stopped is used
    r.partial = true;
    r.error = doc.error();
  } else if (!ok || !root || std::string(root.name()) != "level") {
    r.error = ok ? "no <level> root" : doc.error();
    return r;
  }
  r.parsed = true;
  for (pugi::xml_node n : root.children()) {
    const std::string tag = n.name();
    if (tag == "name") out.name = content(n);
    else if (tag == "location") out.location = content(n);
    else if (tag == "minutes") out.minutes = toInt(content(n));
    else if (tag == "seconds") out.seconds = toInt(content(n));
    else if (tag == "goal") out.goal = toFloat(content(n));
    else if (tag == "mastery") out.mastery = toFloat(content(n));
    else if (tag == "plot") out.plots.push_back(content(n));
    else if (tag == "upgrade") out.upgrades.emplace_back(n.attribute("name").value(), n.attribute("category").value());
    else if (tag == "maxlayers") out.maxLayers = toInt(content(n));
    else if (tag == "customer") out.customers.push_back(parseCustomer(n, fixBugs, &r.fixedF2));
    else if (tag == "zeppelin") {
      for (pugi::xml_node c : n.children())
        if (std::string(c.name()) == "order") out.zeppelin.push_back(parseOrder(c, fixBugs, &r.fixedF2));
    } else if (tag == "lockdown") {
      out.lockdownMin = toFloat(n.attribute("min").value());
      out.lockdownMax = toFloat(n.attribute("max").value());
    } else if (tag == "chocolatedecay") out.chocolateDecay = toInt(content(n));
    else if (tag == "customerdecay") out.customerDecay = toFloat(content(n));
  }
  return r;
}

}  // namespace choco::games::greatcc
