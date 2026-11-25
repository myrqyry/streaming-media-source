#include "media-websocket.hpp"
#include "media-queue.hpp"
#include <QString>
#include <memory>
#include <obs-module.h>
#include <obs-websocket-api.h>
namespace {
constexpr const char *vendorName = "streaming-media";
using SourceRef = std::unique_ptr<obs_source_t, decltype(&obs_source_release)>;
bool isMedia(obs_source_t *source) {
  return source && !obs_source_removed(source) &&
         (QString::fromUtf8(obs_source_get_unversioned_id(source)) ==
              "streaming_media_youtube" ||
          QString::fromUtf8(obs_source_get_unversioned_id(source)) ==
              "streaming_media_audio");
}
SourceRef target(obs_data_t *request) {
  auto uuid = obs_data_get_string(request, "sourceUuid");
  SourceRef s(*uuid ? obs_get_source_by_uuid(uuid) : nullptr,
              obs_source_release);
  if (!isMedia(s.get()))
    s.reset();
  return s;
}
void status(obs_data_t *response, const char *value) {
  obs_data_set_string(response, "status", value);
}
void listSources(obs_data_t *, obs_data_t *response, void *) {
  auto list = obs_data_array_create();
  obs_enum_sources(
      [](void *p, obs_source_t *s) {
        if (!isMedia(s))
          return true;
        auto item = obs_data_create();
        obs_data_set_string(item, "sourceUuid", obs_source_get_uuid(s));
        obs_data_set_string(item, "sourceName", obs_source_get_name(s));
        auto settings = obs_source_get_settings(s);
        obs_data_set_bool(item, "acceptRequests",
                          obs_data_get_bool(settings, "remote_requests"));
        obs_data_release(settings);
        obs_data_array_push_back(static_cast<obs_data_array_t *>(p), item);
        obs_data_release(item);
        return true;
      },
      list);
  obs_data_set_array(response, "sources", list);
  obs_data_array_release(list);
  status(response, "ok");
}
void submit(obs_data_t *request, obs_data_t *response, void *) {
  auto s = target(request);
  if (!s) {
    status(response, "source_not_found");
    return;
  }
  calldata_t c;
  calldata_init(&c);
  calldata_set_string(&c, "url", obs_data_get_string(request, "url"));
  if (proc_handler_call(obs_source_get_proc_handler(s.get()),
                        "streaming_media_submit", &c)) {
    auto result = calldata_string(&c, "status");
    status(response, result ? result : "unavailable");
    auto id = calldata_string(&c, "requestId");
    if (id && *id)
      obs_data_set_string(response, "requestId", id);
  } else
    status(response, "unavailable");
  calldata_free(&c);
}
void requestStatus(obs_data_t *request, obs_data_t *response, void *) {
  auto s = target(request);
  if (!s) {
    status(response, "source_not_found");
    return;
  }
  calldata_t c;
  calldata_init(&c);
  calldata_set_string(&c, "requestId",
                      obs_data_get_string(request, "requestId"));
  if (proc_handler_call(obs_source_get_proc_handler(s.get()),
                        "streaming_media_request_status", &c)) {
    auto result = calldata_string(&c, "status");
    status(response, result ? result : "unavailable");
  } else
    status(response, "unavailable");
  calldata_free(&c);
}
void queueStatus(obs_data_t *request, obs_data_t *response, void *) {
  auto s = target(request);
  if (!s) {
    status(response, "source_not_found");
    return;
  }
  auto settings = obs_source_get_settings(s.get());
  auto q = MediaQueue::fromJson(
      QString::fromUtf8(obs_data_get_string(settings, "queue")));
  auto list = obs_data_array_create();
  for (const auto &url : q.items()) {
    auto item = obs_data_create();
    obs_data_set_string(item, "url", qPrintable(url));
    obs_data_array_push_back(list, item);
    obs_data_release(item);
  }
  obs_data_set_array(response, "items", list);
  obs_data_array_release(list);
  obs_data_set_bool(response, "acceptRequests",
                    obs_data_get_bool(settings, "remote_requests"));
  obs_data_set_bool(response, "autoAdvance",
                    obs_data_get_bool(settings, "auto_advance"));
  obs_data_set_string(response, "currentUrl",
                      obs_data_get_string(settings, "url"));
  obs_data_release(settings);
  status(response, "ok");
}
} // namespace
void registerMediaWebsocket() {
  auto vendor = obs_websocket_register_vendor(vendorName);
  if (!vendor) {
    blog(LOG_INFO, "[streaming-media] OBS WebSocket unavailable; local "
                   "playback unaffected");
    return;
  }
  bool ok = true;
  ok = obs_websocket_vendor_register_request(vendor, "ListSources", listSources,
                                             nullptr) &&
       ok;
  ok = obs_websocket_vendor_register_request(vendor, "SubmitVideo", submit,
                                             nullptr) &&
       ok;
  ok = obs_websocket_vendor_register_request(vendor, "GetRequestStatus",
                                             requestStatus, nullptr) &&
       ok;
  ok = obs_websocket_vendor_register_request(vendor, "GetQueue", queueStatus,
                                             nullptr) &&
       ok;
  blog(ok ? LOG_INFO : LOG_WARNING,
       "[streaming-media] WebSocket vendor registration %s",
       ok ? "ready" : "incomplete");
}
