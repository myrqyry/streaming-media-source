#include "media-dock.hpp"
#include "media-queue.hpp"
#include "playlist-metadata.hpp"
#include "runtime-update.hpp"
#include <QApplication>
#include <QBuffer>
#include <QCache>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDrag>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPointer>
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
  bool titleDone = false, imageDone = false;
};
class MediaDock : public QWidget {
  QVBoxLayout *mainLayout = nullptr;
  QString runtimeBundle, runtimeManaged;
  QLabel *engineStatus = nullptr;
  QToolButton *engineMenu = nullptr;
  QThread *updateWorker = nullptr;
  std::shared_ptr<QString> updateResult;
  void refreshEngine() {
    if (!updateWorker && engineStatus)
      engineStatus->setText(
          runtimeDisplayVersion(runtimeBundle, runtimeManaged));
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
  bool themeRefreshPending = false, themeReady = false;
  QComboBox *sources;
  QLineEdit *url;
  QLabel *thumbnail, *title, *status, *time, *count;
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
    if (videoId(value).isEmpty()) {
      url->setToolTip("Enter a valid HTTPS YouTube video URL");
      url->setFocus();
      return;
    }
    action(immediate ? "load" : "append", value);
    url->clear();
    url->setToolTip({});
  }
  void refreshSources() {
    QList<QPair<QString, QString>> list;
    obs_enum_sources(
        [](void *param, obs_source_t *s) {
          if (!obs_source_removed(s) &&
              QString::fromUtf8(obs_source_get_unversioned_id(s)) ==
                  "streaming_media_youtube")
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
  void preview(const QString &id) {
    if (id == thumbId)
      return;
    thumbId = id;
    if (thumbnailReply) {
      auto old = thumbnailReply.data();
      thumbnailReply = nullptr;
      old->abort();
      old->deleteLater();
    }
    previewImage = QImage{};
    thumbnail->setPixmap({});
    thumbnail->setText(id.isEmpty() ? "No video" : "Preview unavailable");
    if (id.isEmpty() || !isVisible())
      return;
    QNetworkRequest request(
        QUrl("https://i.ytimg.com/vi/" + id + "/mqdefault.jpg"));
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
          QBuffer buffer(&bytes);
          buffer.open(QIODevice::ReadOnly);
          QImageReader reader(&buffer, "JPEG");
          auto size = reader.size();
          if (size.isValid() && size.width() <= 1920 && size.height() <= 1080) {
            reader.setScaledSize(
                size.scaled(QSize(320, 180), Qt::KeepAspectRatio));
            auto image = reader.read();
            if (!image.isNull()) {
              thumbnail->setText({});
              previewImage = image;
              scalePreview();
            }
          }
        }
      }
      reply->deleteLater();
    });
  }
  void decorateQueueItem(QListWidgetItem *item) {
    auto u = item->data(UrlRole).toString();
    auto id = videoId(u);
    auto cached = playlistCache.object(id);
    auto title = cached && !cached->metadata.title.isEmpty()
                     ? cached->metadata.title
                     : id;
    item->setText(title);
    item->setData(AuthorRole, cached ? cached->metadata.author : QString{});
    // Retain the original URL for accessible navigation and diagnostics.
    item->setToolTip(
        "<qt>" + title.toHtmlEscaped() + "<br>" +
        (cached ? cached->metadata.author.toHtmlEscaped() : QString{}) +
        "<br>" + u.toHtmlEscaped() + "</qt>");
    item->setIcon(cached && !cached->image.isNull()
                      ? QIcon(QPixmap::fromImage(cached->image))
                      : QIcon{});
  }
  void fetchVisibleMetadata() {
    if (shuttingDown || playlistReply || !isVisible() || !queue->isVisible() ||
        !queue->count())
      return;
    for (int row = 0; row < queue->count(); ++row) {
      auto item = queue->item(row);
      auto rect = queue->visualItemRect(item);
      if (rect.bottom() < 0)
        continue;
      if (rect.top() >= queue->viewport()->height())
        break;
      auto id = videoId(item->data(UrlRole).toString());
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
      if (metadata) {
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
                      if (metadata)
                        cached->metadata = parsePlaylistMetadata(bytes);
                      else {
                        QBuffer buffer(&bytes);
                        buffer.open(QIODevice::ReadOnly);
                        QImageReader reader(&buffer, "JPEG");
                        auto size = reader.size();
                        if (size.isValid() && size.width() <= 1920 &&
                            size.height() <= 1080) {
                          reader.setScaledSize(
                              size.scaled(QSize(160, 90), Qt::KeepAspectRatio));
                          cached->image = reader.read();
                        }
                      }
                    }
                    for (int i = 0; i < queue->count(); ++i) {
                      auto item = queue->item(i);
                      if (videoId(item->data(UrlRole).toString()) == id)
                        decorateQueueItem(item);
                    }
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
    count->setText(QString("Up next · %1").arg(queueUrls.size()));
  }
  void refresh() {
    if (shuttingDown)
      return;
    if (refreshCount++ % 4 == 0)
      refreshSources();
    auto s = selected();
    auto id = sources->currentData().toString();
    if (id != sourceId) {
      sourceId = id;
      queueJson.clear();
      queueUrls.clear();
      queue->clear();
      thumbId.clear();
      seeking = false;
    }
    bool present = bool(s);
    for (QWidget *w :
         std::initializer_list<QWidget *>{url, load, append, queue, autoplay})
      w->setEnabled(present);
    if (!present) {
      title->setText("Add a Streaming Media source");
      status->setText("Choose Sources → + → Streaming Media (YouTube)");
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
                                         : videoId(current));
      title->setToolTip("<qt>" + title->text().toHtmlEscaped() + "</qt>");
      state = obs_source_media_get_state(s.get());
      duration = obs_source_media_get_duration(s.get());
      auto text = stateText(state);
      if (w && h)
        text += QString(" · %1 × %2").arg(w).arg(h);
      status->setText(error.isEmpty() ? text : error);
      status->setToolTip(status->text());
      preview(videoId(current));
    }
    auto pos = present ? obs_source_media_get_time(s.get()) : 0;
    if (!seeking && !timeline->isSliderDown()) {
      QSignalBlocker b(timeline);
      timeline->setValue(duration > 0
                             ? int(std::clamp(pos * 10000 / duration,
                                              int64_t(0), int64_t(10000)))
                             : 0);
      time->setText(clockText(pos) + " / " + clockText(duration));
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
    url->setPlaceholderText("YouTube URL");
    url->setClearButtonEnabled(true);
    url->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    load = new QPushButton("Play", this);
    append = new QPushButton("Queue", this);
    append->setObjectName("media-queue-add");
    input->addWidget(url, 1);
    input->addWidget(load);
    input->addWidget(append);
    layout->addLayout(input);
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
    meta->addWidget(title);
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
    connect(menu, &QMenu::aboutToShow, this, [this, check, reset] {
      check->setEnabled(!runtimeBundle.isEmpty() && !runtimeManaged.isEmpty() &&
                        !updateWorker);
      reset->setEnabled(!activeRuntimeVersion(runtimeManaged).isEmpty() &&
                        !updateWorker);
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
    thumbId.clear();
    refreshCount = 0;
    refresh();
  }
  ~MediaDock() override { stopEngineUpdate(); }
  void shutdown() {
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
