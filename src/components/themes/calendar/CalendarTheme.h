#pragma once

#include <GfxRenderer.h>
#include <cstdint>
#include <string>
#include <vector>

#include "CalendarEventStore.h"

class CalendarTheme {
 public:
  CalendarTheme() = default;

  /**
   * Draw the smart calendar screensaver.
   * @param renderer E-ink renderer reference.
   * @param events List of calendar events to render.
   * @param currentLocalEpoch Current wall-clock time in local epoch seconds.
   */
  void drawSleepScreen(GfxRenderer& renderer, const std::vector<CalendarEvent>& events,
                       int64_t currentLocalEpoch) const;

 private:
  void drawHeader(GfxRenderer& renderer, int year, int month, int startX, int startY, int width) const;
  void drawMiniWeek(GfxRenderer& renderer, int year, int month, int day, int startX, int startY, int width) const;
  void drawTimeline(GfxRenderer& renderer, const std::vector<CalendarEvent>& events,
                    int64_t currentLocalEpoch, int startX, int startY, int width, int height) const;
};
