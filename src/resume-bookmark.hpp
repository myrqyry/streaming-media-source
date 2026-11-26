#pragma once
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <cmath>
#include <cstdint>
#include <optional>
enum class ResumeMode { Playing, Paused, Stopped };
struct ResumeBookmark {
  ResumeMode mode = ResumeMode::Playing;
  int64_t position = 0;
};
inline QString resumeIdentity(const QString &id, const QString &url) {
  return QString::fromLatin1(
      QCryptographicHash::hash(id.toUtf8() + '\0' + url.toUtf8(),
                               QCryptographicHash::Sha256)
          .toHex());
}
inline QString encodeResume(const QString &id, const QString &url,
                            ResumeBookmark b) {
  return QString::fromUtf8(
      QJsonDocument(
          QJsonObject{{"version", 1},
                      {"identity", resumeIdentity(id, url)},
                      {"mode", b.mode == ResumeMode::Paused    ? "paused"
                               : b.mode == ResumeMode::Stopped ? "stopped"
                                                               : "playing"},
                      {"position", double(b.position)}})
          .toJson(QJsonDocument::Compact));
}
inline std::optional<ResumeBookmark>
decodeResume(const QString &raw, const QString &id, const QString &url) {
  if (raw.size() > 512 || id.isEmpty() || url.isEmpty())
    return {};
  const auto doc = QJsonDocument::fromJson(raw.toUtf8());
  const auto o = doc.object();
  if (o.value("version").toDouble() != 1 ||
      o.value("identity").toString() != resumeIdentity(id, url))
    return {};
  const auto mode = o.value("mode").toString();
  if (mode != "playing" && mode != "paused" && mode != "stopped")
    return {};
  const auto value = o.value("position").toDouble(-1);
  // Reject malformed or absurd timestamps (maximum ten years).
  if (value < 0 || value > 315576000000.0 || std::floor(value) != value)
    return {};
  return ResumeBookmark{mode == "paused"    ? ResumeMode::Paused
                        : mode == "stopped" ? ResumeMode::Stopped
                                            : ResumeMode::Playing,
                        mode == "stopped" ? 0 : int64_t(value)};
}
