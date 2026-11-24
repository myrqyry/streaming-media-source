#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <functional>
#include <optional>
struct RuntimeRelease {
  QString version;
  QByteArray sha;
  qint64 size = 0;
  QUrl url;
};
bool newerRuntime(const QString &candidate, const QString &current);
bool runtimeDownloadUrl(const QUrl &url);
std::optional<RuntimeRelease> parseRuntimeRelease(const QByteArray &json);
QString activeRuntimeVersion(const QString &root);
QString activeRuntimeExecutable(const QString &root);
bool installVerifiedRuntime(const QString &root, const RuntimeRelease &release,
                            const QByteArray &bytes);
bool clearRuntimeOverride(const QString &root);
QString updateMediaRuntime(const QString &bundle, const QString &root,
                           const std::function<bool()> &cancelled);

QString preferredRuntimeExecutable(const QString &bundle, const QString &root);
QString runtimeDisplayVersion(const QString &bundle, const QString &root);
