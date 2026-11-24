#include "media-dock.hpp"
#include "media-queue.hpp"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
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
class MediaDock : public QWidget {
  QComboBox *sources;
  QLineEdit *url;
  QLabel *thumbnail, *title, *status, *time, *count;
  QSlider *timeline;
  QToolButton *play, *stop, *back, *forward, *next, *retry, *up, *down, *remove;
  QPushButton *load, *append;
  QListWidget *queue;
  QCheckBox *autoplay;
  QTimer *timer;
  QNetworkAccessManager *network;
  QPointer<QNetworkReply> thumbnailReply;
  QString thumbId, sourceId, queueJson;
  QStringList queueUrls;
  enum obs_media_state state = OBS_MEDIA_STATE_NONE;
  int64_t duration = 0;
  bool seeking = false;
  int refreshCount = 0;
  Ref selected() { return sourceRef(sources->currentData().toString()); }
  QToolButton *tool(QStyle::StandardPixmap icon, const QString &tip) {
    auto b = new QToolButton(this);
    b->setIcon(style()->standardIcon(icon));
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    b->setAutoRaise(true);
    return b;
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
                size.scaled(thumbnail->size(), Qt::KeepAspectRatio));
            auto image = reader.read();
            if (!image.isNull()) {
              thumbnail->setText({});
              thumbnail->setPixmap(QPixmap::fromImage(image));
            }
          }
        }
      }
      reply->deleteLater();
    });
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
      auto item = new QListWidgetItem(videoId(u), queue);
      item->setToolTip(u);
    }
    if (previous >= 0 && !queueUrls.isEmpty())
      queue->setCurrentRow(std::min(previous, int(queueUrls.size() - 1)));
    count->setText(QString("Up next · %1").arg(queueUrls.size()));
  }
  void refresh() {
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
      calldata_free(&call);
      title->setText(!name.isEmpty()     ? name
                     : current.isEmpty() ? "No video loaded"
                                         : videoId(current));
      title->setToolTip(title->text());
      state = obs_source_media_get_state(s.get());
      duration = obs_source_media_get_duration(s.get());
      auto text = stateText(state);
      auto w = obs_source_get_width(s.get()),
           h = obs_source_get_height(s.get());
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
    play->setIcon(style()->standardIcon(state == OBS_MEDIA_STATE_PLAYING
                                            ? QStyle::SP_MediaPause
                                            : QStyle::SP_MediaPlay));
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
  }
  void skip(int64_t delta) {
    auto s = selected();
    if (s && duration > 0)
      obs_source_media_set_time(
          s.get(), std::clamp(obs_source_media_get_time(s.get()) + delta,
                              int64_t(0), duration));
  }

public:
  MediaDock() {
    setObjectName("streaming-media-controls");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    sources = new QComboBox(this);
    sources->setObjectName("media-source-picker");
    sources->setToolTip("Streaming Media source to control");
    layout->addWidget(sources);
    auto input = new QHBoxLayout;
    url = new QLineEdit(this);
    url->setObjectName("media-url");
    url->setPlaceholderText("Paste YouTube URL");
    url->setClearButtonEnabled(true);
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
    thumbnail->setFixedSize(112, 63);
    thumbnail->setAlignment(Qt::AlignCenter);
    thumbnail->setFrameShape(QFrame::StyledPanel);
    now->addWidget(thumbnail);
    auto meta = new QVBoxLayout;
    title = new QLabel(this);
    title->setObjectName("media-title");
    title->setWordWrap(true);
    title->setTextFormat(Qt::PlainText);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    status = new QLabel(this);
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
    layout->addWidget(timeline);
    layout->addWidget(time);
    auto transport = new QHBoxLayout;
    back = tool(QStyle::SP_MediaSeekBackward, "Back 10 seconds");
    play = tool(QStyle::SP_MediaPlay, "Play");
    stop = tool(QStyle::SP_MediaStop, "Stop");
    forward = tool(QStyle::SP_MediaSeekForward, "Forward 10 seconds");
    next = tool(QStyle::SP_MediaSkipForward, "Play next queued video");
    next->setObjectName("media-next");
    retry = tool(QStyle::SP_BrowserReload, "Resolve and restart video");
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
    queue = new QListWidget(this);
    queue->setObjectName("media-queue-list");
    queue->setAlternatingRowColors(true);
    queue->setMinimumHeight(60);
    layout->addWidget(queue, 1);
    auto edits = new QHBoxLayout;
    up = tool(QStyle::SP_ArrowUp, "Move up");
    up->setObjectName("media-queue-up");
    down = tool(QStyle::SP_ArrowDown, "Move down");
    remove = tool(QStyle::SP_TrashIcon, "Remove queued video");
    remove->setObjectName("media-queue-remove");
    for (auto b : {up, down, remove})
      edits->addWidget(b);
    edits->addStretch();
    layout->addLayout(edits);
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
    timer->start();
    refresh();
  }
  void showEvent(QShowEvent *e) override {
    QWidget::showEvent(e);
    thumbId.clear();
    refreshCount = 0;
    refresh();
  }
  void shutdown() {
    timer->stop();
    if (thumbnailReply)
      thumbnailReply->abort();
  }
};
QPointer<MediaDock> dock;
bool registered = false;
void frontendEvent(enum obs_frontend_event event, void *) {
  if (event == OBS_FRONTEND_EVENT_EXIT) {
    if (dock)
      dock->shutdown();
    registered = false;
  }
}
} // namespace
QWidget *createMediaDockWidget() { return new MediaDock; }
void registerMediaDock() {
  if (!qobject_cast<QApplication *>(QCoreApplication::instance()) ||
      !obs_frontend_get_main_window())
    return;
  auto widget = static_cast<MediaDock *>(createMediaDockWidget());
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
