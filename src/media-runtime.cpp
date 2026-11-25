#include "media-runtime.hpp"
#include "resolver.hpp"
#include "runtime-update.hpp"
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

static QString executable(const QString &path) {
  QFileInfo f(path);
  return f.isFile() && f.isExecutable() ? f.absoluteFilePath() : QString{};
}
static QString executableName(const QString &name) {
#ifdef Q_OS_WIN
  return name + ".exe";
#else
  return name;
#endif
}
MediaRuntime findMediaRuntime(const QString &bundle, const QStringList &paths,
                              bool systemFallback) {
  if (!bundle.isEmpty()) {
    auto yt = executable(QDir(bundle).filePath(executableName("yt-dlp")));
    auto deno = executable(QDir(bundle).filePath(executableName("deno")));
    if (!yt.isEmpty() && !deno.isEmpty())
      return {yt, deno, "deno", true};
  }
  auto find = [&](const QString &name) {
    for (const auto &dir : paths) {
      auto p = executable(QDir(dir).filePath(executableName(name)));
      if (!p.isEmpty())
        return p;
    }
    if (!systemFallback)
      return QString{};
    auto p = QStandardPaths::findExecutable(executableName(name));
    if (!p.isEmpty())
      return p;
    p = executable(QDir::homePath() + "/.local/bin/" + executableName(name));
    if (!p.isEmpty())
      return p;
    if (name == "node") {
      QDir nvm(QDir::homePath() + "/.nvm/versions/node");
      for (auto v : nvm.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                  QDir::Name | QDir::Reversed)) {
        p = executable(nvm.filePath(v + "/bin/node"));
        if (!p.isEmpty())
          return p;
      }
    }
    return QString{};
  };
  auto yt = find("yt-dlp"), js = find("deno");
  if (!yt.isEmpty() && !js.isEmpty())
    return {yt, js, "deno", false};
  js = find("node");
  if (!yt.isEmpty() && !js.isEmpty())
    return {yt, js, "node", false};
  return {};
}
QByteArray probeMediaHttps(const QString &url, bool hls,
                           std::chrono::steady_clock::time_point deadline,
                           const std::function<bool()> &cancelled,
                           MediaProvider provider) {
  using namespace std::chrono;
  auto end = std::min(deadline, steady_clock::now() + seconds(5));
  QNetworkAccessManager network;
  QUrl next(url, QUrl::StrictMode);
  for (int redirects = 0; redirects <= 3; ++redirects) {
    if (cancelled() || steady_clock::now() >= end ||
        !validMediaUrl(QString::fromLatin1(next.toEncoded()), provider))
      return {};
    QNetworkRequest request(next);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Accept-Encoding", "identity");
    if (!hls)
      request.setRawHeader("Range", "bytes=0-1023");
    auto *reply = network.get(request);
    constexpr qint64 limit = 1048576;
    reply->setReadBufferSize(limit + 1);
    QEventLoop loop;
    QTimer timer;
    timer.setInterval(50);
    QByteArray body;
    bool rejected = false;
    auto consume = [&] {
      if (rejected)
        return;
      body += reply->readAll();
      if (body.size() > limit) {
        rejected = true;
        reply->abort();
      }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, consume);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
      if (cancelled() || steady_clock::now() >= end) {
        rejected = true;
        reply->abort();
      }
    });
    timer.start();
    if (!reply->isFinished())
      loop.exec();
    timer.stop();
    consume();
    auto status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto redir =
        reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    bool ok = !rejected && reply->error() == QNetworkReply::NoError;
    delete reply;
    if (!ok)
      return {};
    if (status >= 300 && status < 400 && !redir.isEmpty()) {
      next = next.resolved(redir);
      continue;
    }
    return status >= 200 && status < 300 ? body : QByteArray{};
  }
  return {};
}

QString manualRuntimeDirectory(const QString &managed) {
  return managed.isEmpty() ? QString{} : QDir(managed).filePath("manual");
}

MediaRuntime selectMediaRuntime(const QString &bundle, const QString &managed,
                                const QStringList &paths, bool systemFallback) {
  auto engine = findMediaRuntime(bundle, {}, false);
  if (engine.ready()) {
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    auto updated = preferredRuntimeExecutable(bundle, managed);
    if (!updated.isEmpty() && updated != engine.extractor) {
      engine.extractor = updated;
      engine.updated = true;
    }
#endif
    return engine;
  }
  engine = findMediaRuntime(manualRuntimeDirectory(managed), {}, false);
  if (engine.ready()) {
    engine.bundled = false;
    engine.userProvided = true;
    return engine;
  }
  return findMediaRuntime({}, paths, systemFallback);
}
