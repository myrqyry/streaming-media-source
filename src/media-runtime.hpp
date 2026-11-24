#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <chrono>
#include <functional>
struct MediaRuntime {
  QString extractor, js, engine;
  bool bundled = false;
  bool ready() const { return !extractor.isEmpty() && !js.isEmpty(); }
};
MediaRuntime findMediaRuntime(const QString &bundle,
                              const QStringList &paths = {},
                              bool systemFallback = true);
QByteArray probeMediaHttps(const QString &url, bool hls,
                           std::chrono::steady_clock::time_point deadline,
                           const std::function<bool()> &cancelled);
