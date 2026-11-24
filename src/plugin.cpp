#include "audio-handoff.hpp"
#include "media-dock.hpp"
#include "media-queue.hpp"
#include "media-recovery.hpp"
#include "media-runtime.hpp"
#include "media-websocket.hpp"
#include "resolver.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <obs-module.h>
#include <optional>
#include <thread>
OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void) {
  return "Streaming Media: native YouTube playback";
}
namespace {
struct Request {
  QString url;
  int height = 1080;
  bool hw = true;
  int speed = 100;
  int buffer = 8;
  bool restart = false;
  bool recover = true;
  int64_t resumeMs = 0;
  uint64_t id = 0;
};
struct QueueCommand {
  QString op, url;
  int row = -1, target = -1;
  QString requestId;
};
struct Result {
  Request req;
  Media media;
};
static MediaRuntime runtime() {
  char *path = obs_module_file("runtime");
  auto bundle = QString::fromUtf8(path ? path : "");
  bfree(path);
  return findMediaRuntime(bundle);
}
struct Source {
  obs_source_t *parent = nullptr, *child = nullptr;
  QTemporaryDir temp;
  std::atomic<bool> closing{false}, accept{false}, loading{false},
      failed{false}, finished{false};
  std::atomic<int64_t> finalDuration{0};
  std::atomic<uint32_t> videoWidth{0}, videoHeight{0};
  std::atomic<bool> decoderEnded{false}, decoderUpdated{false};
  bool waitingDecoder = false;
  std::unique_ptr<AudioHandoff> audioHandoff;
  std::atomic<uint64_t> epoch{0};
  std::mutex mutex;
  std::condition_variable cv;
  Request current;
  std::optional<Request> pending;
  std::optional<Result> result;
  std::thread worker;
  QString title, error;
  std::deque<QueueCommand> commands;
  QHash<QString, QString> requestResults;
  QStringList requestOrder;
  enum obs_media_state lastChildState = OBS_MEDIA_STATE_NONE;
  bool played = false, sessionLive = false;
  VodProgress progress;
  RecoveryPlan recovery;
  std::atomic<bool> resumeGate{false};
  int64_t resumeTarget = 0, resumeDeadline = 0;
  bool resumeIssued = false;
  static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
  int64_t lastDuration = 0;
  explicit Source(obs_source_t *s)
      : parent(s), temp(QDir::tempPath() + "/obs-streaming-media-XXXXXX") {}
  ~Source() {
    accept = false;
    closing = true;
    epoch++;
    cv.notify_all();
    if (child) {
      signal_handler_disconnect(obs_source_get_signal_handler(child),
                                "media_ended", childEnded, this);
      signal_handler_disconnect(obs_source_get_signal_handler(child), "update",
                                childUpdated, this);
      obs_source_remove_audio_capture_callback(child, audio, this);
    }
    if (audioHandoff)
      audioHandoff->stop();
    if (worker.joinable())
      worker.join();
    if (child) {
      obs_source_remove_active_child(parent, child);
      obs_source_release(child);
    }
  }
  static void childUpdated(void *data, calldata_t *) {
    static_cast<Source *>(data)->decoderUpdated = true;
  }
  static void childEnded(void *data, calldata_t *) {
    auto s = static_cast<Source *>(data);
    if (s->accept)
      s->decoderEnded = true;
  }
  static void audio(void *data, obs_source_t *, const struct audio_data *a,
                    bool muted) {
    auto s = static_cast<Source *>(data);
    if (!s->accept || s->resumeGate || muted || !a->frames)
      return;
    obs_audio_info info{};
    if (!obs_get_audio_info(&info))
      return;
    auto gen = s->epoch.load();
    if (s->accept && s->audioHandoff)
      s->audioHandoff->push(a->data, a->frames, a->timestamp,
                            info.samples_per_sec, info.speakers, gen);
  }
  void forwardAudio(const AudioPacket &p) {
    if (closing || !accept || resumeGate || p.generation != epoch)
      return;
    obs_source_audio out{};
    out.frames = p.frames;
    out.timestamp = p.timestamp;
    out.samples_per_sec = p.rate;
    out.speakers = p.speakers;
    out.format = AUDIO_FORMAT_FLOAT_PLANAR;
    auto channels = get_audio_channels(p.speakers);
    for (size_t i = 0; i < channels; ++i)
      out.data[i] = reinterpret_cast<const uint8_t *>(p.planes[i].data());
    obs_source_output_audio(parent, &out);
  }
  void clearChild() {
    if (!child)
      return;
    obs_source_media_stop(child);
    // Clear the remote input as well: native FFmpeg otherwise reconnects after
    // EOF.
    auto blank = obs_data_create();
    obs_data_set_bool(blank, "is_local_file", false);
    obs_data_set_string(blank, "input", "");
    obs_source_update(child, blank);
    obs_data_release(blank);
  }
  void cancel(bool preserveRecovery = false) {
    accept = false;
    resumeGate = false;
    resumeTarget = 0;
    resumeDeadline = 0;
    resumeIssued = false;
    if (!preserveRecovery)
      recovery.reset();
    auto gen = ++epoch;
    if (audioHandoff)
      audioHandoff->reset(gen);
    decoderEnded = false;
    decoderUpdated = false;
    waitingDecoder = false;
    videoWidth = 0;
    videoHeight = 0;
    loading = false;
    failed = false;
    finished = false;
    finalDuration = 0;
    progress.reset();
    lastDuration = 0;
    played = false;
    lastChildState = OBS_MEDIA_STATE_NONE;
    {
      std::lock_guard<std::mutex> l(mutex);
      pending.reset();
      result.reset();
      title.clear();
      error.clear();
    }
    clearChild();
  }
  void queue(Request req, bool retry = false) {
    cancel(retry);
    resumeTarget = req.resumeMs;
    resumeGate = resumeTarget > 0;
    if (req.url.isEmpty())
      return;
    if (!validYoutube(req.url)) {
      fail("Use an HTTPS YouTube URL");
      blog(LOG_WARNING, "[streaming-media] Use an HTTPS YouTube URL");
      return;
    }
    req.id = ++epoch;
    if (audioHandoff)
      audioHandoff->reset(req.id);
    loading = true;
    {
      std::lock_guard<std::mutex> l(mutex);
      pending = req;
    }
    cv.notify_one();
  }
  Media resolve(const Request &r) {
    Media m;
    auto engine = runtime();
    if (!engine.ready()) {
      m.error = "Media engine missing; install the bundled runtime package";
      return m;
    }
    QStringList args = {"--ignore-config",
                        "--no-playlist",
                        "--no-warnings",
                        "--no-progress",
                        "--socket-timeout",
                        "10",
                        "--retries",
                        "1",
                        "-J"};
    args << "--no-js-runtimes" << "--js-runtimes"
         << (engine.engine + ":" + engine.js);
    args << "--" << r.url;
    QProcess p;
    p.setProgram(engine.extractor);
    blog(LOG_INFO, "[streaming-media] Media engine: %s (%s)",
         engine.bundled ? "bundled" : "system", qPrintable(engine.engine));
    p.setArguments(args);
    p.start();
    if (!p.waitForStarted(1500)) {
      m.error = "Cannot launch yt-dlp";
      return m;
    }
    QByteArray output;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (p.state() != QProcess::NotRunning) {
      p.waitForFinished(100);
      output += p.readAllStandardOutput();
      p.readAllStandardError();
      if (closing || epoch != r.id ||
          std::chrono::steady_clock::now() > deadline ||
          output.size() > 16 * 1024 * 1024) {
        p.kill();
        p.waitForFinished(1000);
        m.error = "Resolution cancelled or timed out";
        return m;
      }
    }
    output += p.readAllStandardOutput();
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
      m.error = "yt-dlp could not resolve this video; check extractor/runtime "
                "availability";
      return m;
    }
    QHash<QString, QByteArray> snapshots;
    bool live =
        QJsonDocument::fromJson(output).object().value("is_live").toBool();
    auto selected =
        selectMedia(output, r.height, [&](const QString &url, bool hls) {
          if (closing || epoch != r.id ||
              std::chrono::steady_clock::now() > deadline)
            return false;
          auto body = probeMediaHttps(url, hls, deadline,
                                      [&] { return closing || epoch != r.id; });
          if (!hls)
            return !body.isEmpty();
          if (live)
            return body.startsWith("#EXTM3U");
          if (!validVodPlaylist(body))
            return false;
          snapshots.insert(url, body);
          return true;
        });
    if (!selected.live) {
      selected.videoPlaylist = snapshots.value(selected.videoUrl);
      selected.audioPlaylist = snapshots.value(selected.audioUrl);
    }
    return selected;
  }
  void run() {
    while (!closing) {
      Request req;
      {
        std::unique_lock<std::mutex> l(mutex);
        cv.wait(l, [&] { return closing || pending.has_value(); });
        if (closing)
          return;
        req = *pending;
        pending.reset();
      }
      auto media = resolve(req);
      std::lock_guard<std::mutex> l(mutex);
      if (!closing && epoch == req.id)
        result = Result{req, std::move(media)};
    }
  }
  void fail(const QString &message) {
    {
      std::lock_guard<std::mutex> l(mutex);
      error = message;
    }
    resumeGate = false;
    loading = false;
    failed = true;
  }
  bool scheduleRecovery(int64_t position, bool live) {
    bool enabled;
    {
      std::lock_guard<std::mutex> l(mutex);
      enabled = current.recover;
    }
    if (!recovery.schedule(nowMs(), position, live, enabled))
      return false;
    auto attempt = recovery.attempts();
    cancel(true);
    loading = true;
    {
      std::lock_guard<std::mutex> l(mutex);
      error = QString("Reconnecting · attempt %1 of 2").arg(attempt);
    }
    blog(LOG_INFO, "[streaming-media] Scheduling recovery attempt %d of 2",
         attempt);
    return true;
  }
  void enqueue(QueueCommand cmd) {
    std::lock_guard<std::mutex> l(mutex);
    if (commands.size() < 64)
      commands.push_back(std::move(cmd));
  }
  static void commandProc(void *data, calldata_t *call) {
    auto s = static_cast<Source *>(data);
    QueueCommand cmd;
    cmd.op = QString::fromUtf8(calldata_string(call, "op"));
    cmd.url = QString::fromUtf8(calldata_string(call, "url"));
    cmd.row = int(calldata_int(call, "row"));
    cmd.target = int(calldata_int(call, "target"));
    s->enqueue(std::move(cmd));
  }
  static void submitProc(void *data, calldata_t *call) {
    auto s = static_cast<Source *>(data);
    auto url = QString::fromUtf8(calldata_string(call, "url")).trimmed();
    auto settings = obs_source_get_settings(s->parent);
    bool enabled = obs_data_get_bool(settings, "remote_requests");
    obs_data_release(settings);
    QString status;
    if (!enabled)
      status = "disabled";
    else if (url.size() > 2048 || videoId(url).isEmpty())
      status = "invalid_url";
    else {
      std::lock_guard<std::mutex> l(s->mutex);
      if (s->closing || s->commands.size() >= 64)
        status = "busy";
      else {
        auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        while (s->requestOrder.size() >= 256) {
          auto it =
              std::find_if(s->requestOrder.begin(), s->requestOrder.end(),
                           [&](const QString &k) {
                             return s->requestResults.value(k) != "pending";
                           });
          if (it == s->requestOrder.end())
            break;
          s->requestResults.remove(*it);
          s->requestOrder.erase(it);
        }
        s->requestOrder.append(id);
        s->requestResults.insert(id, "pending");
        s->commands.push_back({"remote_append", url, -1, -1, id});
        calldata_set_string(call, "requestId", qPrintable(id));
        status = "pending";
      }
    }
    calldata_set_string(call, "status", qPrintable(status));
  }
  static void requestStatusProc(void *data, calldata_t *call) {
    auto s = static_cast<Source *>(data);
    auto id = QString::fromUtf8(calldata_string(call, "requestId"));
    std::lock_guard<std::mutex> l(s->mutex);
    auto status = s->requestResults.value(id, "unknown_request");
    calldata_set_string(call, "status", qPrintable(status));
  }
  static void statusProc(void *data, calldata_t *call) {
    auto s = static_cast<Source *>(data);
    std::lock_guard<std::mutex> l(s->mutex);
    calldata_set_string(call, "title", qPrintable(s->title));
    calldata_set_string(call, "error", qPrintable(s->error));
    calldata_set_int(call, "width", s->videoWidth.load());
    calldata_set_int(call, "height", s->videoHeight.load());
  }
  void processQueue() {
    std::deque<QueueCommand> local;
    {
      std::lock_guard<std::mutex> l(mutex);
      local.swap(commands);
    }
    bool ended = false;
    if (accept) {
      videoWidth = obs_source_get_base_width(child);
      videoHeight = obs_source_get_base_height(child);
      auto st = obs_source_media_get_state(child);
      auto pos = obs_source_media_get_time(child),
           dur = obs_source_media_get_duration(child);
      bool endSignal = decoderEnded.exchange(false);
      if (resumeGate && !endSignal && st == OBS_MEDIA_STATE_PLAYING &&
          pos > 0 && dur > 0 && videoWidth > 0) {
        if (!resumeIssued) {
          resumeTarget = std::clamp<int64_t>(resumeTarget, 0,
                                             std::max<int64_t>(0, dur - 1000));
          progress.seek(resumeTarget);
          obs_source_media_set_time(child, resumeTarget);
          resumeIssued = true;
          resumeDeadline = nowMs() + 18000;
        } else if (std::abs(pos - resumeTarget) <= 2000) {
          resumeGate = false;
          resumeTarget = 0;
          resumeIssued = false;
          blog(LOG_INFO, "[streaming-media] Recovered VOD playback position");
        }
      }
      bool resumeTimeout =
          resumeGate && resumeDeadline > 0 && nowMs() >= resumeDeadline;
      bool unexpectedStop = played && st == OBS_MEDIA_STATE_STOPPED;

      if (dur > 0)
        lastDuration = dur;
      if (pos > 0 && !endSignal && st != OBS_MEDIA_STATE_ENDED &&
          st != OBS_MEDIA_STATE_ERROR)
        progress.observe(pos);
      if (st == OBS_MEDIA_STATE_PLAYING && !resumeGate && !played && pos > 0 &&
          videoWidth.load() > 0) {
        played = true;
        obs_source_media_started(parent);
      }
      if (resumeTimeout || unexpectedStop || endSignal ||
          ((st == OBS_MEDIA_STATE_ENDED || st == OBS_MEDIA_STATE_ERROR) &&
           lastChildState != st)) {
        ended = (endSignal || st == OBS_MEDIA_STATE_ENDED) && played &&
                reachedVodEnd(progress.position(), lastDuration, sessionLive);
        blog(LOG_DEBUG,
             "[streaming-media] Decoder ended: position=%lld duration=%lld "
             "natural=%d",
             (long long)progress.position(), (long long)lastDuration, ended);
        accept = false;
        if (audioHandoff)
          audioHandoff->reset(epoch.load());
        clearChild();
        if (ended) {
          finalDuration = lastDuration;
          finished = true;
          obs_source_media_ended(parent);
        } else {
          auto position = resumeGate ? resumeTarget : progress.position();
          if (!(played || recovery.attempts() > 0) ||
              !scheduleRecovery(position, sessionLive)) {
            resumeGate = false;
            fail("Playback interrupted; use Retry to resolve fresh media");
          }
        }
      }
      lastChildState = st;
    }
    if (local.empty() && !ended)
      return;
    auto settings = obs_source_get_settings(parent);
    auto q = MediaQueue::fromJson(
        QString::fromUtf8(obs_data_get_string(settings, "queue")));
    auto original = q.serialize();
    QString url = QString::fromUtf8(obs_data_get_string(settings, "url"));
    bool autoAdvance = obs_data_get_bool(settings, "auto_advance");
    bool remoteEnabled = obs_data_get_bool(settings, "remote_requests");
    obs_data_release(settings);
    auto initialUrl = url;
    bool initialAuto = autoAdvance, load = false;
    QList<QPair<QString, QString>> receipts;
    // Commands are serialized on the OBS video thread; stale indexed actions
    // cannot silently target another entry after a queue change.
    for (const auto &c : local) {
      auto matches = [&] {
        return c.row >= 0 && c.row < q.items().size() &&
               q.items()[c.row] == c.url;
      };
      if (c.op == "remote_append")
        receipts.append(
            {c.requestId, q.appendRequest(c.url, url, remoteEnabled)});
      else if (c.op == "append")
        q.append(c.url);
      else if (c.op == "remove" && matches())
        q.take(c.row);
      else if (c.op == "move" && matches())
        q.move(c.row, c.target);
      else if (c.op == "clear")
        q = MediaQueue{};
      else if (c.op == "autoplay")
        autoAdvance = c.target != 0;
      else if (c.op == "load" && !videoId(c.url).isEmpty()) {
        url = c.url;
        load = true;
      } else if (c.op == "play" && matches()) {
        url = q.take(c.row);
        load = true;
      } else if (c.op == "next" && !q.items().isEmpty()) {
        url = q.take(0);
        load = true;
      }
    }
    if (ended && autoAdvance && !load && !q.items().isEmpty()) {
      url = q.take(0);
      load = true;
    }
    if (original != q.serialize() || initialAuto != autoAdvance || load) {
      auto patch = obs_data_create();
      obs_data_set_string(patch, "queue", qPrintable(q.serialize()));
      obs_data_set_bool(patch, "auto_advance", autoAdvance);
      if (load) {
        // A manual selection wins over a scheduled retry.
        if (recovery.waiting())
          cancel();
        obs_data_set_string(patch, "url", qPrintable(url));
      }
      obs_source_update(parent, patch);
      obs_data_release(patch);
      if (load && url == initialUrl)
        reload();
    }
    if (!receipts.isEmpty()) {
      std::lock_guard<std::mutex> l(mutex);
      for (const auto &receipt : receipts)
        requestResults.insert(receipt.first, receipt.second);
    }
  }
  void tick() {
    // OBS applies video-source updates on a later video tick. Do not admit
    // old decoder frames/audio or signals while that update is pending.
    if (waitingDecoder && decoderUpdated.exchange(false)) {
      waitingDecoder = false;
      decoderEnded = false;
      accept = true;
      if (resumeGate)
        resumeDeadline = nowMs() + 25000;
      loading = false;
    }
    processQueue();
    if (auto attempt = recovery.take(nowMs())) {
      Request req;
      {
        std::lock_guard<std::mutex> l(mutex);
        req = current;
      }
      if (req.recover) {
        req.resumeMs = attempt->position;
        queue(req, true);
        std::lock_guard<std::mutex> l(mutex);
        error = QString("Reconnecting · attempt %1 of 2").arg(attempt->attempt);
      } else
        cancel();
    }
    std::optional<Result> r;
    {
      std::lock_guard<std::mutex> l(mutex);
      if (result) {
        r = std::move(result);
        result.reset();
      }
    }
    if (!r || epoch != r->req.id)
      return;
    if (!r->media.error.isEmpty() || !child) {
      if (!recovery.attempts() || !scheduleRecovery(r->req.resumeMs, false)) {
        resumeGate = false;
        fail(r->media.error);
      }
      blog(LOG_WARNING, "[streaming-media] %s", qPrintable(r->media.error));
      return;
    }
    QString input = r->media.direct;
    if (!r->media.manifest.isEmpty()) {
      input = temp.filePath(QString::number(r->req.id) + ".m3u8");
      QFile f(input);
      if (!temp.isValid() ||
          !f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        fail("Cannot create private manifest");
        blog(LOG_WARNING, "[streaming-media] Cannot create private manifest");
        return;
      }
      f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
      auto manifest = r->media.manifest;
      if (!r->media.videoPlaylist.isEmpty() &&
          !r->media.audioPlaylist.isEmpty()) {
        auto writePlaylist = [&](const QString &suffix,
                                 const QByteArray &bytes) {
          auto path =
              temp.filePath(QString::number(r->req.id) + suffix + ".m3u8");
          QFile playlist(path);
          if (!playlist.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return QString{};
          playlist.setPermissions(QFileDevice::ReadOwner |
                                  QFileDevice::WriteOwner);
          if (playlist.write(bytes) != bytes.size())
            return QString{};
          return path;
        };
        auto v = writePlaylist("-video", r->media.videoPlaylist),
             a = writePlaylist("-audio", r->media.audioPlaylist);
        if (v.isEmpty() || a.isEmpty()) {
          fail("Cannot write rendition playlists");
          return;
        }
        manifest.replace(r->media.videoUrl, QFileInfo(v).fileName());
        manifest.replace(r->media.audioUrl, QFileInfo(a).fileName());
      }
      auto bytes = manifest.toUtf8();
      if (f.write(bytes) != bytes.size()) {
        fail("Cannot write media manifest");
        return;
      }
      f.close();
    }
    auto settings = obs_data_create();
    obs_data_set_bool(settings, "is_local_file", false);
    obs_data_set_string(settings, "input", qPrintable(input));
    obs_data_set_string(settings, "input_format",
                        r->media.manifest.isEmpty() ? "" : "hls");
    obs_data_set_string(settings, "ffmpeg_options",
                        r->media.manifest.isEmpty()
                            ? ""
                            : "protocol_whitelist=file,http,https,tcp,tls,"
                              "crypto allowed_extensions=ALL");
    obs_data_set_bool(settings, "seekable", !r->media.live);
    obs_data_set_bool(settings, "restart_on_activate", false);
    obs_data_set_bool(settings, "close_when_inactive", false);
    obs_data_set_bool(settings, "clear_on_media_end", true);
    obs_data_set_bool(settings, "hw_decode", r->req.hw);
    obs_data_set_bool(settings, "log_changes", false);
    obs_data_set_int(settings, "buffering_mb", r->req.buffer);
    obs_data_set_int(settings, "speed_percent", r->req.speed);
    decoderUpdated = false;
    waitingDecoder = true;
    obs_source_update(child, settings);
    obs_data_release(settings);
    {
      std::lock_guard<std::mutex> l(mutex);
      title = r->media.title;
      error.clear();
    }
    sessionLive = r->media.live;
    if (sessionLive) {
      resumeGate = false;
      resumeTarget = 0;
    }
    decoderEnded = false;
    failed = false;
    blog(LOG_INFO, "[streaming-media] YouTube media resolved (%s, %s)",
         r->media.live ? "live" : "VOD",
         r->media.manifest.isEmpty() ? "combined" : "adaptive HLS");
  }
  void update(obs_data_t *d) {
    Request r;
    r.url = QString::fromUtf8(obs_data_get_string(d, "url")).trimmed();
    r.height = int(obs_data_get_int(d, "height"));
    if (r.height < 144 || r.height > 2160)
      r.height = 1080;
    r.hw = obs_data_get_bool(d, "hw");
    r.speed = int(obs_data_get_int(d, "speed"));
    r.buffer = int(obs_data_get_int(d, "buffer"));
    r.restart = obs_data_get_bool(d, "restart");
    r.recover = obs_data_get_bool(d, "auto_recover");
    bool changed;
    {
      std::lock_guard<std::mutex> l(mutex);
      changed = r.url != current.url || r.height != current.height ||
                r.hw != current.hw || r.speed != current.speed ||
                r.buffer != current.buffer;
      current = r;
    }
    if (changed)
      queue(r);
    else if (!r.recover && recovery.waiting())
      cancel();
  }
  void reload() {
    Request r;
    {
      std::lock_guard<std::mutex> l(mutex);
      r = current;
    }
    queue(r);
  }
};
const char *name(void *) { return "Streaming Media (YouTube)"; }
void defaults(obs_data_t *s) {
  obs_data_set_default_int(s, "height", 1080);
  obs_data_set_default_bool(s, "hw", true);
  obs_data_set_default_int(s, "speed", 100);
  obs_data_set_default_int(s, "buffer", 8);
  obs_data_set_default_bool(s, "restart", false);
  obs_data_set_default_bool(s, "auto_recover", true);
  obs_data_set_default_string(s, "queue", "[]");
  obs_data_set_default_bool(s, "auto_advance", false);
  obs_data_set_default_bool(s, "remote_requests", false);
}
obs_properties_t *properties(void *) {
  auto p = obs_properties_create();
  obs_properties_set_flags(p, OBS_PROPERTIES_DEFER_UPDATE);
  obs_properties_add_text(p, "url", "YouTube URL", OBS_TEXT_DEFAULT);
  auto q = obs_properties_add_list(p, "height", "Maximum resolution",
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
  for (auto h : {360, 480, 720, 1080, 1440, 2160}) {
    auto title = QString::number(h) + "p";
    obs_property_list_add_int(q, qPrintable(title), h);
  }
  obs_properties_add_bool(p, "hw", "Hardware decoding");
  auto speed =
      obs_properties_add_int_slider(p, "speed", "Playback speed", 50, 200, 5);
  obs_property_int_set_suffix(speed, "%");
  auto buffer =
      obs_properties_add_int_slider(p, "buffer", "Network buffer", 0, 16, 1);
  obs_property_int_set_suffix(buffer, " MB");
  obs_properties_add_bool(p, "restart", "Restart on scene activation");
  obs_properties_add_bool(p, "auto_recover", "Recover interrupted playback");
  obs_properties_add_bool(p, "remote_requests",
                          "Accept WebSocket video requests");
  return p;
}
void *create(obs_data_t *settings, obs_source_t *parent) {
  auto s = new Source(parent);
  auto d = obs_data_create();
  obs_data_set_bool(d, "is_local_file", false);
  obs_data_set_bool(d, "restart_on_activate", false);
  obs_data_set_bool(d, "log_changes", false);
  s->child =
      obs_source_create_private("ffmpeg_source", "Streaming Media playback", d);
  obs_data_release(d);
  if (!s->child) {
    delete s;
    return nullptr;
  }
  if (!obs_source_add_active_child(parent, s->child)) {
    obs_source_release(s->child);
    s->child = nullptr;
    delete s;
    return nullptr;
  }
  s->audioHandoff = std::make_unique<AudioHandoff>(
      [s](const AudioPacket &p) { s->forwardAudio(p); });
  obs_source_set_audio_mixers(s->child, 0);
  obs_source_add_audio_capture_callback(s->child, Source::audio, s);
  signal_handler_connect(obs_source_get_signal_handler(s->child), "media_ended",
                         Source::childEnded, s);
  signal_handler_connect(obs_source_get_signal_handler(s->child), "update",
                         Source::childUpdated, s);
  auto proc = obs_source_get_proc_handler(parent);
  proc_handler_add(proc,
                   "void streaming_media_command(string op, string url, int "
                   "row, int target)",
                   Source::commandProc, s);
  proc_handler_add(proc,
                   "void streaming_media_status(out string title, out string "
                   "error, out int width, out int height)",
                   Source::statusProc, s);
  proc_handler_add(proc,
                   "void streaming_media_submit(string url, out string "
                   "requestId, out string status)",
                   Source::submitProc, s);
  proc_handler_add(proc,
                   "void streaming_media_request_status(string requestId, out "
                   "string status)",
                   Source::requestStatusProc, s);
  s->worker = std::thread([s] { s->run(); });
  s->update(settings);
  return s;
}
Source *self(void *p) { return static_cast<Source *>(p); }
void destroy(void *p) { delete self(p); }
void update(void *p, obs_data_t *s) { self(p)->update(s); }
void tick(void *p, float) { self(p)->tick(); }
void render(void *p, gs_effect_t *) {
  auto s = self(p);
  if (s->accept && !s->resumeGate)
    obs_source_video_render(s->child);
}
uint32_t width(void *p) {
  auto s = self(p);
  return s->accept ? s->videoWidth.load() : 0;
}
uint32_t height(void *p) {
  auto s = self(p);
  return s->accept ? s->videoHeight.load() : 0;
}
enum gs_color_space color(void *p, size_t n,
                          const enum gs_color_space *preferred) {
  return obs_source_get_color_space(self(p)->child, n, preferred);
}
void children(void *p, obs_source_enum_proc_t cb, void *param) {
  auto s = self(p);
  cb(s->parent, s->child, param);
}
enum obs_media_state state(void *p) {
  auto s = self(p);
  if (s->loading || s->resumeGate)
    return OBS_MEDIA_STATE_OPENING;
  if (s->failed)
    return OBS_MEDIA_STATE_ERROR;
  if (s->finished)
    return OBS_MEDIA_STATE_ENDED;
  if (!s->accept)
    return OBS_MEDIA_STATE_STOPPED;
  return obs_source_media_get_state(s->child);
}
int64_t duration(void *p) {
  auto s = self(p);
  return s->accept ? obs_source_media_get_duration(s->child)
                   : s->finalDuration.load();
}
int64_t time(void *p) {
  auto s = self(p);
  return s->accept ? obs_source_media_get_time(s->child)
                   : s->finalDuration.load();
}
void seek(void *p, int64_t ms) {
  auto s = self(p);
  if (s->accept && !s->resumeGate) {
    s->progress.seek(ms);
    obs_source_media_set_time(s->child, ms);
  }
}
void restart(void *p) { self(p)->reload(); }
void stop(void *p) { self(p)->cancel(); }
void next(void *p) { self(p)->enqueue({"next", {}, -1, -1}); }
void pause(void *p, bool paused) {
  auto s = self(p);
  if (s->accept && !s->resumeGate)
    obs_source_media_play_pause(s->child, paused);
  else if (!paused && !s->loading && !s->resumeGate)
    s->reload();
}
void activate(void *p) {
  auto s = self(p);
  bool r;
  {
    std::lock_guard<std::mutex> l(s->mutex);
    r = s->current.restart;
  }
  if (r && !s->loading)
    s->reload();
}
} // namespace
bool obs_module_load(void) {
  obs_source_info i{};
  i.id = "streaming_media_youtube";
  i.type = OBS_SOURCE_TYPE_INPUT;
  i.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO |
                   OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CONTROLLABLE_MEDIA |
                   OBS_SOURCE_SRGB;
  i.get_name = name;
  i.create = create;
  i.destroy = destroy;
  i.update = update;
  i.get_defaults = defaults;
  i.get_properties = properties;
  i.video_tick = tick;
  i.video_render = render;
  i.get_width = width;
  i.get_height = height;
  i.video_get_color_space = color;
  i.enum_active_sources = children;
  i.activate = activate;
  i.media_get_state = state;
  i.media_get_duration = duration;
  i.media_get_time = time;
  i.media_set_time = seek;
  i.media_restart = restart;
  i.media_stop = stop;
  i.media_next = next;
  i.media_play_pause = pause;
  i.icon_type = OBS_ICON_TYPE_MEDIA;
  obs_register_source(&i);
  registerMediaDock();
  blog(LOG_INFO, "[streaming-media] Loaded 0.5.0");
  return true;
}

void obs_module_post_load(void) { registerMediaWebsocket(); }
void obs_module_unload(void) { unregisterMediaDock(); }
