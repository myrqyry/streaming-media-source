#include "runtime-update.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
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
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const QByteArray digest(64, 'a');
  auto json =
      QByteArray(
          R"({"tag_name":"2026.11.24","draft":false,"prerelease":false,"assets":[{"name":"yt-dlp_linux","size":1024,"digest":"sha256:)") +
      digest +
      R"(","browser_download_url":"https://github.com/yt-dlp/yt-dlp/releases/download/2026.11.24/yt-dlp_linux"}]})";
  auto release = parseRuntimeRelease(json);
  check(release && release->version == "2026.11.24" && release->size == 1024,
        "exact official stable asset");
  auto bad = json;
  bad.replace("github.com/yt-dlp", "evil.invalid/yt-dlp");
  check(!parseRuntimeRelease(bad), "reject foreign asset");
  bad = json;
  bad.replace("sha256:", "sha1:");
  check(!parseRuntimeRelease(bad), "require SHA256");
  bad = json;
  bad.replace("prerelease\":false", "prerelease\":true");
  check(!parseRuntimeRelease(bad), "stable only");
  check(newerRuntime("2026.11.24", "2026.11.9"), "numeric version comparison");
  check(!newerRuntime("2026.11.9", "2026.11.24"), "never downgrade");
  check(!newerRuntime("../escape", "2026.11.24"), "invalid version rejected");
  check(!runtimeDownloadUrl(QUrl("https://github.com.evil.invalid/x")),
        "redirect host boundary");
  check(!runtimeDownloadUrl(QUrl("http://github.com/x")), "TLS only");
  // The managed updater is a Linux x86_64 feature. The release parser and
  // trust-boundary checks above still run on every platform.
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
  QTemporaryDir tmp;
  auto root = tmp.path();
  QByteArray bytes = "sample executable";
  auto sha =
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
  RuntimeRelease fixture{"2026.11.24", sha, bytes.size(), QUrl()};
  check(!installVerifiedRuntime(root, fixture, "corrupt"),
        "reject corrupt bytes");
  check(activeRuntimeVersion(root).isEmpty(),
        "failure leaves active marker untouched");
  check(installVerifiedRuntime(root, fixture, bytes),
        "verified bytes installed");
  check(activeRuntimeVersion(root) == fixture.version,
        "activate only verified version");
  auto path = activeRuntimeExecutable(root);
  check(QFile::exists(path), "active executable exists");
  auto bundle = root + "/bundle";
  QDir().mkpath(bundle);
  QFile marker(bundle + "/yt-dlp-version.txt");
  check(marker.open(QIODevice::WriteOnly), "bundle marker");
  marker.write("2026.11.25\n");
  marker.close();
  check(preferredRuntimeExecutable(bundle, root) == bundle + "/yt-dlp",
        "newer bundle beats stale override");
  check(marker.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "older bundle marker");
  marker.write("2026.11.23\n");
  marker.close();
  check(preferredRuntimeExecutable(bundle, root) == path,
        "newer verified override selected");
  check(clearRuntimeOverride(root), "rollback");
  check(activeRuntimeVersion(root).isEmpty() && QFile::exists(path),
        "rollback preserves running executable");
#endif
}
