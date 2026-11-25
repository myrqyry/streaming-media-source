#include "media-dock.hpp"
#include "media-queue.hpp"
#include "media-runtime.hpp"
#include "playlist-import.hpp"
#include "playlist-metadata.hpp"
#include "preview-frame.hpp"
#include "runtime-update.hpp"
#include <QApplication>
#include <QBuffer>
#include <QCache>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <memory>
#include <obs-frontend-api.h>
#include <obs-module.h>
namespace {
constexpr const char *dockId = "streaming-media-dock";
using Ref = std::unique_ptr<obs_source_t, decltype(&obs_source_release)>;
Ref sourceRef(const QString &id) {
  return Ref(id.isEmpty() ? nullptr : obs_get_source_by_uuid(qPrintable(id)),
             obs_source_release);
}
QString clockText(int64_t ms) {
  auto sec = std::max<int64_t>(0, ms / 1000);
  return sec >= 3600
             ? QString("%1:%2:%3")
                   .arg(sec / 3600)
                   .arg(sec / 60 % 60, 2, 10, QChar('0'))
                   .arg(sec % 60, 2, 10, QChar('0'))
             : QString("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QChar('0'));
}
QString infoTooltip(const QJsonObject &info) {
  QStringList lines;
  auto add = [&](const QString &label, const QString &value) {
    if (!value.isEmpty())
      lines << label.toHtmlEscaped() + ": " + value.toHtmlEscaped();
  };
  add("Channel", info.value("author").toString());
  add("Uploaded by", info.value("uploader").toString());
  add("Category", info.value("category").toString());
  add("Date (UTC)", info.value("date").toString());
  for (const auto &pair :
       {std::pair<const char *, const char *>{"views", "Views"},
        {"viewers", "Viewers at resolution"},
        {"likes", "Likes"},
        {"followers", "Channel followers"},
        {"comments", "Comments"}}) {
    if (info.contains(pair.first))
      add(pair.second,
          QLocale().toString(qint64(info.value(pair.first).toDouble())));
  }
  QStringList qualities;
  for (auto h : info.value("resolutions").toArray())
    qualities << QString::number(h.toInt()) + "p";
  add("Reported qualities", qualities.join(", "));
  add("Description", info.value("description").toString());
  return lines.join("<br>");
}
QString cacheKey(const QString &url) {
  auto twitch = canonicalProviderUrl(url);
  return twitch.isEmpty() ? videoId(url) : twitch;
}
QImage decodeThumbnail(const QByteArray &bytes, QSize target) {
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  auto format = reader.format().toLower();
  if (format != "jpeg" && format != "png" && format != "webp")
    return {};
  auto size = reader.size();
  if (!size.isValid() || size.width() > 1920 || size.height() > 1920 ||
      qint64(size.width()) * size.height() > 1920 * 1080)
    return {};
  reader.setScaledSize(size.scaled(target, Qt::KeepAspectRatio));
  return reader.read();
}
QString stateText(enum obs_media_state state) {
  switch (state) {
  case OBS_MEDIA_STATE_PLAYING:
    return "Playing";
  case OBS_MEDIA_STATE_PAUSED:
    return "Paused";
  case OBS_MEDIA_STATE_OPENING:
    return "Resolving…";
  case OBS_MEDIA_STATE_BUFFERING:
    return "Buffering…";
  case OBS_MEDIA_STATE_STOPPED:
    return "Stopped";
  case OBS_MEDIA_STATE_ENDED:
    return "Ended";
  case OBS_MEDIA_STATE_ERROR:
    return "Playback error";
  default:
    return "Ready";
  }
}
constexpr int UrlRole = Qt::UserRole, AuthorRole = Qt::UserRole + 1;
class PlaylistView : public QListWidget {
  int dragRow = -1, insertionRow = -1;
  int insertionAt(const QPoint &point) const {
    auto index = indexAt(point);
    int insertion = index.isValid() ? index.row() : count();
    if (index.isValid() && point.y() > visualRect(index).center().y())
      ++insertion;
    return insertion;
  }
  void indicate(int row) {
    insertionRow = row;
    viewport()->update();
  }

public:
  std::function<void()> dragStarted;
  std::function<void(int, int)> dropped;
  explicit PlaylistView(QWidget *parent) : QListWidget(parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionMode(QAbstractItemView::SingleSelection);
  }

protected:
  void startDrag(Qt::DropActions) override {
    dragRow = currentRow();
    if (dragRow < 0)
      return;
    if (dragStarted)
      dragStarted();
    QDrag drag(this);
    auto mime = new QMimeData;
    mime->setData("application/x-streaming-media-row", "1");
    drag.setMimeData(mime);
    drag.exec(Qt::MoveAction);
    dragRow = -1;
    indicate(-1);
  }
  void dragEnterEvent(QDragEnterEvent *e) override {
    if (e->source() == this && dragRow >= 0)
      e->acceptProposedAction();
    else
      e->ignore();
  }
  void dragMoveEvent(QDragMoveEvent *e) override {
    if (e->source() != this || dragRow < 0) {
      e->ignore();
      return;
    }
    // The model is authoritative; accept the gesture but never mutate it here.
    auto point = e->position().toPoint();
    if (point.y() < 16)
      verticalScrollBar()->setValue(verticalScrollBar()->value() - 12);
    else if (point.y() > viewport()->height() - 16)
      verticalScrollBar()->setValue(verticalScrollBar()->value() + 12);
    indicate(insertionAt(point));
    e->acceptProposedAction();
  }
  void dropEvent(QDropEvent *e) override {
    if (e->source() != this || dragRow < 0) {
      e->ignore();
      return;
    }
    int insertion = insertionAt(e->position().toPoint());
    indicate(-1);
    int target = playlistDropTarget(dragRow, insertion, count());
    if (target >= 0 && target != dragRow && dropped)
      dropped(dragRow, target);
    e->acceptProposedAction();
  }
  void dragLeaveEvent(QDragLeaveEvent *e) override {
    indicate(-1);
    QListWidget::dragLeaveEvent(e);
  }
  void paintEvent(QPaintEvent *e) override {
    QListWidget::paintEvent(e);
    if (insertionRow < 0 || !count())
      return;
    int y = insertionRow < count() ? visualItemRect(item(insertionRow)).top()
                                   : visualItemRect(item(count() - 1)).bottom();
    QPainter p(viewport());
    p.setPen(QPen(palette().color(QPalette::Highlight), 2));
    p.drawLine(3, y, viewport()->width() - 3, y);
  }
};
class PlaylistDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &) const override {
    int h = std::max(36, option.fontMetrics.height() * 2 + 2);
    return {100, h + 8};
  }
  void paint(QPainter *p, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    auto title = opt.text;
    auto icon = opt.icon;
    auto style = opt.widget ? opt.widget->style() : QApplication::style();
    opt.text.clear();
    opt.icon = QIcon{};
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, opt.widget);
    p->save();
    int h = std::max(24, opt.rect.height() - 8);
    int w = std::min(h * 16 / 9, std::max(32, opt.rect.width() / 3));
    QRect imageRect(opt.rect.left() + 5, opt.rect.center().y() - h / 2, w, h);
    if (!icon.isNull())
      icon.paint(p, imageRect, Qt::AlignCenter,
                 opt.state & QStyle::State_Enabled ? QIcon::Normal
                                                   : QIcon::Disabled);
    else {
      p->fillRect(imageRect, opt.palette.brush(QPalette::Mid));
      auto fallback =
          style->standardIcon(QStyle::SP_MediaPlay, nullptr, opt.widget);
      fallback.paint(p, imageRect, Qt::AlignCenter);
    }
    auto role = opt.state & QStyle::State_Selected ? QPalette::HighlightedText
                                                   : QPalette::Text;
    p->setPen(opt.palette.color(role));
    auto textRect = opt.rect.adjusted(w + 12, 4, -6, -4);
    auto author = index.data(AuthorRole).toString();
    QFont font = opt.font;
    p->setFont(font);
    int line = opt.fontMetrics.height();
    auto first = textRect;
    first.setHeight(line);
    first.moveTop(textRect.center().y() - line);
    p->drawText(
        first, Qt::AlignLeft | Qt::AlignVCenter,
        opt.fontMetrics.elidedText(title, Qt::ElideRight, first.width()));
    auto second = first.translated(0, line);
    QColor muted = opt.palette.color(role);
    if (!(opt.state & QStyle::State_Selected))
      muted.setAlphaF(.72);
    p->setPen(muted);
    p->drawText(
        second, Qt::AlignLeft | Qt::AlignVCenter,
        opt.fontMetrics.elidedText(author.isEmpty() ? "YouTube" : author,
                                   Qt::ElideRight, second.width()));
    p->restore();
  }
};
struct CachedPlaylistItem {
  PlaylistMetadata metadata;
  QImage image;
  bool titleDone = false, imageDone = false, generated = false;
};
class MediaDock : public QWidget {
  QVBoxLayout *mainLayout = nullptr;
  QProcess *importProcess = nullptr;
  QProcess *metadataProcess = nullptr;
  QByteArray metadataBytes;
  bool metadataOverflow = false;
  QLabel *importStatus = nullptr;
  QToolButton *importCancel = nullptr;
  QByteArray importBytes;
  QString importSource, importReceipt;
  bool importPlay = false;
  int importTotal = -1, importFound = 0, importSkipped = 0;
  QElapsedTimer receiptTimer;
  void importMessage(const QString &message) {
    importStatus->setText(message);
    importStatus->setToolTip(message);
    importStatus->setVisible(!message.isEmpty());
    importCancel->setVisible(importProcess != nullptr);
  }
  void cancelImport(const QString &message) {
    if (!importProcess)
      return;
    auto p = importProcess;
    importProcess = nullptr;
    disconnect(p, nullptr, this, nullptr);
    p->kill();
    p->waitForFinished(1000);
    p->deleteLater();
    importBytes.clear();
    importMessage(message);
  }
  void updateQueueDuration() {
    qint64 total = 0;
    int known = 0;
    for (auto &u : queueUrls) {
      auto cached = playlistCache.object(cacheKey(u));
      if (cached && cached->metadata.durationMs >= 0) {
        total += cached->metadata.durationMs;
        ++known;
      }
    }
    auto summary = QString("Up next · %1").arg(queueUrls.size());
    count->setText(summary);
    count->setToolTip(
        known ? summary + " · " + clockText(total) +
                    (known == queueUrls.size()
                         ? " remaining"
                         : " known · " +
                               QString::number(queueUrls.size() - known) +
                               " unknown durations")
              : summary);
    queueDuration->setText(
        known ? clockText(total) +
                    (known == queueUrls.size()
                         ? " queued"
                         : " known · " +
                               QString::number(queueUrls.size() - known) +
                               " unknown")
              : QString{});
    queueDuration->setVisible(known > 0);
  }
  void pollImportReceipt() {
    if (importReceipt.isEmpty())
      return;
    auto s = sourceRef(importSource);
    QString result;
    if (s && !obs_source_removed(s.get())) {
      calldata_t c;
      calldata_init(&c);
      calldata_set_string(&c, "requestId", qPrintable(importReceipt));
      proc_handler_call(obs_source_get_proc_handler(s.get()),
                        "streaming_media_request_status", &c);
      result = QString::fromUtf8(calldata_string(&c, "status"));
      calldata_free(&c);
    }
    if (result == "pending" && receiptTimer.elapsed() < 10000)
      return;
    importReceipt.clear();
    auto parts = result.split(':');
    if (parts.size() == 3 && parts[0] == "imported") {
      int added = parts[1].toInt(), started = parts[2].toInt();
      QString msg = QString("Imported · %1 queued").arg(added);
      if (started)
        msg += " · first selected";
      if (importFound > added + started ||
          importTotal > importFound + importSkipped)
        msg += " · limited by queue capacity";
      if (importSkipped)
        msg += QString(" · %1 unavailable skipped").arg(importSkipped);
      importMessage(msg);
    } else
      importMessage("Import result unavailable; check queue");
  }
  void beginImport(const QString &link, bool immediate) {
    if (importProcess || !importReceipt.isEmpty())
      return;
    auto s = selected();
    if (!s)
      return;
    auto engine = selectMediaRuntime(runtimeBundle, runtimeManaged);
    if (!engine.ready()) {
      importMessage("Media engine missing");
      return;
    }
    importSource = QString::fromUtf8(obs_source_get_uuid(s.get()));
    importPlay = immediate;
    importBytes.clear();
    auto p = new QProcess(this);
    importProcess = p;
    p->setProgram(engine.extractor);
    p->setArguments({"--ignore-config", "--flat-playlist", "--playlist-end",
                     "101", "--dump-single-json", "--skip-download",
                     "--no-warnings", "--no-progress", "--socket-timeout", "10",
                     "--retries", "1", "--no-js-runtimes", "--js-runtimes",
                     engine.engine + ":" + engine.js, "--", link});
    auto drain = [this, p] {
      if (importProcess != p)
        return;
      importBytes += p->readAllStandardOutput();
      p->readAllStandardError();
      if (importBytes.size() > 4 * 1024 * 1024)
        cancelImport("Playlist response too large");
    };
    connect(p, &QProcess::readyReadStandardOutput, this, drain);
    connect(p, &QProcess::readyReadStandardError, this,
            [p] { p->readAllStandardError(); });
    connect(p, &QProcess::errorOccurred, this,
            [this, p](QProcess::ProcessError error) {
              if (importProcess == p && error == QProcess::FailedToStart)
                cancelImport("Cannot start media engine");
            });
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, p, drain](int code, QProcess::ExitStatus exit) {
              if (importProcess != p)
                return;
              drain();
              if (importProcess != p)
                return;
              importProcess = nullptr;
              p->deleteLater();
              auto data = parseImportedPlaylist(importBytes);
              importBytes.clear();
              if (code != 0 || exit != QProcess::NormalExit ||
                  data.entries.isEmpty()) {
                importMessage("No available playlist videos found");
                return;
              }
              auto s = sourceRef(importSource);
              if (!s || obs_source_removed(s.get()) ||
                  sources->currentData().toString() != importSource) {
                importMessage("Import cancelled · source changed");
                return;
              }
              QJsonArray urls;
              for (const auto &entry : data.entries) {
                urls.append(entry.url);
                auto id = videoId(entry.url);
                auto cached = playlistCache.object(id);
                if (!cached) {
                  cached = new CachedPlaylistItem;
                  playlistCache.insert(id, cached);
                }
                cached->metadata = entry.metadata;
                cached->titleDone = !entry.metadata.title.isEmpty();
              }
              importFound = data.entries.size();
              importTotal = data.total;
              importSkipped = data.skipped;
              auto json = QJsonDocument(urls).toJson(QJsonDocument::Compact);
              calldata_t c;
              calldata_init(&c);
              calldata_set_string(&c, "urls", json.constData());
              calldata_set_bool(&c, "play", importPlay);
              proc_handler_call(obs_source_get_proc_handler(s.get()),
                                "streaming_media_import", &c);
              importReceipt =
                  QString::fromUtf8(calldata_string(&c, "requestId"));
              calldata_free(&c);
              if (importReceipt.isEmpty()) {
                importMessage("Queue busy · try again");
                return;
              }
              receiptTimer.start();
              for (int i = 0; i < queue->count(); ++i)
                decorateQueueItem(queue->item(i));
              updateQueueDuration();
              importMessage("Applying playlist…");
            });
    QTimer::singleShot(45000, p, [this, p] {
      if (importProcess == p)
        cancelImport("Playlist import timed out");
    });
    importMessage("Importing playlist…");
    p->start();
  }

  QString runtimeBundle, runtimeManaged;
  QLabel *engineStatus = nullptr;
  QToolButton *engineMenu = nullptr;
  QThread *updateWorker = nullptr;
  std::shared_ptr<QString> updateResult;
  void refreshEngine() {
    if (updateWorker || !engineStatus)
      return;
    auto engine = selectMediaRuntime(runtimeBundle, runtimeManaged);
    engineStatus->setText(
        !engine.ready()       ? "Media engine missing"
        : engine.userProvided ? "User-installed engine"
        : engine.bundled ? runtimeDisplayVersion(runtimeBundle, runtimeManaged)
                         : "System engine");
    engineStatus->setToolTip(
        engine.ready() ? "Media engine ready"
                       : "Open the engine menu to download yt-dlp and Deno and "
                         "open their installation folder.");
  }
  void checkEngine() {
    if (updateWorker || runtimeBundle.isEmpty() || runtimeManaged.isEmpty())
      return;
    engineMenu->setEnabled(false);
    engineStatus->setText("Checking media engine…");
    updateResult = std::make_shared<QString>();
    auto result = updateResult;
    auto bundle = runtimeBundle, root = runtimeManaged;
    updateWorker = QThread::create([result, bundle, root] {
      *result = updateMediaRuntime(bundle, root, [] {
        return QThread::currentThread()->isInterruptionRequested();
      });
    });
    auto worker = updateWorker;
    connect(worker, &QThread::finished, this, [this, result, worker] {
      if (updateWorker != worker)
        return;
      updateWorker = nullptr;
      worker->deleteLater();
      engineStatus->setText(*result);
      engineStatus->setToolTip(*result);
      engineMenu->setEnabled(true);
    });
    updateWorker->start();
  }
  void stopEngineUpdate() {
    if (!updateWorker)
      return;
    auto worker = updateWorker;
    updateWorker = nullptr;
    disconnect(worker, nullptr, this, nullptr);
    worker->requestInterruption();
    worker->wait();
    delete worker;
  }

  QImage previewImage;
  qint64 seenPreviewGeneration = 0;
  QElapsedTimer previewWait;
  bool themeRefreshPending = false, themeReady = false;
  QComboBox *sources;
  QLineEdit *url;
  QLabel *thumbnail, *title, *status, *mediaAuthor, *time, *count,
      *queueDuration;
  QSlider *timeline;
  QToolButton *play, *stop, *back, *forward, *next, *retry, *up, *down, *remove;
  QPushButton *load, *append;
  PlaylistView *queue;
  QCheckBox *autoplay;
  QTimer *timer;
  QNetworkAccessManager *network;
  QPointer<QNetworkReply> thumbnailReply;
  QString thumbId, sourceId, queueJson;
  QStringList queueUrls;
  QCache<QString, CachedPlaylistItem> playlistCache{128};
  QPointer<QNetworkReply> playlistReply;
  QString dragSource, dragQueue;
  bool shuttingDown = false;
  enum obs_media_state state = OBS_MEDIA_STATE_NONE;
  int64_t duration = 0;
  bool seeking = false;
  int refreshCount = 0;
  Ref selected() { return sourceRef(sources->currentData().toString()); }
  void themeIcon(QToolButton *b, const char *themeClass,
                 QStyle::StandardPixmap fallback, bool force = false) {
    if (!force && b->property("class").toString() == QLatin1String(themeClass))
      return;
    b->setProperty("class", themeClass);
    b->setIcon(b->style()->standardIcon(fallback, nullptr, b));
    // OBS theme classes supply qproperty-icon. Match the native media toolbar:
    // repolish only on state transitions or actual theme changes.
    b->style()->unpolish(b);
    b->style()->polish(b);
    b->update();
  }
  QToolButton *tool(QStyle::StandardPixmap icon, const QString &tip,
                    const char *themeClass) {
    auto b = new QToolButton(this);
    b->setProperty("mediaFallbackIcon", int(icon));
    themeIcon(b, themeClass, icon, true);
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    b->setAutoRaise(true);
    return b;
  }
  void updatePlayIcon(bool force = false) {
    bool playing = state == OBS_MEDIA_STATE_PLAYING;
    auto fallback = playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay;
    play->setProperty("mediaFallbackIcon", int(fallback));
    themeIcon(play, playing ? "icon-media-pause" : "icon-media-play", fallback,
              force);
  }
  void scalePreview() {
    if (!themeReady)
      return;
    int w =
        std::clamp(std::min(fontMetrics().height() * 6, width() / 3), 64, 160);
    QSize size(w, w * 9 / 16);
    if (thumbnail->size() != size)
      thumbnail->setFixedSize(size);
    if (!previewImage.isNull()) {
      qreal dpr = thumbnail->devicePixelRatioF();
      auto pix = QPixmap::fromImage(previewImage.scaled(
          size * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation));
      pix.setDevicePixelRatio(dpr);
      thumbnail->setPixmap(pix);
    }
  }
  void applyTheme() {
    if (!themeReady)
      return;
    auto metric = [&](QStyle::PixelMetric m, int fallback) {
      int v = style()->pixelMetric(m, nullptr, this);
      return v >= 0 ? v : fallback;
    };
    int margin = metric(QStyle::PM_LayoutLeftMargin, 8);
    int spacing = metric(QStyle::PM_LayoutVerticalSpacing, 6);
    mainLayout->setContentsMargins(margin, margin, margin, margin);
    mainLayout->setSpacing(spacing);
    int iconSize = std::max(12, metric(QStyle::PM_SmallIconSize, 16));
    for (auto b : findChildren<QToolButton *>()) {
      b->setIconSize(QSize(iconSize, iconSize));
      if (b == play)
        updatePlayIcon(true);
      else if (b->property("mediaFallbackIcon").isValid()) {
        auto cls = b->property("class").toString().toUtf8();
        themeIcon(
            b, cls.constData(),
            QStyle::StandardPixmap(b->property("mediaFallbackIcon").toInt()),
            true);
      }
    }
    scalePreview();
    queue->doItemsLayout();
    queue->viewport()->update();
  }
  void scheduleTheme() {
    if (!themeReady || themeRefreshPending)
      return;
    themeRefreshPending = true;
    QTimer::singleShot(0, this, [this] {
      themeRefreshPending = false;
      applyTheme();
    });
  }
  void action(const QString &op, const QString &value = {}, int row = -1,
              int target = -1) {
    auto s = selected();
    if (!s)
      return;
    calldata_t data;
    calldata_init(&data);
    calldata_set_string(&data, "op", qPrintable(op));
    calldata_set_string(&data, "url", qPrintable(value));
    calldata_set_int(&data, "row", row);
    calldata_set_int(&data, "target", target);
    proc_handler_call(obs_source_get_proc_handler(s.get()),
                      "streaming_media_command", &data);
    calldata_free(&data);
  }
  void queueAction(const QString &op, int target = -1) {
    int row = queue->currentRow();
    if (row >= 0 && row < queueUrls.size())
      action(op, queueUrls[row], row, target);
  }
  void submit(bool immediate) {
    auto value = url->text().trimmed();
    if (importProcess || !importReceipt.isEmpty())
      return;
    auto playlist = canonicalPlaylistUrl(value);
    auto id = videoId(value);
    if (!playlist.isEmpty()) {
      bool entire = id.isEmpty();
      if (!id.isEmpty()) {
        QMenu choice(this);
        auto video = choice.addAction("This video");
        auto all = choice.addAction("Entire playlist");
        auto chosen = choice.exec(url->mapToGlobal(QPoint(0, url->height())));
        if (!chosen)
          return;
        entire = chosen == all;
      }
      if (entire)
        beginImport(playlist, immediate);
      else
        action(immediate ? "load" : "append",
               "https://www.youtube.com/watch?v=" + id);
    } else if (auto chosenSource = selected();
               chosenSource &&
               validSourceMediaUrl(
                   value,
                   QString::fromUtf8(obs_source_get_unversioned_id(
                       chosenSource.get())) == "streaming_media_audio")) {
      action(immediate ? "load" : "append", value);
    } else {
      url->setToolTip(
          "Enter a supported URL for the selected media or audio source");
      url->setFocus();
      return;
    }
    url->clear();
    url->setToolTip({});
  }
  void refreshSources() {
    QList<QPair<QString, QString>> list;
    obs_enum_sources(
        [](void *param, obs_source_t *s) {
          if (!obs_source_removed(s) &&
              (QString::fromUtf8(obs_source_get_unversioned_id(s)) ==
                   "streaming_media_youtube" ||
               QString::fromUtf8(obs_source_get_unversioned_id(s)) ==
                   "streaming_media_audio"))
            static_cast<QList<QPair<QString, QString>> *>(param)->append(
                {QString::fromUtf8(obs_source_get_uuid(s)),
                 QString::fromUtf8(obs_source_get_name(s))});
          return true;
        },
        &list);
    std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
      return a.second.localeAwareCompare(b.second) < 0;
    });
    bool same = list.size() == sources->count();
    if (same)
      for (int i = 0; i < list.size(); i++)
        if (sources->itemData(i) != list[i].first ||
            sources->itemText(i) != list[i].second) {
          same = false;
          break;
        }
    if (same)
      return;
    auto old = sources->currentData().toString();
    QSignalBlocker block(sources);
    sources->clear();
    for (const auto &p : list)
      sources->addItem(p.second, p.first);
    int idx = sources->findData(old);
    if (idx >= 0)
      sources->setCurrentIndex(idx);
  }
  void preview(const QString &id, const QString &imageUrl = {},
               bool preferFrame = false) {
    if (id == thumbId) {
      if (!preferFrame && previewWait.isValid() &&
          previewWait.elapsed() > 5000 && previewImage.isNull() &&
          !thumbnailReply) {
        thumbId.clear();
      } else
        return;
    }
    thumbId = id;
    seenPreviewGeneration = 0;
    previewWait.invalidate();
    thumbnail->setProperty("generatedPreview", false);
    thumbnail->setToolTip({});
    if (thumbnailReply) {
      auto old = thumbnailReply.data();
      thumbnailReply = nullptr;
      old->abort();
      old->deleteLater();
    }
    previewImage = QImage{};
    thumbnail->setPixmap({});
    bool external = id.startsWith("provider:");
    auto provider = external ? mediaProvider(id.mid(9).section('|', 0, 0))
                             : MediaProvider::YouTube;
    thumbnail->setText(id.isEmpty() ? "No video" : "Preview unavailable");
    if (preferFrame) {
      previewWait.start();
      return;
    }
    if (id.isEmpty() || !isVisible() ||
        (external && !validThumbnailUrl(imageUrl, provider)))
      return;
    QNetworkRequest request(
        QUrl(external ? imageUrl
                      : "https://i.ytimg.com/vi/" + id + "/mqdefault.jpg"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(8000);
    auto reply = network->get(request);
    thumbnailReply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [reply](qint64 received, qint64 total) {
              if (received > 1024 * 1024 || total > 1024 * 1024)
                reply->abort();
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
      if (thumbnailReply == reply) {
        thumbnailReply = nullptr;
        if (id == thumbId && reply->error() == QNetworkReply::NoError &&
            reply->bytesAvailable() <= 1024 * 1024) {
          auto bytes = reply->readAll();
          auto image = decodeThumbnail(bytes, QSize(320, 180));
          if (!image.isNull()) {
            thumbnail->setText({});
            previewImage = image;
            scalePreview();
          }
        }
      }
      reply->deleteLater();
    });
  }
  void generatedPreview(obs_source_t *source, const QString &current) {
    if (!isVisible() || current.isEmpty())
      return;
    calldata_t call;
    calldata_init(&call);
    calldata_set_int(&call, "seen", seenPreviewGeneration);
    proc_handler_call(obs_source_get_proc_handler(source),
                      "streaming_media_preview", &call);
    auto gen = calldata_int(&call, "generation");
    auto raw = calldata_string(&call, "pixels");
    auto image = raw ? decodePreviewFrame(QByteArray(raw),
                                          int(calldata_int(&call, "width")),
                                          int(calldata_int(&call, "height")))
                     : QImage{};
    calldata_free(&call);
    if (gen != seenPreviewGeneration && seenPreviewGeneration) {
      // A reload of the same URL must not be mistaken for an already
      // captured generation. Keep the old still while new media opens.
      seenPreviewGeneration = 0;
    }
    if (image.isNull())
      return;
    seenPreviewGeneration = gen;
    previewWait.invalidate();
    if (thumbnailReply) {
      auto old = thumbnailReply.data();
      thumbnailReply = nullptr;
      old->abort();
      old->deleteLater();
    }
    previewImage = image;
    thumbnail->setText({});
    thumbnail->setProperty("generatedPreview", true);
    thumbnail->setToolTip("Preview captured from playback");
    scalePreview();
    auto key = cacheKey(current);
    if (!key.isEmpty()) {
      auto cached = playlistCache.object(key);
      if (!cached) {
        cached = new CachedPlaylistItem;
        playlistCache.insert(key, cached);
      }
      cached->image =
          image.scaled(160, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      cached->imageDone = true;
      cached->generated = true;
      refreshQueueMetadata(key);
    }
  }
  void decorateQueueItem(QListWidgetItem *item) {
    auto u = item->data(UrlRole).toString();
    auto id = cacheKey(u);
    auto cached = playlistCache.object(id);
    auto title = cached && !cached->metadata.title.isEmpty()
                     ? cached->metadata.title
                     : mediaLabel(u);
    item->setText(title);
    auto author =
        cached                                      ? cached->metadata.author
        : mediaProvider(u) == MediaProvider::Twitch ? QString("Twitch")
        : mediaProvider(u) == MediaProvider::SoundCloud ? QString("SoundCloud")
        : mediaProvider(u) == MediaProvider::Bandcamp   ? QString("Bandcamp")
        : mediaProvider(u) == MediaProvider::Kick       ? QString("Kick")
                                                        : QString{};
    if (cached && cached->metadata.durationMs >= 0) {
      if (!author.isEmpty())
        author += " · ";
      author += clockText(cached->metadata.durationMs);
    }
    item->setData(AuthorRole, author);
    // Retain the original URL for accessible navigation and diagnostics.
    item->setToolTip("<qt>" + title.toHtmlEscaped() + "<br>" +
                     (cached ? (cached->metadata.details.isEmpty()
                                    ? cached->metadata.author.toHtmlEscaped()
                                    : infoTooltip(cached->metadata.details))
                             : QString{}) +
                     "<br>" + u.toHtmlEscaped() + "</qt>");
    item->setIcon(cached && !cached->image.isNull()
                      ? QIcon(QPixmap::fromImage(cached->image))
                      : QIcon{});
  }
  void refreshQueueMetadata(const QString &key) {
    for (int i = 0; i < queue->count(); ++i) {
      auto item = queue->item(i);
      if (cacheKey(item->data(UrlRole).toString()) == key)
        decorateQueueItem(item);
    }
    updateQueueDuration();
  }
  void cancelMetadata() {
    if (!metadataProcess)
      return;
    auto p = metadataProcess;
    metadataProcess = nullptr;
    disconnect(p, nullptr, this, nullptr);
    p->kill();
    p->waitForFinished(1000);
    p->deleteLater();
    metadataBytes.clear();
  }
  void fetchProviderMetadata(const QString &key) {
    auto engine = selectMediaRuntime(runtimeBundle, runtimeManaged);
    if (!engine.ready())
      return;
    auto p = new QProcess(this);
    metadataProcess = p;
    metadataBytes.clear();
    metadataOverflow = false;
    p->setProgram(engine.extractor);
    p->setArguments({"--ignore-config", "--no-playlist", "--skip-download",
                     "--no-warnings", "--no-progress", "--socket-timeout", "8",
                     "--retries", "0", "--use-extractors",
                     providerExtractor(key), "-J", "--", key});
    auto drain = [this, p] {
      if (metadataProcess != p)
        return;
      auto bytes = p->readAllStandardOutput();
      if (metadataBytes.size() + bytes.size() > 4 * 1024 * 1024) {
        metadataOverflow = true;
        p->kill();
      } else if (!metadataOverflow)
        metadataBytes += bytes;
      p->readAllStandardError();
    };
    auto finish = [this, p, key, drain](bool ok) {
      if (metadataProcess != p)
        return;
      drain();
      metadataProcess = nullptr;
      p->deleteLater();
      auto cached = playlistCache.object(key);
      if (cached) {
        cached->titleDone = true;
        if (ok && !metadataOverflow) {
          auto doc = QJsonDocument::fromJson(metadataBytes);
          auto root = doc.object();
          auto expected = providerExtractorKey(key);
          if (root.value("extractor_key").toString() == expected) {
            auto info = normalizedMediaInfo(root, mediaProvider(key));
            cached->metadata.title = info.value("title").toString();
            cached->metadata.author = info.value("author").toString();
            cached->metadata.durationMs =
                qint64(info.value("durationMs").toDouble(-1));
            cached->metadata.details = info;
          }
        }
        refreshQueueMetadata(key);
      }
      metadataBytes.clear();
    };
    connect(p, &QProcess::readyReadStandardOutput, this, drain);
    connect(p, &QProcess::readyReadStandardError, this,
            [p] { p->readAllStandardError(); });
    connect(p, &QProcess::errorOccurred, this,
            [finish](QProcess::ProcessError e) {
              if (e == QProcess::FailedToStart)
                finish(false);
            });
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [finish](int code, QProcess::ExitStatus exit) {
              finish(code == 0 && exit == QProcess::NormalExit);
            });
    QTimer::singleShot(20000, p, [this, p] {
      if (metadataProcess == p) {
        metadataOverflow = true;
        p->kill();
      }
    });
    p->start();
  }
  void fetchVisibleMetadata() {
    if (shuttingDown || playlistReply || metadataProcess || importProcess ||
        !isVisible() || !queue->isVisible() || !queue->count())
      return;
    for (int row = 0; row < queue->count(); ++row) {
      auto item = queue->item(row);
      auto rect = queue->visualItemRect(item);
      if (rect.bottom() < 0)
        continue;
      if (rect.top() >= queue->viewport()->height())
        break;
      auto original = item->data(UrlRole).toString();
      auto id = cacheKey(original);
      auto provider = mediaProvider(original);
      bool twitch = provider != MediaProvider::YouTube &&
                    provider != MediaProvider::Unknown;
      if (id.isEmpty())
        continue;
      auto cached = playlistCache.object(id);
      if (!cached) {
        cached = new CachedPlaylistItem;
        playlistCache.insert(id, cached);
      }
      if (cached->titleDone && cached->imageDone)
        continue;
      bool metadata = !cached->titleDone;
      QUrl address;
      if (twitch && metadata) {
        fetchProviderMetadata(id);
        return;
      }
      if (twitch) {
        auto url = cached->metadata.details.value("thumbnail").toString();
        if (!validThumbnailUrl(url, provider)) {
          cached->imageDone = true;
          continue;
        }
        address = QUrl(url);
      } else if (metadata) {
        address = QUrl("https://www.youtube.com/oembed");
        QUrlQuery query;
        query.addQueryItem("url", "https://www.youtube.com/watch?v=" + id);
        query.addQueryItem("format", "json");
        address.setQuery(query);
      } else
        address = QUrl("https://i.ytimg.com/vi/" + id + "/mqdefault.jpg");
      QNetworkRequest request(address);
      request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                           QNetworkRequest::ManualRedirectPolicy);
      request.setTransferTimeout(8000);
      const qint64 limit = metadata ? 65536 : 1048576;
      auto reply = network->get(request);
      playlistReply = reply;
      connect(reply, &QNetworkReply::downloadProgress, this,
              [reply, limit](qint64 got, qint64 total) {
                if (got > limit || total > limit)
                  reply->abort();
              });
      connect(reply, &QNetworkReply::finished, this,
              [this, reply, id, metadata, limit] {
                if (playlistReply != reply) {
                  reply->deleteLater();
                  return;
                }
                playlistReply = nullptr;
                if (!shuttingDown) {
                  auto cached = playlistCache.object(id);
                  if (cached) {
                    if (metadata)
                      cached->titleDone = true;
                    else
                      cached->imageDone = true;
                    if (reply->error() == QNetworkReply::NoError &&
                        reply->bytesAvailable() <= limit) {
                      auto bytes = reply->readAll();
                      if (metadata) {
                        auto parsed = parsePlaylistMetadata(bytes);
                        if (!parsed.title.isEmpty())
                          cached->metadata.title = parsed.title;
                        if (!parsed.author.isEmpty())
                          cached->metadata.author = parsed.author;
                      } else if (!cached->generated) {
                        cached->image = decodeThumbnail(bytes, QSize(160, 90));
                      }
                    }
                    refreshQueueMetadata(id);
                  }
                }
                reply->deleteLater();
                // The next normal refresh issues another visible-row request.
              });
      return;
    }
  }
  void setQueue(const QString &json) {
    if (json == queueJson)
      return;
    int previous = queue->currentRow();
    queueJson = json;
    queueUrls = MediaQueue::fromJson(json).items();
    QSignalBlocker b(queue);
    queue->clear();
    for (const auto &u : queueUrls) {
      auto item = new QListWidgetItem(queue);
      item->setData(UrlRole, u);
      decorateQueueItem(item);
    }
    if (previous >= 0 && !queueUrls.isEmpty())
      queue->setCurrentRow(std::min(previous, int(queueUrls.size() - 1)));
    updateQueueDuration();
  }
  void refresh() {
    if (shuttingDown)
      return;
    pollImportReceipt();
    if (refreshCount++ % 4 == 0)
      refreshSources();
    auto s = selected();
    auto id = sources->currentData().toString();
    if (id != sourceId) {
      cancelMetadata();
      if (importProcess)
        cancelImport("Import cancelled · source changed");
      sourceId = id;
      queueJson.clear();
      queueUrls.clear();
      queue->clear();
      thumbId.clear();
      seeking = false;
    }
    bool present = bool(s);
    bool live = false;
    for (QWidget *w :
         std::initializer_list<QWidget *>{url, load, append, queue, autoplay})
      w->setEnabled(present);
    load->setEnabled(present && !importProcess && importReceipt.isEmpty());
    append->setEnabled(present && !importProcess && importReceipt.isEmpty());
    if (!present) {
      title->setText("Add a Streaming Media source");
      mediaAuthor->clear();
      mediaAuthor->hide();
      status->setText("Choose Sources → + → Streaming Media");
      setQueue("[]");
      preview({});
      duration = 0;
      state = OBS_MEDIA_STATE_NONE;
    } else {
      auto d = obs_source_get_settings(s.get());
      auto current = QString::fromUtf8(obs_data_get_string(d, "url"));
      setQueue(QString::fromUtf8(obs_data_get_string(d, "queue")));
      {
        QSignalBlocker b(autoplay);
        autoplay->setChecked(obs_data_get_bool(d, "auto_advance"));
      }
      obs_data_release(d);
      calldata_t call;
      calldata_init(&call);
      proc_handler_call(obs_source_get_proc_handler(s.get()),
                        "streaming_media_status", &call);
      auto name = QString::fromUtf8(calldata_string(&call, "title"));
      auto error = QString::fromUtf8(calldata_string(&call, "error"));
      auto w = calldata_int(&call, "width"), h = calldata_int(&call, "height");
      calldata_free(&call);
      title->setText(!name.isEmpty()     ? name
                     : current.isEmpty() ? "No video loaded"
                                         : mediaLabel(current));
      calldata_t infoCall;
      calldata_init(&infoCall);
      proc_handler_call(obs_source_get_proc_handler(s.get()),
                        "streaming_media_info", &infoCall);
      auto raw = calldata_string(&infoCall, "json");
      auto info = raw ? QJsonDocument::fromJson(QByteArray(raw)).object()
                      : QJsonObject{};
      calldata_free(&infoCall);
      auto key = cacheKey(current);
      if (!key.isEmpty() && !info.value("title").toString().isEmpty()) {
        auto cached = playlistCache.object(key);
        if (!cached) {
          cached = new CachedPlaylistItem;
          playlistCache.insert(key, cached);
        }
        if (cached->metadata.details != info) {
          if (!cached->generated &&
              mediaProvider(current) != MediaProvider::YouTube &&
              cached->metadata.details.value("thumbnail") !=
                  info.value("thumbnail")) {
            cached->imageDone = false;
            cached->image = QImage{};
          }
          cached->metadata.details = info;
          cached->metadata.title = info.value("title").toString();
          cached->metadata.author = info.value("author").toString();
          cached->metadata.durationMs =
              qint64(info.value("durationMs").toDouble(-1));
          cached->titleDone = true;
          refreshQueueMetadata(key);
        }
      }
      QStringList subtitle;
      for (auto key : {"author", "category"}) {
        auto value = info.value(key).toString();
        if (!value.isEmpty())
          subtitle << value;
      }
      mediaAuthor->setText(subtitle.join(" · "));
      mediaAuthor->setVisible(!subtitle.isEmpty());
      auto details = infoTooltip(info);
      auto tip = "<qt>" + title->text().toHtmlEscaped() +
                 (details.isEmpty() ? QString{} : "<br>" + details) + "</qt>";
      title->setToolTip(tip);
      mediaAuthor->setToolTip(tip);

      state = obs_source_media_get_state(s.get());
      duration = obs_source_media_get_duration(s.get());
      live = duration <= 0 &&
             (providerExtractor(current) == "twitch:stream" ||
              providerExtractor(current) == "kick:live") &&
             (state == OBS_MEDIA_STATE_PLAYING ||
              state == OBS_MEDIA_STATE_PAUSED ||
              state == OBS_MEDIA_STATE_BUFFERING);
      auto text = stateText(state);
      if (live)
        text += " · Live";
      if (w && h)
        text += QString(" · %1 × %2").arg(w).arg(h);
      status->setText(error.isEmpty() ? text : error);
      status->setToolTip(status->text());
      auto twitch = canonicalProviderUrl(current);
      auto imageUrl = info.value("thumbnail").toString();
      const bool playingFrame =
          w > 0 && h > 0 &&
          (state == OBS_MEDIA_STATE_PLAYING || state == OBS_MEDIA_STATE_PAUSED);
      preview(twitch.isEmpty() ? videoId(current)
                               : "provider:" + twitch + "|" + imageUrl,
              imageUrl,
              playingFrame &&
                  (!previewWait.isValid() || previewWait.elapsed() < 5000));
      if (playingFrame)
        generatedPreview(s.get(), current);
    }
    auto pos = present ? obs_source_media_get_time(s.get()) : 0;
    if (!seeking && !timeline->isSliderDown()) {
      QSignalBlocker b(timeline);
      timeline->setValue(duration > 0
                             ? int(std::clamp(pos * 10000 / duration,
                                              int64_t(0), int64_t(10000)))
                             : 0);
      time->setText(live ? "Live"
                         : clockText(pos) + " / " + clockText(duration));
    }
    timeline->setEnabled(present && duration > 0);
    updatePlayIcon();
    play->setToolTip(state == OBS_MEDIA_STATE_PLAYING ? "Pause" : "Play");
    for (auto b : {play, stop, retry})
      b->setEnabled(present);
    back->setEnabled(present && duration > 0);
    forward->setEnabled(present && duration > 0);
    next->setEnabled(present && !queueUrls.isEmpty());
    int row = queue->currentRow();
    up->setEnabled(present && row > 0);
    down->setEnabled(present && row >= 0 && row + 1 < queueUrls.size());
    remove->setEnabled(present && row >= 0);
    fetchVisibleMetadata();
  }
  void skip(int64_t delta) {
    auto s = selected();
    if (s && duration > 0)
      obs_source_media_set_time(
          s.get(), std::clamp(obs_source_media_get_time(s.get()) + delta,
                              int64_t(0), duration));
  }

