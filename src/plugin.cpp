#include "media-dock.hpp"
#include "media-queue.hpp"
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
  uint64_t id = 0;
};
struct QueueCommand {
  QString op, url;
  int row = -1, target = -1;
};
struct Result {
  Request req;
  Media media;
};
static QString exe(const QString &name) {
  auto p = QStandardPaths::findExecutable(name);
  if (!p.isEmpty())
    return p;
  p = QDir::homePath() + "/.local/bin/" + name;
  if (QFileInfo(p).isExecutable())
    return p;
  if (name == "node") {
    QDir d(QDir::homePath() + "/.nvm/versions/node");
    for (auto version : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                    QDir::Name | QDir::Reversed)) {
      p = d.absoluteFilePath(version + "/bin/node");
      if (QFileInfo(p).isExecutable())
        return p;
    }
  }
  return {};
}
struct Source {
  obs_source_t *parent = nullptr, *child = nullptr;
  QTemporaryDir temp;
  std::atomic<bool> closing{false}, accept{false}, loading{false},
      failed{false}, finished{false};
  std::atomic<int64_t> finalDuration{0};
  std::atomic<uint64_t> epoch{0};
  std::mutex mutex;
  std::condition_variable cv;
  Request current;
  std::optional<Request> pending;
  std::optional<Result> result;
  std::thread worker;
  QString title, error;
  std::deque<QueueCommand> commands;
  enum obs_media_state lastChildState = OBS_MEDIA_STATE_NONE;
  bool played = false, sessionLive = false;
  int64_t lastPosition = 0, lastDuration = 0;
  explicit Source(obs_source_t *s)
      : parent(s), temp(QDir::tempPath() + "/obs-streaming-media-XXXXXX") {}
  ~Source() {
    accept = false;
    closing = true;
    epoch++;
    cv.notify_all();
    if (child)
      obs_source_remove_audio_capture_callback(child, audio, this);
    if (worker.joinable())
      worker.join();
    if (child) {
      obs_source_remove_active_child(parent, child);
      obs_source_release(child);
    }
  }
  static void audio(void *data, obs_source_t *, const struct audio_data *a,
                    bool muted) {
    auto s = static_cast<Source *>(data);
    if (!s->accept || muted || !a->frames)
      return;
    obs_audio_info info{};
    if (!obs_get_audio_info(&info))
      return;
    obs_source_audio out{};
    out.frames = a->frames;
    out.timestamp = a->timestamp;
    out.samples_per_sec = info.samples_per_sec;
    out.speakers = info.speakers;
    out.format = AUDIO_FORMAT_FLOAT_PLANAR;
    for (size_t i = 0; i < MAX_AV_PLANES; i++)
      out.data[i] = a->data[i];
    obs_source_output_audio(s->parent, &out);
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
  void cancel() {
    epoch++;
    accept = false;
    loading = false;
    failed = false;
    finished = false;
    finalDuration = 0;
    lastPosition = 0;
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
  void queue(Request req) {
    cancel();
    if (req.url.isEmpty())
      return;
    if (!validYoutube(req.url)) {
      fail("Use an HTTPS YouTube URL");
      blog(LOG_WARNING, "[streaming-media] Use an HTTPS YouTube URL");
      return;
    }
    req.id = ++epoch;
    loading = true;
    {
      std::lock_guard<std::mutex> l(mutex);
      pending = req;
    }
    cv.notify_one();
  }
  Media resolve(const Request &r) {
    Media m;
    auto path = exe("yt-dlp");
    if (path.isEmpty()) {
      m.error = "yt-dlp not found";
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
    auto node = exe("node");
    if (!node.isEmpty())
      args << "--js-runtimes" << ("node:" + node);
    args << "--" << r.url;
    QProcess p;
    p.setProgram(path);
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
    auto curl = exe("curl");
    if (curl.isEmpty()) {
      m.error = "curl not found (required for rendition validation)";
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
          QProcess check;
          QStringList args = {"--disable",
                              "--silent",
                              "--show-error",
                              "--fail",
                              "--location",
                              "--proto",
                              "=https",
                              "--proto-redir",
                              "=https",
                              "--max-time",
                              "5",
                              "--max-filesize",
                              "1048576",
                              "--config",
                              "-"};
          if (!hls)
            args << "--range" << "0-1023";
          check.start(curl, args);
          if (!check.waitForStarted(1000))
            return false;
          // Input URLs are not exposed in command-line arguments or plugin log
          // messages.
          check.write(("url = \"" + url + "\"\n").toUtf8());
          check.closeWriteChannel();
          while (check.state() != QProcess::NotRunning) {
            check.waitForFinished(100);
            if (closing || epoch != r.id ||
                std::chrono::steady_clock::now() > deadline) {
              check.kill();
              check.waitForFinished(1000);
              return false;
            }
          }
          auto body = check.readAllStandardOutput();
          if (check.exitStatus() != QProcess::NormalExit ||
              check.exitCode() != 0)
            return false;
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
    loading = false;
    failed = true;
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
  static void statusProc(void *data, calldata_t *call) {
    auto s = static_cast<Source *>(data);
    std::lock_guard<std::mutex> l(s->mutex);
    calldata_set_string(call, "title", qPrintable(s->title));
    calldata_set_string(call, "error", qPrintable(s->error));
  }
  void processQueue() {
    std::deque<QueueCommand> local;
    {
      std::lock_guard<std::mutex> l(mutex);
      local.swap(commands);
    }
    bool ended = false;
    if (accept) {
      auto st = obs_source_media_get_state(child);
      auto pos = obs_source_media_get_time(child),
           dur = obs_source_media_get_duration(child);
      if (dur > 0)
        lastDuration = dur;
      if (pos > 0)
        lastPosition = pos;
      if (st == OBS_MEDIA_STATE_PLAYING && !played && pos > 0 &&
          obs_source_get_width(child) > 0) {
        played = true;
        obs_source_media_started(parent);
      }
      if ((st == OBS_MEDIA_STATE_ENDED || st == OBS_MEDIA_STATE_ERROR) &&
          lastChildState != st) {
        ended = st == OBS_MEDIA_STATE_ENDED && played &&
                reachedVodEnd(lastPosition, lastDuration, sessionLive);
        accept = false;
        clearChild();
        if (ended) {
          finalDuration = lastDuration;
          finished = true;
          obs_source_media_ended(parent);
        } else {
          fail("Playback interrupted; use Retry to resolve fresh media");
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
    obs_data_release(settings);
    auto initialUrl = url;
    bool initialAuto = autoAdvance, load = false;
    // Commands are serialized on the OBS video thread; stale indexed actions
    // cannot silently target another entry after a queue change.
    for (const auto &c : local) {
      auto matches = [&] {
        return c.row >= 0 && c.row < q.items().size() &&
               q.items()[c.row] == c.url;
      };
      if (c.op == "append")
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
      if (load)
        obs_data_set_string(patch, "url", qPrintable(url));
      obs_source_update(parent, patch);
      obs_data_release(patch);
      if (load && url == initialUrl)
        reload();
    }
  }
  void tick() {
    processQueue();
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
      fail(r->media.error);
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
    obs_source_update(child, settings);
    obs_data_release(settings);
    {
      std::lock_guard<std::mutex> l(mutex);
      title = r->media.title;
      error.clear();
    }
    sessionLive = r->media.live;
    accept = true;
    loading = false;
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
  obs_data_set_default_string(s, "queue", "[]");
  obs_data_set_default_bool(s, "auto_advance", false);
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
  obs_source_set_audio_mixers(s->child, 0);
  obs_source_add_audio_capture_callback(s->child, Source::audio, s);
  auto proc = obs_source_get_proc_handler(parent);
  proc_handler_add(proc,
                   "void streaming_media_command(string op, string url, int "
                   "row, int target)",
                   Source::commandProc, s);
  proc_handler_add(
      proc, "void streaming_media_status(out string title, out string error)",
      Source::statusProc, s);
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
  if (s->accept)
    obs_source_video_render(s->child);
}
uint32_t width(void *p) {
  auto s = self(p);
  return s->accept ? obs_source_get_width(s->child) : 0;
}
uint32_t height(void *p) {
  auto s = self(p);
  return s->accept ? obs_source_get_height(s->child) : 0;
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
  if (s->loading)
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
  if (s->accept)
    obs_source_media_set_time(s->child, ms);
}
void restart(void *p) { self(p)->reload(); }
void stop(void *p) { self(p)->cancel(); }
void next(void *p) { self(p)->enqueue({"next", {}, -1, -1}); }
void pause(void *p, bool paused) {
  auto s = self(p);
  if (s->accept)
    obs_source_media_play_pause(s->child, paused);
  else if (!paused && !s->loading)
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
  blog(LOG_INFO, "[streaming-media] Loaded 0.2.0");
  return true;
}

void obs_module_unload(void) { unregisterMediaDock(); }
