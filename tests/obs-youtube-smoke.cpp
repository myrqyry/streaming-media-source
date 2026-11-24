#include "media-dock.hpp"
#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWidget>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <obs-module.h>
#include <obs.h>
#include <thread>
#include <utility>
#include <vector>
using namespace std::chrono_literals;
std::atomic<int> frames{0}, audio{0}, mixed{0};
std::mutex uiMutex;
std::vector<std::pair<obs_task_t, void *>> tasks;
void queueUi(obs_task_t f, void *p, bool wait) {
  if (wait) {
    f(p);
    return;
  }
  std::lock_guard<std::mutex> l(uiMutex);
  tasks.emplace_back(f, p);
}
void pump() {
  std::vector<std::pair<obs_task_t, void *>> t;
  {
    std::lock_guard<std::mutex> l(uiMutex);
    t.swap(tasks);
  }
  for (auto &p : t)
    p.first(p.second);
  QCoreApplication::processEvents();
}
void videoCb(void *, struct video_data *v) {
  if (!v->data[0])
    return;
  int different = 0;
  for (int y = 0; y < 180; y += 8)
    for (int x = 0; x < 320; x += 8)
      if (v->data[0][y * v->linesize[0] + x] > 30)
        different++;
  if (different > 30)
    frames++;
}
void audioCb(void *, obs_source_t *, const struct audio_data *a, bool muted) {
  if (muted || !a->data[0])
    return;
  auto p = (float *)a->data[0];
  for (uint32_t i = 0; i < a->frames; i++)
    if (std::abs(p[i]) > 0.001f) {
      audio++;
      break;
    }
}
void mixedCb(void *, size_t, struct audio_data *a) {
  if (!a->data[0])
    return;
  auto p = (float *)a->data[0];
  for (uint32_t i = 0; i < a->frames; i++)
    if (std::abs(p[i]) > 0.001f) {
      mixed++;
      break;
    }
}
bool load(const char *path, const char *data) {
  obs_module_t *m = nullptr;
  return obs_open_module(&m, path, data) == MODULE_SUCCESS &&
         obs_init_module(m);
}
template <class F> bool until(int secs, F f) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(secs);
  do {
    pump();
    if (f())
      return true;
    std::this_thread::sleep_for(100ms);
  } while (std::chrono::steady_clock::now() < end);
  return false;
}
static void queueCommand(obs_source_t *s, const char *op, const char *url = "",
                         int row = -1, int target = -1) {
  calldata_t c;
  calldata_init(&c);
  calldata_set_string(&c, "op", op);
  calldata_set_string(&c, "url", url);
  calldata_set_int(&c, "row", row);
  calldata_set_int(&c, "target", target);
  proc_handler_call(obs_source_get_proc_handler(s), "streaming_media_command",
                    &c);
  calldata_free(&c);
}
static QStringList queued(obs_source_t *s) {
  auto d = obs_source_get_settings(s);
  auto a = QJsonDocument::fromJson(QByteArray(obs_data_get_string(d, "queue")))
               .array();
  obs_data_release(d);
  QStringList result;
  for (auto v : a)
    result.append(v.toString());
  return result;
}
static QString currentUrl(obs_source_t *s) {
  auto d = obs_source_get_settings(s);
  auto r = QString::fromUtf8(obs_data_get_string(d, "url"));
  obs_data_release(d);
  return r;
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QTemporaryDir tmp;
  if (argc != 3)
    return 2;
  if (!obs_startup("en-US", tmp.path().toUtf8().constData(), nullptr))
    return 2;
  obs_set_ui_task_handler(queueUi);
  obs_audio_info ai{48000, SPEAKERS_STEREO};
  if (!obs_reset_audio(&ai))
    return 2;
  obs_video_info v{};
  v.graphics_module = "libobs-opengl";
  v.fps_num = 30;
  v.fps_den = 1;
  v.base_width = 1920;
  v.base_height = 1080;
  v.output_width = 320;
  v.output_height = 180;
  v.output_format = VIDEO_FORMAT_I420;
  v.colorspace = VIDEO_CS_709;
  v.range = VIDEO_RANGE_PARTIAL;
  v.scale_type = OBS_SCALE_BILINEAR;
  if (obs_reset_video(&v) != OBS_VIDEO_SUCCESS)
    return 2;
  if (!load("/usr/lib/x86_64-linux-gnu/obs-plugins/obs-ffmpeg.so",
            "/usr/share/obs/obs-plugins/obs-ffmpeg") ||
      !load(argv[1], qPrintable(tmp.path())))
    return 2;
  auto d = obs_data_create();
  obs_data_set_string(d, "url", argv[2]);
  obs_data_set_bool(d, "hw", false);
  auto s =
      obs_source_create("streaming_media_youtube", "YouTube smoke", d, nullptr);
  obs_data_release(d);
  if (!s)
    return 2;
  auto scene = obs_scene_create("Smoke scene");
  auto item = obs_scene_add(scene, s);
  obs_set_output_source(0, obs_scene_get_source(scene));
  obs_source_add_audio_capture_callback(s, audioCb, nullptr);
  obs_add_raw_video_callback(nullptr, videoCb, nullptr);
  audio_convert_info conv{48000, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_STEREO};
  obs_add_raw_audio_callback(0, &conv, mixedCb, nullptr);
  bool playing = until(45, [&] {
    return frames > 15 && audio > 15 && mixed > 15 &&
           obs_source_get_width(s) > 0 && obs_source_media_get_time(s) > 1000;
  });
  fprintf(stderr,
          "PLAY width=%u height=%u nonblack=%d source_audio=%d mixed_audio=%d "
          "state=%d time=%lld duration=%lld\n",
          obs_source_get_width(s), obs_source_get_height(s), frames.load(),
          audio.load(), mixed.load(), obs_source_media_get_state(s),
          (long long)obs_source_media_get_time(s),
          (long long)obs_source_media_get_duration(s));
  bool pause = false, seek = false, resume = false;
  if (playing) {
    obs_source_media_play_pause(s, true);
    until(3, [&] {
      return obs_source_media_get_state(s) == OBS_MEDIA_STATE_PAUSED;
    });
    auto before = obs_source_media_get_time(s);
    std::this_thread::sleep_for(700ms);
    auto after = obs_source_media_get_time(s);
    pause = obs_source_media_get_state(s) == OBS_MEDIA_STATE_PAUSED &&
            std::abs(after - before) < 300;
    obs_source_media_set_time(s, 45000);
    obs_source_media_play_pause(s, false);
    seek = until(18, [&] {
      auto t = obs_source_media_get_time(s);
      return t >= 44000 && t < 65000;
    });
    auto n = mixed.load();
    resume = until(8, [&] { return mixed > n + 10; });
    fprintf(stderr, "CONTROL pause=%d seek=%d resume_audio=%d time=%lld\n",
            pause, seek, resume, (long long)obs_source_media_get_time(s));
  }
  std::unique_ptr<QWidget> dock(createMediaDockWidget());
  dock->resize(360, 430);
  dock->show();
  auto picker = dock->findChild<QComboBox *>("media-source-picker");
  auto entry = dock->findChild<QLineEdit *>("media-url");
  auto add = dock->findChild<QPushButton *>("media-queue-add");
  auto rows = dock->findChild<QListWidget *>("media-queue-list");
  auto up = dock->findChild<QToolButton *>("media-queue-up");
  auto remove = dock->findChild<QToolButton *>("media-queue-remove");
  auto nextButton = dock->findChild<QToolButton *>("media-next");
  auto title = dock->findChild<QLabel *>("media-title");
  auto thumb = dock->findChild<QLabel *>("media-thumbnail");
  bool widgetReady = picker && entry && add && rows && up && remove &&
                     nextButton && title && thumb && until(5, [&] {
                       return picker->count() == 1 &&
                              !title->text().isEmpty() &&
                              title->text() != "No video loaded";
                     });
  bool thumbnailReady =
      widgetReady && until(8, [&] { return !thumb->pixmap().isNull(); });
  fprintf(stderr, "DOCK widgets=%d thumbnail=%d\n", widgetReady,
          thumbnailReady);
  if (widgetReady)
    dock->grab().save("build/dock-widget-preview.png");
  bool queueEdit = false, nextPlayback = false, stopped = false,
       autoNext = false;
  if (playing && pause && seek && resume && widgetReady) {
    const char *other = "https://youtu.be/aqz-KE-bpKQ";
    const char *third = "https://youtu.be/jNQXAC9IVRw";
    entry->setText(third);
    add->click();
    entry->setText(other);
    add->click();
    queueEdit =
        until(3, [&] { return queued(s) == QStringList{third, other}; });
    until(3, [&] { return rows->count() == 2; });
    rows->setCurrentRow(1);
    up->click();
    queueEdit = queueEdit && until(3, [&] {
                  return queued(s) == QStringList{other, third};
                });
    until(3, [&] {
      return rows->count() == 2 && rows->item(1)->toolTip() == third;
    });
    rows->setCurrentRow(1);
    remove->click();
    queueEdit =
        queueEdit && until(3, [&] { return queued(s) == QStringList{other}; });
    auto before = mixed.load();
    until(3, [&] { return nextButton->isEnabled(); });
    nextButton->click();
    bool changed =
        until(5, [&] { return currentUrl(s) == other && queued(s).isEmpty(); });
    nextPlayback =
        changed && until(45, [&] {
          return obs_source_media_get_state(s) == OBS_MEDIA_STATE_PLAYING &&
                 obs_source_media_get_time(s) > 1000 &&
                 obs_source_media_get_time(s) < 20000 && mixed > before + 15;
        });
    // Force a seek near the end, then let the actual decoder finish naturally.
    queueCommand(s, "append", argv[2]);
    queueCommand(s, "autoplay", "", -1, 1);
    until(3, [&] { return queued(s) == QStringList{argv[2]}; });
    auto end = obs_source_media_get_duration(s);
    if (nextPlayback && end > 10000) {
      obs_source_media_set_time(s, end - 2500);
      bool advanced = until(
          30, [&] { return currentUrl(s) == argv[2] && queued(s).isEmpty(); });
      auto n = mixed.load();
      autoNext =
          advanced && until(45, [&] {
            return obs_source_media_get_state(s) == OBS_MEDIA_STATE_PLAYING &&
                   obs_source_media_get_time(s) > 1000 &&
                   obs_source_media_get_time(s) < 20000 && mixed > n + 10;
          });
    }
    fprintf(stderr, "AUTO_NEXT %d\n", autoNext);
    queueCommand(s, "append", third);
    until(3, [&] { return queued(s) == QStringList{third}; });
    obs_source_media_stop(s);
    stopped = until(3, [&] {
      return obs_source_media_get_state(s) == OBS_MEDIA_STATE_STOPPED;
    });
    std::this_thread::sleep_for(500ms);
    stopped = stopped && queued(s) == QStringList{third};
    fprintf(stderr, "QUEUE edit=%d next_playback=%d stop_preserves_queue=%d\n",
            queueEdit, nextPlayback, stopped);
  }
  dock.reset();
  obs_source_media_stop(s);
  auto weak = obs_source_get_weak_source(s);
  auto sceneWeak = obs_source_get_weak_source(obs_scene_get_source(scene));
  // Drain the queued media stop before removing the owning scene item.
  until(5, [&] {
    return obs_source_media_get_state(s) == OBS_MEDIA_STATE_STOPPED;
  });
  obs_wait_for_destroy_queue();
  pump();
  obs_remove_raw_audio_callback(0, mixedCb, nullptr);
  obs_remove_raw_video_callback(videoCb, nullptr);
  obs_source_remove_audio_capture_callback(s, audioCb, nullptr);
  obs_set_output_source(0, nullptr);
  obs_sceneitem_remove(item);
  obs_scene_release(scene);
  obs_source_release(s);
  for (int i = 0; i < 20; i++) {
    pump();
    std::this_thread::sleep_for(20ms);
  }
  bool released = until(8, [&] {
    obs_wait_for_destroy_queue();
    return obs_weak_source_expired(weak);
  });
  fprintf(stderr,
          "TEARDOWN refs_released=%d source_expired=%d scene_expired=%d\n",
          released, obs_weak_source_expired(weak),
          obs_weak_source_expired(sceneWeak));
  obs_weak_source_release(weak);
  obs_weak_source_release(sceneWeak);
  pump();
  obs_wait_for_destroy_queue();
  pump();
  obs_shutdown();
  return released && playing && pause && seek && resume && widgetReady &&
                 queueEdit && nextPlayback && stopped && autoNext
             ? 0
             : 1;
}
