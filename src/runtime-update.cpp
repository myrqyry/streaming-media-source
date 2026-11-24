#include "runtime-update.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QVersionNumber>
#include <chrono>
#include <cmath>
namespace {
constexpr qint64 maxAsset = 80 * 1024 * 1024;
bool validVersion(const QString &v) {
  static const QRegularExpression re(
      "^[0-9]{4}\\.[0-9]{1,2}\\.[0-9]{1,2}(\\.[0-9]{1,3})?$");
  return re.match(v).hasMatch();
}
QString readSmall(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 128)
    return {};
  return QString::fromUtf8(file.readAll()).trimmed();
}
QString versionPath(const QString &root, const QString &version) {
  return QDir(root).filePath("yt-dlp-" + version);
}
bool save(const QString &path, const QByteArray &bytes,
          bool executable = false) {
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly))
    return false;
  if (!file.setPermissions(
          QFileDevice::ReadOwner | QFileDevice::WriteOwner |
          (executable ? QFileDevice::ExeOwner : QFileDevice::Permissions{})))
    return false;
  return file.write(bytes) == bytes.size() && file.commit();
}
bool validDigest(const QByteArray &sha) {
  static const QRegularExpression re("^[a-f0-9]{64}$");
  return re.match(QString::fromLatin1(sha)).hasMatch();
}
bool verified(const RuntimeRelease &r, const QByteArray &bytes) {
  return validVersion(r.version) && validDigest(r.sha) && r.size > 0 &&
         r.size <= maxAsset && bytes.size() == r.size &&
         QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
             r.sha;
}
QByteArray get(const QUrl &initial, qint64 limit, int seconds,
               const std::function<bool()> &cancelled) {
  using namespace std::chrono;
  auto deadline = steady_clock::now() + std::chrono::seconds(seconds);
  QNetworkAccessManager manager;
  auto url = initial;
  for (int redirect = 0; redirect <= 4; ++redirect) {
    if (cancelled() || steady_clock::now() >= deadline ||
        !runtimeDownloadUrl(url))
      return {};
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("User-Agent", "Streaming-Media-Source-OBS");
    request.setRawHeader("Accept-Encoding", "identity");
    if (url.host() == "api.github.com")
      request.setRawHeader("Accept", "application/vnd.github+json");
    auto reply = manager.get(request);
    reply->setReadBufferSize(1024 * 1024);
    QEventLoop loop;
    QTimer timer;
    timer.setInterval(100);
    QByteArray data;
    bool failed = false;
    auto drain = [&] {
      if (failed)
        return;
      data += reply->readAll();
      if (data.size() > limit) {
        failed = true;
        reply->abort();
      }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, drain);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
      if (cancelled() || steady_clock::now() >= deadline) {
        failed = true;
        reply->abort();
      }
    });
    timer.start();
    if (!reply->isFinished())
      loop.exec();
    timer.stop();
    drain();
    auto status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto next =
        reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    bool ok = !failed && reply->error() == QNetworkReply::NoError;
    delete reply;
    if (!ok)
      return {};
    if (status >= 300 && status < 400 && !next.isEmpty()) {
      url = url.resolved(next);
      continue;
    }
    return status == 200 ? data : QByteArray{};
  }
  return {};
}
QString executableVersion(const QString &path,
                          const std::function<bool()> &cancelled) {
  QProcess p;
  p.start(path, {"--ignore-config", "--version"});
  if (!p.waitForStarted(1500))
    return {};
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (p.state() != QProcess::NotRunning) {
    p.waitForFinished(100);
    if (cancelled() || std::chrono::steady_clock::now() >= deadline) {
      p.kill();
      p.waitForFinished(1000);
      return {};
    }
  }
  auto version = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
  return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0 &&
                 validVersion(version)
             ? version
             : QString{};
}
} // namespace
bool newerRuntime(const QString &candidate, const QString &current) {
  return validVersion(candidate) && validVersion(current) &&
         QVersionNumber::compare(QVersionNumber::fromString(candidate),
                                 QVersionNumber::fromString(current)) > 0;
}
bool runtimeDownloadUrl(const QUrl &u) {
  auto h = u.host().toLower();
  return u.isValid() && u.scheme() == "https" && u.userInfo().isEmpty() &&
         (u.port() == -1 || u.port() == 443) &&
         (h == "api.github.com" || h == "github.com" ||
          h == "release-assets.githubusercontent.com" ||
          h == "objects.githubusercontent.com");
}
std::optional<RuntimeRelease> parseRuntimeRelease(const QByteArray &json) {
  if (json.size() > 1024 * 1024)
    return {};
  auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject())
    return {};
  auto o = doc.object();
  auto v = o.value("tag_name").toString();
  if (!validVersion(v) || o.value("draft").toBool(true) ||
      o.value("prerelease").toBool(true))
    return {};
  for (auto entry : o.value("assets").toArray()) {
    auto a = entry.toObject();
    if (a.value("name").toString() != "yt-dlp_linux")
      continue;
    auto d = a.value("digest").toString().toLatin1();
    auto size = a.value("size").toDouble();
    auto url = QString("https://github.com/yt-dlp/yt-dlp/releases/download/%1/"
                       "yt-dlp_linux")
                   .arg(v);
    if (a.value("browser_download_url").toString() != url ||
        !d.startsWith("sha256:") || !validDigest(d.mid(7)) || size <= 0 ||
        size > maxAsset || std::floor(size) != size)
      return {};
    return RuntimeRelease{v, d.mid(7), qint64(size), QUrl(url)};
  }
  return {};
}
QString activeRuntimeVersion(const QString &root) {
  if (root.isEmpty())
    return {};
  auto v = readSmall(QDir(root).filePath("active"));
  if (!validVersion(v))
    return {};
  QFileInfo f(versionPath(root, v));
  return f.isFile() && f.isExecutable() && !f.isSymLink() ? v : QString{};
}
QString activeRuntimeExecutable(const QString &root) {
  auto v = activeRuntimeVersion(root);
  return v.isEmpty() ? QString{} : versionPath(root, v);
}
bool installVerifiedRuntime(const QString &root, const RuntimeRelease &r,
                            const QByteArray &bytes) {
  if (root.isEmpty() || !verified(r, bytes) || !QDir().mkpath(root))
    return false;
  auto path = versionPath(root, r.version);
  // Never replace a running immutable version with different bytes.
  if (QFile::exists(path)) {
    QFile existing(path);
    if (!existing.open(QIODevice::ReadOnly) || existing.size() != r.size ||
        QCryptographicHash::hash(existing.readAll(), QCryptographicHash::Sha256)
                .toHex() != r.sha ||
        !QFileInfo(path).isExecutable() || QFileInfo(path).isSymLink())
      return false;
  } else if (!save(path, bytes, true))
    return false;
  return save(QDir(root).filePath("active"), r.version.toUtf8() + "\n");
}
bool clearRuntimeOverride(const QString &root) {
  if (root.isEmpty())
    return false;
  if (!QFileInfo(root).exists())
    return true;
  QLockFile lock(QDir(root).filePath("update.lock"));
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0))
    return false;
  QFile f(QDir(root).filePath("active"));
  return !f.exists() || f.remove();
}
QString updateMediaRuntime(const QString &bundle, const QString &root,
                           const std::function<bool()> &cancelled) {
#if !defined(Q_OS_LINUX) || !defined(Q_PROCESSOR_X86_64)
  return "Runtime updates currently support Linux x86_64 only";
#endif
  auto bundled = QDir(bundle).filePath("yt-dlp");
  if (bundle.isEmpty() || root.isEmpty() ||
      !QFileInfo(bundled).isExecutable() ||
      !QFileInfo(QDir(bundle).filePath("deno")).isExecutable())
    return "Install the bundled media engine first";
  if (!QDir().mkpath(root))
    return "Cannot create runtime directory";
  QLockFile lock(QDir(root).filePath("update.lock"));
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0))
    return "Another runtime update is running";
  auto baseVersion = executableVersion(bundled, cancelled);
  if (baseVersion.isEmpty())
    return "Bundled extractor version check failed";
  auto managedVersion = activeRuntimeVersion(root);
  auto current =
      newerRuntime(managedVersion, baseVersion) ? managedVersion : baseVersion;
  auto release = parseRuntimeRelease(
      get(QUrl("https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest"),
          1024 * 1024, 20, cancelled));
  if (cancelled())
    return "Update cancelled";
  if (!release)
    return "Could not verify latest stable release";
  if (!newerRuntime(release->version, current))
    return "Media engine up to date · " + current;
  auto bytes = get(release->url, release->size, 120, cancelled);
  if (cancelled())
    return "Update cancelled";
  if (!verified(*release, bytes))
    return "Download checksum or size mismatch";
  // Verify the candidate actually launches before activating it.
  QTemporaryDir candidate(QDir(root).filePath(".candidate-XXXXXX"));
  if (!candidate.isValid())
    return "Cannot stage runtime";
  auto path = QDir(candidate.path()).filePath("yt-dlp");
  if (!save(path, bytes, true))
    return "Cannot stage runtime";
  if (executableVersion(path, cancelled) != release->version)
    return "Candidate version check failed";
  if (cancelled())
    return "Update cancelled";
  if (!installVerifiedRuntime(root, *release, bytes))
    return "Could not activate verified runtime";
  return "Updated · " + release->version;
}

QString preferredRuntimeExecutable(const QString &bundle, const QString &root) {
  if (bundle.isEmpty())
    return {};
  auto version = activeRuntimeVersion(root);
  auto base = readSmall(QDir(bundle).filePath("yt-dlp-version.txt"));
  if (!version.isEmpty() &&
      (!validVersion(base) || newerRuntime(version, base)))
    return versionPath(root, version);
  return QDir(bundle).filePath("yt-dlp");
}
QString runtimeDisplayVersion(const QString &bundle, const QString &root) {
  if (bundle.isEmpty() ||
      !QFileInfo(QDir(bundle).filePath("yt-dlp")).isExecutable() ||
      !QFileInfo(QDir(bundle).filePath("deno")).isExecutable())
    return "System engine";
  auto path = preferredRuntimeExecutable(bundle, root);
  if (path != QDir(bundle).filePath("yt-dlp"))
    return "Updated · " + activeRuntimeVersion(root);
  auto base = readSmall(QDir(bundle).filePath("yt-dlp-version.txt"));
  return validVersion(base) ? "Bundled · " + base : "Bundled engine";
}
