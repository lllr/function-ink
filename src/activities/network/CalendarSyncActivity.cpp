#include "CalendarSyncActivity.h"

#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#ifdef SIMULATOR
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#else
#include <SecureHttpClient.h>
#endif

#include "CalendarConfigStore.h"
#include "CalendarEventStore.h"
#include "CrossPointSettings.h"
#include "IcalParser.h"
#include "WifiCredentialStore.h"
#include "activities/ActivityManager.h"
#include "fontIds.h"

namespace {
int64_t toEpoch(int year, int month, int day, int hour, int min, int sec) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  auto isLeap = [](int y) { return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)); };

  if (year < 1970) return 0;
  int64_t totalDays = 0;
  for (int y = 1970; y < year; ++y) {
    totalDays += isLeap(y) ? 366 : 365;
  }
  for (int m = 1; m < month; ++m) {
    totalDays += (m == 2 && isLeap(year)) ? 29 : days[m - 1];
  }
  totalDays += (day - 1);
  return totalDays * 86400LL + hour * 3600LL + min * 60LL + sec;
}
}  // namespace

void CalendarSyncActivity::onEnter() {
  Activity::onEnter();
  state = INIT;
  statusMessage = "Starting calendar sync...";
  errorMessage.clear();
  syncedEventsCount = 0;
  syncStarted = false;
  requestUpdate();
}

void CalendarSyncActivity::onExit() {
  Activity::onExit();
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true);
  }
}

void CalendarSyncActivity::loop() {
  if (!syncStarted && state == INIT) {
    syncStarted = true;
    performSync();
    return;
  }

  // If in terminal states, exit on button press
  if (state == SUCCESS || state == ERROR_STATE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
        mappedInput.wasPressed(MappedInputManager::Button::Power)) {
      activityManager.goHome();
    }
  }
}

bool CalendarSyncActivity::connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  const size_t count = WIFI_STORE.getCredentialCount();
  if (count == 0) {
    errorMessage = "No Wi-Fi networks configured";
    return false;
  }

  for (size_t i = 0; i < count; ++i) {
    auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred) continue;

    LOG_INF("CAL", "Connecting to Wi-Fi SSID: %s", cred->ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(cred->ssid.c_str(), cred->password.empty() ? nullptr : cred->password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
      delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
      LOG_INF("CAL", "Connected! IP: %s", WiFi.localIP().toString().c_str());
      return true;
    }
  }

  errorMessage = "Failed to connect to Wi-Fi";
  return false;
}

bool CalendarSyncActivity::fetchAndParse(const std::string& url, int utcOffsetSeconds, int64_t minEpoch,
                                         int64_t maxEpoch, std::vector<CalendarEvent>& outEvents) {
  LOG_INF("CAL", "Fetching iCal feed: %s", url.c_str());
#ifdef SIMULATOR
  HTTPClient http;
  std::unique_ptr<WiFiClientSecure> secureClient;
  WiFiClient plainClient;
  bool isHttps = (url.rfind("https://", 0) == 0);
  if (isHttps) {
    secureClient.reset(new WiFiClientSecure);
    secureClient->setInsecure();
    http.begin(*secureClient, url.c_str());
  } else {
    http.begin(plainClient, url.c_str());
  }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    http.end();
    IcalStreamParser parser(utcOffsetSeconds, minEpoch, maxEpoch, outEvents);
    parser.feed(payload.c_str(), payload.length());
    parser.finish();
    return true;
  }
  http.end();
  LOG_ERR("CAL", "HTTP GET failed with code: %d", code);
  return false;
#else
  freeink::SecureHttpClient http;
  http.setInsecure();
  http.setFollowRedirects(5);
  if (!http.begin(url)) {
    LOG_ERR("CAL", "Failed to parse URL: %s", url.c_str());
    return false;
  }
  IcalStreamParser parser(utcOffsetSeconds, minEpoch, maxEpoch, outEvents);
  auto dataCb = [&parser](const uint8_t* data, size_t len) -> bool {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  };
  int code = http.GET(dataCb);
  parser.finish();
  http.end();
  if (code == 200) {
    return true;
  }
  LOG_ERR("CAL", "SecureHttpClient GET failed with code: %d", code);
  return false;
