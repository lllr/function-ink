#pragma once

#include <string>
#include <vector>

#include "CalendarConfigStore.h"
#include "CalendarEventStore.h"
#include "activities/Activity.h"

class CalendarSyncActivity final : public Activity {
 public:
  static constexpr const char* NAME = "CalendarSync";

  explicit CalendarSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity(NAME, renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == SYNCING; }

 private:
  enum State {
    INIT,
    CONNECTING,
    SYNCING,
    SUCCESS,
    ERROR_STATE
  };

  State state = INIT;
  std::string statusMessage;
  std::string errorMessage;
  size_t syncedEventsCount = 0;
  bool syncStarted = false;

  void performSync();
  bool connectWifi();
  bool fetchAndParse(const std::string& url, int utcOffsetSeconds, int64_t minEpoch, int64_t maxEpoch,
                     std::vector<CalendarEvent>& outEvents);
};
