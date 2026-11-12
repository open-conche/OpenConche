#pragma once

// CSUtil's comparison helpers (docs/re/greatcc-boot-and-natives.md 4.5): the mask parser and CompareValue the
// achievement table and the quests use.

#include <string_view>

namespace choco::games::greatcc {

enum CompareBit : unsigned { kCmpNot = 1, kCmpLess = 2, kCmpGreater = 4, kCmpEqual = 8 };

// "not", "lessthan", "greaterthan", "equal" joined by '+' (spaces ignored, case ignored), and the characters '!', '<', '>',
// '=' anywhere in the text, each set their bit: ">=" and "greaterthan + equal" are both 12.
unsigned parseComparison(std::string_view text);

// The original's evaluation, quirks included: a lone less or greater bit compares strictly; both bits together give false
// unless equal is also set, in which case the test is `a < b`; the equal bit then ORs in `a == b`; the not bit inverts.
// So lessthan + greaterthan + equal means `a <= b`.
bool compareValue(double a, double b, unsigned mask);

}  // namespace choco::games::greatcc
