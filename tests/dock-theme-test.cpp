#include "media-dock.hpp"
#include "preview-frame.hpp"
#include "runtime-update.hpp"
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <obs.h>
static void check(bool ok, const char *msg) {
  if (!ok) {
    qCritical() << msg;
    std::exit(1);
  }
}
struct MediaControlFixture {
  obs_media_state state = OBS_MEDIA_STATE_PLAYING;
  int pauses = 0, resumes = 0;
};
static void settle() {
  QEventLoop loop;
  QTimer::singleShot(50, &loop, &QEventLoop::quit);
  loop.exec();
}
static QColor pixel(QToolButton *b) {
  return b->icon().pixmap(16, 16, QIcon::Normal).toImage().pixelColor(8, 8);
}
int main(int argc, char **argv) {
  // Windows Qt can route messages to the debugger instead of the
  // CTest console. Make startup and failure output visible on every host.
  qInstallMessageHandler(
      [](QtMsgType, const QMessageLogContext &, const QString &msg) {
        fprintf(stderr, "%s\n", qPrintable(msg));
        fflush(stderr);
      });
  fprintf(stderr, "dock-theme-test: starting QApplication\n");
  fflush(stderr);
  QApplication app(argc, argv);
  qInfo() << "dock-theme-test: platform" << app.platformName();
  check(previewSize(1920, 1080) == QSize(320, 180), "landscape preview");
  check(previewSize(1080, 1920) == QSize(101, 180), "portrait preview");
  check(previewSize(0, 1080).isEmpty(), "invalid source dimensions");
  QByteArray padded(32, 0);
  padded[0] = char(255);
  padded[16 + 1] = char(255);
  auto captured = copyPreviewFrame(
      reinterpret_cast<const uint8_t *>(padded.constData()), 16, QSize(2, 2));
  check(!captured.isNull() && captured.pixelColor(0, 0) == QColor(Qt::red) &&
            captured.pixelColor(0, 1) == QColor(Qt::green),
        "RGBA staging stride and color");
  check(copyPreviewFrame(reinterpret_cast<const uint8_t *>(padded.constData()),
                         4, QSize(2, 2))
            .isNull(),
        "reject invalid stride");
  check(decodePreviewFrame(QByteArray(15, 0).toBase64(), 2, 2).isNull(),
        "reject malformed payload");
  check(decodePreviewFrame(QByteArray(16, 0).toBase64(), 321, 2).isNull(),
        "reject oversized preview");
  auto decoded = decodePreviewFrame(
      QByteArray(reinterpret_cast<const char *>(captured.constBits()),
                 captured.sizeInBytes())
          .toBase64(),
      2, 2);
  check(decoded == captured, "preview round trip");
  QTemporaryDir tmp;
  check(tmp.isValid(), "temporary test directory");
  check(obs_startup("en-US", qPrintable(tmp.path()), nullptr), "OBS startup");
  QImage red(16, 16, QImage::Format_ARGB32);
  red.fill(Qt::red);
  check(red.save(tmp.filePath("red.png")), "red icon");
  QImage blue(16, 16, QImage::Format_ARGB32);
  blue.fill(Qt::blue);
  check(blue.save(tmp.filePath("blue.png")), "blue icon");
  auto css = [&](const QString &name) {
    return QString(".icon-media-play, .icon-media-next, .icon-trash { "
                   "qproperty-icon: url(%1); }")
        .arg(tmp.filePath(name));
  };
  app.setStyleSheet(css("red.png"));
  auto bundle = qEnvironmentVariable("SMOKE_UPDATE_BUNDLE");
  auto managed = qEnvironmentVariable("SMOKE_UPDATE_MANAGED");
  std::unique_ptr<QWidget> dock(createMediaDockWidget(bundle, managed));
  dock->resize(360, 430);
  dock->show();
  settle();
  auto next = dock->findChild<QToolButton *>("media-next");
  auto remove = dock->findChild<QToolButton *>("media-queue-remove");
  auto play = dock->findChild<QToolButton *>("media-play");
  auto thumb = dock->findChild<QLabel *>("media-thumbnail");
  check(next && remove && thumb, "existing controls");
  check(next->property("class").toString() == "icon-media-next" &&
            remove->property("class").toString() == "icon-trash",
        "native OBS icon classes");
  check(play && pixel(play) == QColor(Qt::red) &&
            pixel(next) == QColor(Qt::red),
        "initial theme icons");
  app.setStyleSheet(css("blue.png"));
  settle();
  check(pixel(play) == QColor(Qt::blue) && pixel(next) == QColor(Qt::blue) &&
            pixel(remove) == QColor(Qt::blue),
        "live theme switch refreshes all icons");
  auto previous = thumb->width();
  QFont larger = app.font();
  larger.setPixelSize(24);
  app.setFont(larger);
  settle();
  check(thumb->width() > previous, "thumbnail responds to font metrics");
  const auto widePreview = thumb->width();
  // Simulate the actual width assigned by the OBS dock host rather than
  // relying on a top-level QPA window's platform-specific size hints.
  dock->setFixedWidth(240);
  settle();
  check(dock->width() == 240 && thumb->width() <= 80 &&
            thumb->width() < widePreview,
        "compact thumbnail fits narrow dock");
  if (!bundle.isEmpty() && !managed.isEmpty()) {
    auto engine = dock->findChild<QLabel *>("media-engine-status");
    auto update = dock->findChild<QAction *>("media-engine-check");
    auto reset = dock->findChild<QAction *>("media-engine-reset");
    auto menu = dock->findChild<QToolButton *>("media-engine-menu");
    check(engine && update && reset && menu, "engine controls available");
    auto wait = [&] {
      QElapsedTimer elapsed;
      elapsed.start();
      while (!menu->isEnabled() && elapsed.elapsed() < 150000)
        settle();
      qInfo().noquote() << "ENGINE_RESULT" << engine->text();
      check(menu->isEnabled(), "worker finished");
    };
    update->trigger();
    wait();
    check(engine->text().startsWith("Updated"),
          "official release downloaded and activated");
    auto version = activeRuntimeVersion(managed);
    auto path = preferredRuntimeExecutable(bundle, managed);
    check(!version.isEmpty() && QFileInfo(path).isExecutable(),
          "managed extractor selected");
    update->trigger();
    wait();
    check(engine->text().startsWith("Media engine up to date"),
          "second check is a no-op");
    reset->trigger();
    check(activeRuntimeVersion(managed).isEmpty() && QFileInfo(path).exists(),
          "rollback preserves immutable file");
    qInfo().noquote() << "ENGINE_ROLLBACK" << engine->text();
    update->trigger();
    // Destroy during an explicit update; worker cancellation must be safe.
    dock.reset();
  }
  dock.reset();
  // Use actual dock clicks and a controllable OBS source. Two clicks in
  // one polling interval must not send the same pause command twice.
  obs_source_info info{};
  info.id = "streaming_media_youtube";
  info.type = OBS_SOURCE_TYPE_INPUT;
  info.output_flags = OBS_SOURCE_CONTROLLABLE_MEDIA;
  info.get_name = [](void *) { return "Media control fixture"; };
  info.create = [](obs_data_t *, obs_source_t *) -> void * {
    return new MediaControlFixture;
  };
  info.destroy = [](void *p) { delete static_cast<MediaControlFixture *>(p); };
  info.media_get_state = [](void *p) {
    return static_cast<MediaControlFixture *>(p)->state;
  };
  info.media_play_pause = [](void *p, bool pause) {
    auto &f = *static_cast<MediaControlFixture *>(p);
    if (pause)
      ++f.pauses;
    else
      ++f.resumes;
    f.state = pause ? OBS_MEDIA_STATE_PAUSED : OBS_MEDIA_STATE_PLAYING;
  };
  obs_register_source(&info);
  auto *fixture =
      obs_source_create(info.id, "Control fixture", nullptr, nullptr);
  check(fixture, "create controllable fixture");
  dock.reset(createMediaDockWidget({}, {}));
  dock->show();
  auto *control = dock->findChild<QToolButton *>("media-play");
  check(control && control->isEnabled(), "play enabled for fixture");
  control->click();
  check(control->toolTip() == "Play", "first click shows pending pause");
  control->click();
  check(control->toolTip() == "Pause", "second click shows pending resume");
  // This headless fixture does not start OBS's video thread; real queued
  // callbacks are covered by the live smoke test.

  dock.reset();
  obs_source_release(fixture);
  obs_wait_for_destroy_queue();
  qInfo() << "dock-theme-test: shutting down OBS";
  obs_shutdown();
  qInfo() << "dock-theme-test: completed";
}
