#include "media-dock.hpp"
#include <QApplication>
#include <QDebug>
#include <QEventLoop>
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
  std::unique_ptr<QWidget> dock(createMediaDockWidget());
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
  dock.reset();
  obs_shutdown();
}
