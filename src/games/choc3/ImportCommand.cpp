#include "games/choc3/ImportCommand.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "app/Diagnostics.h"
#include "games/choc3/Choc3Module.h"
#include "games/choc3/PrefsImport.h"
#include "sdk/Users.h"
#include "platform/PathUtil.h"
#include "platform/UserDir.h"

namespace choco::app {

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t kMaxSourceBytes = 64u * 1024u * 1024u;  // a real prefs.dat is a few KB per user

// Days since 1970-01-01 to a civil date (proleptic Gregorian), so no platform time function is needed.
void civilFromDays(long long z, int& y, int& m, int& d) {
  z += 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const long long doe = z - era * 146097;
  const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const long long mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

}  // namespace

std::string backupDirName(const std::string& userDir, std::chrono::system_clock::time_point when) {
  std::string base = userDir;
  while (base.size() > 1 && (base.back() == '/' || base.back() == '\\')) base.pop_back();
  const long long secs = std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch()).count();
  long long days = secs / 86400, rem = secs % 86400;
  if (rem < 0) {
    rem += 86400;
    --days;
  }
  int y = 0, m = 0, d = 0;
  civilFromDays(days, y, m, d);
  char buf[32];
  std::snprintf(buf, sizeof buf, "-backup-%04d%02d%02d-%02d%02d", y, m, d, static_cast<int>(rem / 3600),
                static_cast<int>(rem % 3600 / 60));
  return base + buf;
}

std::string backUpUserDir(const std::string& userDir, std::chrono::system_clock::time_point when, std::string& error) {
  std::error_code ec;
  const fs::path given = platform::pathFromUtf8(userDir);
  if (!fs::is_directory(given, ec) || fs::is_empty(given, ec)) return {};
  // The name comes from the canonical path, so "--userdir ." backs up next to the folder, not inside it.
  fs::path canon = fs::weakly_canonical(given, ec);
  if (ec) canon = fs::absolute(given, ec);
  const std::string src = platform::pathToUtf8(canon.lexically_normal());
  const std::string base = backupDirName(src, when);
  std::string name = base;
  for (int n = 2; fs::exists(platform::pathFromUtf8(name), ec) || fs::exists(platform::pathFromUtf8(name + ".partial"), ec); ++n)
    name = base + "-" + std::to_string(n);
  if (pathInside(name, src)) {
    error = "the backup folder " + name + " would be inside " + src;
    return {};
  }
  // Copy to a temporary name and rename at the end, so a partial copy never looks like a backup.
  const std::string partial = name + ".partial";
  fs::create_directories(platform::pathFromUtf8(partial), ec);
  if (!ec) fs::copy(canon, platform::pathFromUtf8(partial), fs::copy_options::recursive, ec);
  if (!ec) fs::rename(platform::pathFromUtf8(partial), platform::pathFromUtf8(name), ec);
  if (ec) {
    const std::string why = ec.message();
    std::error_code ignore;
    fs::remove_all(platform::pathFromUtf8(partial), ignore);
    error = "cannot back up " + src + " to " + name + ": " + why;
    return {};
  }
  return name;
}

int runImportPrefs(const Options& o) {
  const std::string userDir = o.userDir.empty() ? platform::defaultUserDir(games::choc3::module().profile().userDirName) : o.userDir;

  // Read the source first, read-only and whole, so a bad file stops here with nothing backed up or written.
  const fs::path srcPath = platform::pathFromUtf8(o.importPrefs);
  std::error_code ec;
  if (!fs::is_regular_file(srcPath, ec)) {
    diag::err("import: '%s' is not a file\n", o.importPrefs.c_str());
    return 1;
  }
  if (fs::file_size(srcPath, ec) > kMaxSourceBytes) {
    diag::err("import: '%s' is too large to be a prefs.dat\n", o.importPrefs.c_str());
    return 1;
  }
  std::string data;
  {
    std::ifstream in(srcPath, std::ios::binary | std::ios::in);
    if (!in) {
      diag::err("import: cannot open '%s' for reading\n", o.importPrefs.c_str());
      return 1;
    }
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  const game::PrefsParseResult parsed = game::parsePrefsDat(data);
  if (!parsed.ok) {
    diag::err("import: %s\n", parsed.error.c_str());
    return 1;
  }
  diag::out("[import] %s: identity '%s', %zu chunks, %d users%s\n", o.importPrefs.c_str(), parsed.prefs.identity.c_str(),
            parsed.prefs.chunks, parsed.prefs.sections.empty() ? 0 : static_cast<int>(parsed.prefs.sections.size()) - 1,
            parsed.prefs.complete ? "" : " (file ends early)");

  std::string error;
  const std::string backup = backUpUserDir(userDir, std::chrono::system_clock::now(), error);
  if (!error.empty()) {
    diag::err("import: %s; nothing was imported\n", error.c_str());
    return 1;
  }
  if (backup.empty()) diag::out("[import] %s has no files yet: no backup needed\n", userDir.c_str());
  else diag::out("[import] backed up %s to %s\n", userDir.c_str(), backup.c_str());

  sdk::UserStore store(userDir);
  const game::ImportReport report = game::importPrefs(parsed.prefs, store);
  diag::outText(game::formatReport(report));

  // The store ignores write failures, so read the folder back and compare it with what the import meant to write.
  std::string problem = report.writeFailed ? "a write failed" : "";
  {
    sdk::UserStore check(userDir);
    int expectCount = store.count();
    if (check.count() != expectCount) problem = "the folder holds " + std::to_string(check.count()) + " users after the import, expected " + std::to_string(expectCount);
    for (const auto& u : report.users) {
      if (!problem.empty()) break;
      if (check.name(u.storeIndex) != u.name) {
        problem = "user '" + u.name + "' did not reach the disk";
        break;
      }
      if (u.saveWritten) {
        std::ifstream in(platform::pathFromUtf8(userDir) / "profiles" / platform::pathFromUtf8(check.folder(u.storeIndex)) / "save.txt",
                         std::ios::binary);
        const std::string got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (got != u.save) problem = "the save of '" + u.name + "' on disk differs from the imported one";
      }
    }
    if (problem.empty() && check.current() != report.currentUser) problem = "the current user on disk is " + std::to_string(check.current()) + ", expected " + std::to_string(report.currentUser);
  }
  if (!problem.empty()) {
    diag::err("import: FAILED, %s (is %s writable?)%s\n", problem.c_str(), userDir.c_str(),
              backup.empty() ? "" : ("; the previous contents are in " + backup).c_str());
    return 1;
  }
  diag::out("[import] done: the source file was only read\n");
  return 0;
}

}  // namespace choco::app
