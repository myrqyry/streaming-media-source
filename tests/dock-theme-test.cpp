#include "media-dock.hpp"
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
#include <cstdlib>
#include <memory>
#include <obs.h>
static void check(bool ok, const char *msg) {
  if (!ok) {
    qCritical() << msg;
    std::exit(1);
  }
}
static void settle() {
  QEventLoop loop;
  QTimer::singleShot(50, &loop, &QEventLoop::quit);
  loop.exec();
}
static QColor pixel(QToolButton *b) {
  return b->icon().pixmap(16, 16, QIcon::Normal).toImage().pixelColor(8, 8);
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QTemporaryDir tmp;
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
  dock->resize(240, 430);
  settle();
  check(thumb->width() <= previous, "compact thumbnail fits narrow dock");
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
  obs_shutdown();
}