#endif
}

void CalendarSyncActivity::performSync() {
  if (!CALENDAR_CONFIG_STORE.hasCalendars()) {
    state = ERROR_STATE;
    errorMessage = "No calendar configured\nAdd URL in Web Settings";
    requestUpdate();
    return;
  }

  state = CONNECTING;
  statusMessage = "Connecting to Wi-Fi...";
  requestUpdateAndWait();

  if (!connectWifi()) {
    state = ERROR_STATE;
    errorMessage = "Failed to connect to Wi-Fi";
    requestUpdate();
    return;
  }

  state = SYNCING;
  statusMessage = "Updating time and calendar...";
  requestUpdateAndWait();

  // Always sync system time (and hardware RTC if present) via NTP
  halClock.syncFromNTP();

  const int utcOffsetSeconds = (SETTINGS.clockUtcOffsetQ - 48) * 15 * 60;
  int64_t currentLocalEpoch = 0;

  time_t rawNow = time(nullptr);
  if (rawNow >= 1735689600) {  // >= 2025-01-01
    currentLocalEpoch = static_cast<int64_t>(rawNow) + utcOffsetSeconds;
  } else {
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, min = 0;
    if (halClock.getDateTime(year, month, day, hour, min) && year >= 2025) {
      currentLocalEpoch = toEpoch(year, month, day, hour, min, 0) + utcOffsetSeconds;
    }
  }

  int64_t minEpoch = 0;
  int64_t maxEpoch = INT64_MAX;
  if (currentLocalEpoch > 0) {
    int64_t daysSinceEpoch = currentLocalEpoch / 86400LL;
    minEpoch = daysSinceEpoch * 86400LL;  // Start of today in local epoch
    maxEpoch = minEpoch + 4 * 86400LL;    // Next 3 days inclusive
  }

  std::vector<CalendarEvent> allEvents;
  allEvents.reserve(64);

  bool anySuccess = false;
  const auto& calendars = CALENDAR_CONFIG_STORE.getCalendars();
  for (const auto& cal : calendars) {
    if (cal.enabled && !cal.url.empty()) {
      if (fetchAndParse(cal.url, utcOffsetSeconds, minEpoch, maxEpoch, allEvents)) {
        anySuccess = true;
      }
    }
  }

  // Also check if manual /manual_calendar.ics exists on SD
  FsFile manualFile;
  if (Storage.openFileForRead("CAL", "/manual_calendar.ics", manualFile)) {
    IcalStreamParser parser(utcOffsetSeconds, minEpoch, maxEpoch, allEvents);
    char buf[512];
    while (int r = manualFile.read(buf, sizeof(buf))) {
      if (r <= 0) break;
      parser.feed(buf, r);
    }
    parser.finish();
    manualFile.close();
    anySuccess = true;
  }

  WiFi.disconnect(true);

  if (anySuccess) {
    CALENDAR_EVENT_STORE.setEvents(std::move(allEvents));
    syncedEventsCount = CALENDAR_EVENT_STORE.getCount();
    state = SUCCESS;
    statusMessage = "Calendar sync successful!";
  } else {
    state = ERROR_STATE;
    errorMessage = "Failed to download calendar feed";
  }

  requestUpdate();
}

void CalendarSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, screenH / 4, "Calendar Sync", true, EpdFontFamily::BOLD);

  if (state == CONNECTING || state == SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, screenH / 2, statusMessage.c_str());
  } else if (state == SUCCESS) {
    char countBuf[64];
    snprintf(countBuf, sizeof(countBuf), "%zu events synced", syncedEventsCount);
    renderer.drawCenteredText(UI_10_FONT_ID, screenH / 2 - 20, statusMessage.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, screenH / 2 + 20, countBuf);
    renderer.drawCenteredText(SMALL_FONT_ID, screenH * 3 / 4, "Press any button to return");
  } else if (state == ERROR_STATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, screenH / 2, errorMessage.c_str());
    renderer.drawCenteredText(SMALL_FONT_ID, screenH * 3 / 4, "Press any button to return");
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
