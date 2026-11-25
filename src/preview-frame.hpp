#pragma once
#include <QByteArray>
#include <QImage>
#include <QSize>
#include <cstdint>
#include <cstring>

inline QSize previewSize(uint32_t width, uint32_t height) {
  if (!width || !height || width > 32768 || height > 32768)
    return {};
  return QSize(int(width), int(height)).scaled(320, 180, Qt::KeepAspectRatio);
}
inline bool validPreviewSize(QSize s) {
  return s.width() > 0 && s.height() > 0 && s.width() <= 320 &&
         s.height() <= 180;
}
inline QImage copyPreviewFrame(const uint8_t *data, uint32_t stride,
                               QSize size) {
  if (!data || !validPreviewSize(size) || stride < uint32_t(size.width() * 4))
    return {};
  QImage result(size, QImage::Format_RGBX8888);
  if (result.isNull())
    return {};
  for (int y = 0; y < size.height(); ++y) {
    auto line = result.scanLine(y);
    std::memcpy(line, data + size_t(y) * stride, size_t(size.width()) * 4);
    for (int x = 0; x < size.width(); ++x)
      line[x * 4 + 3] = 255;
  }
  return result;
}
inline bool visiblePreviewFrame(const QImage &image) {
  if (image.isNull())
    return false;
  for (int y = 0; y < image.height(); y += 4) {
    auto row = image.constScanLine(y);
    for (int x = 0; x < image.width(); x += 4)
      if (row[x * 4] > 8 || row[x * 4 + 1] > 8 || row[x * 4 + 2] > 8)
        return true;
  }
  return false;
}
inline QImage decodePreviewFrame(const QByteArray &base64, int width,
                                 int height) {
  QSize size(width, height);
  if (!validPreviewSize(size) || base64.size() > 320 * 180 * 4 * 2)
    return {};
  auto raw =
      QByteArray::fromBase64(base64, QByteArray::AbortOnBase64DecodingErrors);
  if (raw.size() != qsizetype(width) * height * 4)
    return {};
  return copyPreviewFrame(reinterpret_cast<const uint8_t *>(raw.constData()),
                          uint32_t(width * 4), size);
}
