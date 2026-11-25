#pragma once
#include "resolver.hpp"
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <chrono>
#include <functional>
struct MediaRuntime {
  QString extractor, js, engine;
  bool bundled = false;
  bool updated = false;
  bool userProvided = false;
  bool ready() const { return !extractor.isEmpty() && !js.isEmpty(); }
};
MediaRuntime findMediaRuntime(const QString &bundle,
                              const QStringList &paths = {},
                              bool systemFallback = true);
QByteArray probeMediaHttps(const QString &url, bool hls,
                           std::chrono::steady_clock::time_point deadline,
                           const std::function<bool()> &cancelled,
                           MediaProvider provider = MediaProvider::YouTube);

QString manualRuntimeDirectory(const QString &managed);
MediaRuntime selectMediaRuntime(const QString &bundle, const QString &managed,
                                const QStringList &paths = {},
                                bool systemFallback = true);
