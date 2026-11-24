#include "media-runtime.hpp"
#include "resolver.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>
static void check(bool ok, const char *msg) {
  if (!ok) {
    std::cerr << msg << '\n';
    std::exit(1);
  }
}
static void fixture(const QString &path, bool executable = true) {
  QFile f(path);
  check(f.open(QIODevice::WriteOnly), "fixture");
  f.write("fixture");
  f.close();
  check(f.setPermissions(
            QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            (executable ? QFileDevice::ExeOwner : QFileDevice::Permissions{})),
        "permissions");
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir tmp;
  auto root = tmp.path();
  QDir(root).mkdir("bundled");
  QDir(root).mkdir("system");
  fixture(root + "/system/yt-dlp");
  fixture(root + "/system/node");
  fixture(root + "/bundled/yt-dlp");
  fixture(root + "/bundled/deno");
  auto r = findMediaRuntime(root + "/bundled", {root + "/system"}, false);
  check(r.bundled && r.extractor == root + "/bundled/yt-dlp" &&
            r.js == root + "/bundled/deno" && r.engine == "deno",
        "complete bundle preferred");
  QFile::remove(root + "/bundled/deno");
  r = findMediaRuntime(root + "/bundled", {root + "/system"}, false);
  check(!r.bundled && r.extractor == root + "/system/yt-dlp" &&
            r.engine == "node",
        "incomplete bundle uses complete system pair");
  fixture(root + "/bundled/deno", false);
  r = findMediaRuntime(root + "/bundled", {}, false);
  check(!r.ready(), "nonexecutable bundle rejected, no implicit PATH fallback");
  check(validMediaUrl("https://r1.googlevideo.com/video?x=%2F"),
        "valid rendition");
  check(!validMediaUrl("https://googlevideo.com.evil.invalid/x"),
        "reject hostile redirect");
  check(!validMediaUrl("http://r1.googlevideo.com/x"), "reject downgrade");
  check(!validMediaUrl("https://r1.googlevideo.com:81/x"),
        "reject nonstandard port");
}