public:
  MediaDock(const QString &bundle, const QString &managed)
      : runtimeBundle(bundle), runtimeManaged(managed) {
    setObjectName("streaming-media-controls");
    auto layout = new QVBoxLayout(this);
    mainLayout = layout;
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    sources = new QComboBox(this);
    sources->setObjectName("media-source-picker");
    sources->setToolTip("Streaming Media source to control");
    layout->addWidget(sources);
    auto input = new QHBoxLayout;
    url = new QLineEdit(this);
    url->setObjectName("media-url");
    url->setPlaceholderText("Media or playlist URL");
    url->setClearButtonEnabled(true);
    url->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    load = new QPushButton("Play", this);
    load->setObjectName("media-url-play");
    append = new QPushButton("Queue", this);
    append->setObjectName("media-queue-add");
    input->addWidget(url, 1);
    input->addWidget(load);
    input->addWidget(append);
    layout->addLayout(input);
    auto importRow = new QHBoxLayout;
    importStatus = new QLabel(this);
    importStatus->setObjectName("media-import-status");
    importStatus->setTextFormat(Qt::PlainText);
    importStatus->setWordWrap(true);
    importStatus->setProperty("class", "text-muted");
    importStatus->hide();
    importCancel = tool(QStyle::SP_DialogCancelButton, "Cancel playlist import",
                        "icon-trash");
    importCancel->setObjectName("media-import-cancel");
    importCancel->hide();
    connect(importCancel, &QToolButton::clicked, this,
            [this] { cancelImport("Playlist import cancelled"); });
    importRow->addWidget(importStatus, 1);
    importRow->addWidget(importCancel);
    layout->addLayout(importRow);
    auto now = new QHBoxLayout;
    thumbnail = new QLabel("No video", this);
    thumbnail->setObjectName("media-thumbnail");
    thumbnail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    thumbnail->setAlignment(Qt::AlignCenter);
    thumbnail->setFrameShape(QFrame::StyledPanel);
    now->addWidget(thumbnail);
    auto meta = new QVBoxLayout;
    title = new QLabel(this);
    title->setObjectName("media-title");
    title->setWordWrap(true);
    title->setTextFormat(Qt::PlainText);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title->setProperty("class", "text-bold");
    status = new QLabel(this);
    status->setProperty("class", "text-muted");
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    mediaAuthor = new QLabel(this);
    mediaAuthor->setObjectName("media-author");
    mediaAuthor->setTextFormat(Qt::PlainText);
    mediaAuthor->setWordWrap(true);
    mediaAuthor->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    mediaAuthor->setProperty("class", "text-muted");
    mediaAuthor->hide();
    meta->addWidget(title);
    meta->addWidget(mediaAuthor);
    meta->addWidget(status);
    now->addLayout(meta, 1);
    layout->addLayout(now);
    timeline = new QSlider(Qt::Horizontal, this);
    timeline->setRange(0, 10000);
    time = new QLabel("0:00 / 0:00", this);
    time->setAlignment(Qt::AlignRight);
    time->setProperty("class", "text-muted");
    layout->addWidget(timeline);
    layout->addWidget(time);
    auto transport = new QHBoxLayout;
    back = new QToolButton(this);
    back->setText("−10");
    back->setToolTip("Back 10 seconds");
    back->setAccessibleName("Back 10 seconds");
    back->setAutoRaise(true);
    play = tool(QStyle::SP_MediaPlay, "Play", "icon-media-play");
    play->setObjectName("media-play");
    stop = tool(QStyle::SP_MediaStop, "Stop", "icon-media-stop");
    forward = new QToolButton(this);
    forward->setText("+10");
    forward->setToolTip("Forward 10 seconds");
    forward->setAccessibleName("Forward 10 seconds");
    forward->setAutoRaise(true);
    next = tool(QStyle::SP_MediaSkipForward, "Play next queued video",
                "icon-media-next");
    next->setObjectName("media-next");
    retry = tool(QStyle::SP_BrowserReload, "Resolve and restart video",
                 "icon-media-restart");
    for (auto b : {back, play, stop, forward, next, retry})
      transport->addWidget(b);
    transport->addStretch();
    layout->addLayout(transport);
    auto header = new QHBoxLayout;
    count = new QLabel("Up next · 0", this);
    header->addWidget(count, 1);
    autoplay = new QCheckBox("Auto advance", this);
    header->addWidget(autoplay);
    layout->addLayout(header);
    queueDuration = new QLabel(this);
    queueDuration->setObjectName("media-queue-duration");
    queueDuration->setProperty("class", "text-muted");
    queueDuration->hide();
    layout->addWidget(queueDuration);
    queue = new PlaylistView(this);
    queue->setItemDelegate(new PlaylistDelegate(queue));
    queue->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    queue->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    queue->dragStarted = [this] {
      dragSource = sourceId;
      dragQueue = queueJson;
    };
    queue->dropped = [this](int from, int target) {
      if (sourceId != dragSource || queueJson != dragQueue || from < 0 ||
          from >= queueUrls.size())
        return;
      action("move", queueUrls[from], from, target);
    };
    queue->setObjectName("media-queue-list");
    queue->setAlternatingRowColors(true);
    queue->setMinimumHeight(0);
    layout->addWidget(queue, 1);
    auto edits = new QHBoxLayout;
    up = tool(QStyle::SP_ArrowUp, "Move up", "icon-up");
    up->setObjectName("media-queue-up");
    down = tool(QStyle::SP_ArrowDown, "Move down", "icon-down");
    remove = tool(QStyle::SP_TrashIcon, "Remove queued video", "icon-trash");
    remove->setObjectName("media-queue-remove");
    for (auto b : {up, down, remove})
      edits->addWidget(b);
    edits->addStretch();
    layout->addLayout(edits);
    auto engineRow = new QHBoxLayout;
    engineStatus = new QLabel(this);
    engineStatus->setObjectName("media-engine-status");
    engineStatus->setProperty("class", "text-muted");
    engineStatus->setTextFormat(Qt::PlainText);
    engineStatus->setWordWrap(true);
    engineStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    engineMenu = tool(QStyle::SP_BrowserReload, "Media engine options",
                      "icon-media-restart");
    engineMenu->setObjectName("media-engine-menu");
    auto menu = new QMenu(engineMenu);
    auto yt = menu->addAction("Download yt-dlp");
    yt->setObjectName("media-engine-download-ytdlp");
    auto deno = menu->addAction("Download Deno");
    deno->setObjectName("media-engine-download-deno");
    auto folder = menu->addAction("Open runtime folder");
    folder->setObjectName("media-engine-open-folder");
    auto refreshAction = menu->addAction("Refresh engine status");
    refreshAction->setObjectName("media-engine-refresh");
    auto guide = menu->addAction("Setup instructions");
    guide->setObjectName("media-engine-guide");
    connect(yt, &QAction::triggered, this, [] {
      QDesktopServices::openUrl(
          QUrl("https://github.com/yt-dlp/yt-dlp#release-files"));
    });
    connect(deno, &QAction::triggered, this, [] {
      QDesktopServices::openUrl(
          QUrl("https://github.com/denoland/deno/releases/latest"));
    });
    connect(folder, &QAction::triggered, this, [this] {
      auto path = manualRuntimeDirectory(runtimeManaged);
      if (path.isEmpty() || !QDir().mkpath(path) ||
          !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        engineStatus->setText("Cannot open runtime folder");
    });
    connect(refreshAction, &QAction::triggered, this,
            [this] { refreshEngine(); });
    connect(guide, &QAction::triggered, this, [] {
      QDesktopServices::openUrl(
          QUrl("https://github.com/myrqyry/streaming-media-source/blob/master/"
               "docs/runtime-setup.md"));
    });
    menu->addSeparator();
    auto check = menu->addAction("Check for extractor updates");
    check->setObjectName("media-engine-check");
    auto reset = menu->addAction("Use bundled extractor");
    reset->setObjectName("media-engine-reset");
    connect(check, &QAction::triggered, this, [this] { checkEngine(); });
    connect(reset, &QAction::triggered, this, [this] {
      if (updateWorker)
        return;
      if (clearRuntimeOverride(runtimeManaged)) {
        refreshEngine();
        engineStatus->setToolTip(
            "Bundled extractor will be used for the next resolution");
      } else
        engineStatus->setText("Could not restore bundled extractor");
    });
    connect(menu, &QMenu::aboutToShow, this, [this, check, reset, folder] {
      refreshEngine();
      folder->setEnabled(!runtimeManaged.isEmpty());
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
      auto bundled = findMediaRuntime(runtimeBundle, {}, false).ready();
      check->setEnabled(bundled && !runtimeManaged.isEmpty() && !updateWorker);
      reset->setEnabled(bundled &&
                        !activeRuntimeVersion(runtimeManaged).isEmpty() &&
                        !updateWorker);
#else
      check->setEnabled(false);
      reset->setEnabled(false);
#endif
    });
    engineMenu->setMenu(menu);
    engineMenu->setPopupMode(QToolButton::InstantPopup);
    engineRow->addWidget(engineStatus, 1);
    engineRow->addWidget(engineMenu);
    layout->addLayout(engineRow);
    refreshEngine();
    network = new QNetworkAccessManager(this);
    timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this] {
      if (isVisible())
        refresh();
    });
    connect(sources, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { refresh(); });
    connect(load, &QPushButton::clicked, this, [this] { submit(true); });
    connect(append, &QPushButton::clicked, this, [this] { submit(false); });
    connect(url, &QLineEdit::returnPressed, this, [this] { submit(true); });
    connect(play, &QToolButton::clicked, this, [this] {
      auto s = selected();
      if (s)
        obs_source_media_play_pause(s.get(), state == OBS_MEDIA_STATE_PLAYING);
    });
    connect(stop, &QToolButton::clicked, this, [this] {
      auto s = selected();
      if (s)
        obs_source_media_stop(s.get());
    });
    connect(retry, &QToolButton::clicked, this, [this] {
      auto s = selected();
      if (s)
        obs_source_media_restart(s.get());
    });
    connect(next, &QToolButton::clicked, this, [this] {
      auto s = selected();
      if (s)
        obs_source_media_next(s.get());
    });
    connect(back, &QToolButton::clicked, this, [this] { skip(-10000); });
    connect(forward, &QToolButton::clicked, this, [this] { skip(10000); });
    connect(timeline, &QSlider::sliderPressed, this,
            [this] { seeking = true; });
    connect(timeline, &QSlider::sliderMoved, this, [this](int v) {
      if (duration > 0)
        time->setText(clockText(duration * v / 10000) + " / " +
                      clockText(duration));
    });
    connect(timeline, &QSlider::sliderReleased, this, [this] {
      auto s = selected();
      if (s && duration > 0)
        obs_source_media_set_time(s.get(),
                                  duration * timeline->value() / 10000);
      seeking = false;
    });
    connect(queue, &QListWidget::itemDoubleClicked, this,
            [this] { queueAction("play"); });
    connect(queue, &QListWidget::currentRowChanged, this,
            [this] { refresh(); });
    connect(up, &QToolButton::clicked, this,
            [this] { queueAction("move", queue->currentRow() - 1); });
    connect(down, &QToolButton::clicked, this,
            [this] { queueAction("move", queue->currentRow() + 1); });
    connect(remove, &QToolButton::clicked, this,
            [this] { queueAction("remove"); });
    connect(autoplay, &QCheckBox::toggled, this, [this](bool checked) {
      action("autoplay", {}, -1, checked ? 1 : 0);
    });
    themeReady = true;
    applyTheme();
    timer->start();
    refresh();
  }
  void changeEvent(QEvent *e) override {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::StyleChange ||
        e->type() == QEvent::PaletteChange || e->type() == QEvent::FontChange ||
        e->type() == QEvent::ApplicationFontChange ||
        e->type() == QEvent::ApplicationPaletteChange)
      scheduleTheme();
  }
  void resizeEvent(QResizeEvent *e) override {
    QWidget::resizeEvent(e);
    scalePreview();
  }
  void themeChanged() { scheduleTheme(); }
  void showEvent(QShowEvent *e) override {
    QWidget::showEvent(e);
    scheduleTheme();
    refreshEngine();
    thumbId.clear();
    refreshCount = 0;
    refresh();
  }
  ~MediaDock() override {
    cancelMetadata();
    cancelImport({});
    stopEngineUpdate();
  }
  void shutdown() {
    cancelMetadata();
    cancelImport({});
    stopEngineUpdate();
    shuttingDown = true;
    timer->stop();
    if (playlistReply)
      playlistReply->abort();
    if (thumbnailReply)
      thumbnailReply->abort();
  }
};
QPointer<MediaDock> dock;
bool registered = false;
void frontendEvent(enum obs_frontend_event event, void *) {
  if (event == OBS_FRONTEND_EVENT_THEME_CHANGED && dock)
    dock->themeChanged();
  if (event == OBS_FRONTEND_EVENT_EXIT) {
    if (dock)
      dock->shutdown();
    registered = false;
  }
}
} // namespace
QWidget *createMediaDockWidget(const QString &bundle, const QString &managed) {
  return new MediaDock(bundle, managed);
}
void registerMediaDock(const QString &bundle, const QString &managed) {
  if (!qobject_cast<QApplication *>(QCoreApplication::instance()) ||
      !obs_frontend_get_main_window())
    return;
  auto widget =
      static_cast<MediaDock *>(createMediaDockWidget(bundle, managed));
  if (!obs_frontend_add_dock_by_id(dockId, "Streaming Media", widget)) {
    delete widget;
    return;
  }
  dock = widget;
  registered = true;
  obs_frontend_add_event_callback(frontendEvent, nullptr);
}
void unregisterMediaDock() {
  if (registered) {
    obs_frontend_remove_event_callback(frontendEvent, nullptr);
    if (dock)
      dock->shutdown();
    obs_frontend_remove_dock(dockId);
  }
  registered = false;
  dock = nullptr;
}
