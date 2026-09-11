#include "CalendarTheme.h"

#include <HalPowerManager.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {

bool isLeapYear(int year) {
  return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

int daysInMonth(int year, int month) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) return 29;
  if (month >= 1 && month <= 12) return days[month - 1];
  return 30;
}

int64_t toEpoch(int year, int month, int day, int hour, int minute, int second) {
  int64_t days = 0;
  for (int y = 1970; y < year; ++y) {
    days += isLeapYear(y) ? 366 : 365;
  }
  for (int m = 1; m < month; ++m) {
    days += daysInMonth(year, m);
  }
  days += (day - 1);
  return days * 86400LL + hour * 3600LL + minute * 60LL + second;
}

const char* MONTH_NAMES[] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December"};

const char* DAY_NAMES[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

const char* DAY_INITIALS[] = {"M", "T", "W", "T", "F", "S", "S"};
const char* SHORT_DAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

struct DateTimeComponents {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  int dayOfWeek;  // 0 = Sunday, 1 = Monday, ..., 6 = Saturday
};

DateTimeComponents epochToComponents(int64_t epoch) {
  DateTimeComponents dt{};
  if (epoch <= 0) return dt;

  int64_t days = epoch / 86400LL;
  int64_t remSec = epoch % 86400LL;
  if (remSec < 0) {
    remSec += 86400LL;
    days--;
  }

  dt.hour = static_cast<int>(remSec / 3600);
  dt.minute = static_cast<int>((remSec % 3600) / 60);
  dt.second = static_cast<int>(remSec % 60);

  // 1970-01-01 was Thursday (day index 4)
  dt.dayOfWeek = static_cast<int>((days + 4) % 7);
  if (dt.dayOfWeek < 0) dt.dayOfWeek += 7;

  int y = 1970;
  while (true) {
    int daysInY = isLeapYear(y) ? 366 : 365;
    if (days < daysInY) break;
    days -= daysInY;
    y++;
  }
  dt.year = y;

  int m = 1;
  while (true) {
    int daysInM = daysInMonth(y, m);
    if (days < daysInM) break;
    days -= daysInM;
    m++;
  }
  dt.month = m;
  dt.day = static_cast<int>(days + 1);

  return dt;
}

}  // namespace

void CalendarTheme::drawHeader(GfxRenderer& renderer, int year, int month, int startX, int startY, int width) const {
  char titleBuf[64];
  const char* monthName = (month >= 1 && month <= 12) ? MONTH_NAMES[month - 1] : "Calendar";
  snprintf(titleBuf, sizeof(titleBuf), "%s %d", monthName, year);

  // Draw month name + year in bold on the left
  renderer.drawText(UI_12_FONT_ID, startX, startY + 6, titleBuf, true, EpdFontFamily::BOLD);

  // Draw battery on the top right
  const int batteryX = startX + width - 65;
  GUI.drawBatteryRight(renderer, Rect{batteryX, startY + 4, 38, 14}, true, true);
}

void CalendarTheme::drawMiniWeek(GfxRenderer& renderer, int year, int month, int day, int startX, int startY,
                                int width) const {
  const int64_t todayEpoch = toEpoch(year, month, day, 12, 0, 0);
  const auto todayComponents = epochToComponents(todayEpoch);

  // European / ISO standard: Monday is first day of week (M T W T F S S)
  // todayComponents.dayOfWeek: 0 = Sun, 1 = Mon, ..., 6 = Sat
  const int displayDayIndex = (todayComponents.dayOfWeek + 6) % 7;  // 0 = Mon, ..., 6 = Sun
  const int64_t mondayEpoch = todayEpoch - displayDayIndex * 86400LL;

  const int colWidth = width / 7;

  for (int i = 0; i < 7; ++i) {
    const int colX = startX + i * colWidth + colWidth / 2;
    const int64_t dayEpoch = mondayEpoch + i * 86400LL;
    const auto dt = epochToComponents(dayEpoch);

    // Day letter
    const int letterW = renderer.getTextWidth(SMALL_FONT_ID, DAY_INITIALS[i]);
    renderer.drawText(SMALL_FONT_ID, colX - letterW / 2, startY, DAY_INITIALS[i], true);

    // Day number
    char numBuf[8];
    snprintf(numBuf, sizeof(numBuf), "%d", dt.day);
    const int numW = renderer.getTextWidth(UI_10_FONT_ID, numBuf);
    const int numY = startY + 22;

    const bool isToday = (i == displayDayIndex);
    if (isToday) {
      // Highlight current day with circle
      renderer.fillRoundedRect(colX - 14, numY - 3, 28, 28, 14, Color::Black);
      renderer.drawText(UI_10_FONT_ID, colX - numW / 2, numY, numBuf, false, EpdFontFamily::BOLD);
    } else {
      renderer.drawText(UI_10_FONT_ID, colX - numW / 2, numY, numBuf, true);
    }
  }

  // Divider line
  const int dividerY = startY + 54;
  renderer.drawLine(startX, dividerY, startX + width, dividerY, true);
}

void CalendarTheme::drawTimeline(GfxRenderer& renderer, const std::vector<CalendarEvent>& events,
                                int64_t currentLocalEpoch, int startX, int startY, int width, int height) const {
  const auto now = epochToComponents(currentLocalEpoch);
  int currentY = startY;
  const int maxY = startY + height - 20;

  // 1. Rolling window: Keep events that ended within the past 1 hour (windowStart) onwards
  const int64_t windowStart = currentLocalEpoch - 3600LL;

  std::vector<const CalendarEvent*> relevantEvents;
  relevantEvents.reserve(events.size());
  for (const auto& ev : events) {
    if (ev.endTime >= windowStart) {
      relevantEvents.push_back(&ev);
    }
  }

  if (relevantEvents.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, startY + height / 3, "No upcoming events", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, startY + height / 3 + 28, "Sync via Home menu to update calendar", true);
    return;
  }

  // 2. Identify the active event happening NOW (if any)
  const CalendarEvent* activeNowEvent = nullptr;
  for (const auto* ev : relevantEvents) {
    if (ev->startTime <= currentLocalEpoch && ev->endTime >= currentLocalEpoch) {
      activeNowEvent = ev;
      break;
    }
  }

  int lastRenderedDay = now.day;
  bool nextEventMarked = false;

  for (size_t idx = 0; idx < relevantEvents.size(); ++idx) {
    const auto& ev = *relevantEvents[idx];
    if (currentY >= maxY) break;

    const auto evStart = epochToComponents(ev.startTime);
    const bool isToday = (evStart.year == now.year && evStart.month == now.month && evStart.day == now.day);
    const int daysDiff = static_cast<int>((ev.startTime / 86400LL) - (currentLocalEpoch / 86400LL));
    const bool isCurrent = (&ev == activeNowEvent);

    // If this event is on a future day and day has changed:
    if (!isToday && evStart.day != lastRenderedDay) {
      lastRenderedDay = evStart.day;

      currentY += 12;
      if (currentY + 36 >= maxY) break;

      // Draw Day Badge Pill (e.g. [THU] or [FRI 12])
      char badgeText[32];
      if (daysDiff == 1) {
        snprintf(badgeText, sizeof(badgeText), "%s", SHORT_DAY_NAMES[evStart.dayOfWeek]);
      } else {
        snprintf(badgeText, sizeof(badgeText), "%s %d", SHORT_DAY_NAMES[evStart.dayOfWeek], evStart.day);
      }

      const int badgeW = renderer.getTextWidth(SMALL_FONT_ID, badgeText) + 18;
      const int badgeH = 20;
      renderer.drawRoundedRect(startX, currentY, badgeW, badgeH, 1, 4, true);
      renderer.drawText(SMALL_FONT_ID, startX + 9, currentY + 3, badgeText, true, EpdFontFamily::BOLD);

      // Horizontal line extending from badge to the right edge
      renderer.drawLine(startX + badgeW + 10, currentY + badgeH / 2, startX + width, currentY + badgeH / 2, true);
      currentY += badgeH + 14;
    }

    if (currentY + 44 >= maxY) break;

    const bool isNext = (!activeNowEvent && !nextEventMarked && ev.startTime > currentLocalEpoch);
    if (isNext) {
      nextEventMarked = true;
    }

    // Left column: Time and duration strings
    char timeStr[16];
    if (ev.allDay) {
      snprintf(timeStr, sizeof(timeStr), "All day");
    } else {
      snprintf(timeStr, sizeof(timeStr), "%02d:%02d", evStart.hour, evStart.minute);
    }

    char durStr[32];
    if (ev.allDay) {
      durStr[0] = '\0';
    } else {
      int64_t dur = ev.endTime - ev.startTime;
      int h = static_cast<int>(dur / 3600);
      int m = static_cast<int>((dur % 3600) / 60);
      if (h > 0 && m > 0) {
        snprintf(durStr, sizeof(durStr), "%d hr, %d min", h, m);
      } else if (h > 0) {
        snprintf(durStr, sizeof(durStr), "%d hr", h);
      } else {
        snprintf(durStr, sizeof(durStr), "%d min", std::max(1, m));
      }
    }

    // -------------------------------------------------------------
    // CASE A: ACTIVE EVENT "HAPPENING NOW" -> Framed Card Box
    // -------------------------------------------------------------
    if (isCurrent) {
      // Left side: ▶ [Now] or ▶ 16:00
      const int arrowX = startX;
      const int arrowY = currentY + 4;
      renderer.drawLine(arrowX, arrowY, arrowX + 5, arrowY + 3, true);
      renderer.drawLine(arrowX + 5, arrowY + 3, arrowX, arrowY + 6, true);
      renderer.drawLine(arrowX, arrowY, arrowX, arrowY + 6, true);

      // Now badge pill on the left
      const int badgeW = 38;
      const int badgeH = 17;
      const int badgeX = startX + 10;
      const int badgeY = currentY + 1;
      renderer.fillRoundedRect(badgeX, badgeY, badgeW, badgeH, 4, Color::Black);
      const int textW = renderer.getTextWidth(SMALL_FONT_ID, "NOW");
      const int textH = renderer.getLineHeight(SMALL_FONT_ID);
      renderer.drawText(SMALL_FONT_ID, badgeX + (badgeW - textW) / 2, badgeY + (badgeH - textH) / 2,
                        "NOW", false, EpdFontFamily::BOLD);

      if (durStr[0] != '\0') {
        renderer.drawText(SMALL_FONT_ID, startX + 10, currentY + 22, durStr, true);
      }

      // Card box on the right
      const int cardX = startX + 78;
      const int cardW = width - 78;
      const int cardH = !ev.location.empty() ? 76 : 64;

      if (currentY + cardH >= maxY) break;

      // Draw rounded card border (2px)
      renderer.drawRoundedRect(cardX, currentY, cardW, cardH, 2, 6, true);

      // Event title inside card (prominent bold)
      const int innerPadX = 14;
      const int innerPadY = 12;
      const int maxTitleW = cardW - innerPadX * 2;
      std::string truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, ev.title.c_str(), maxTitleW, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, cardX + innerPadX, currentY + innerPadY, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);

      // Subtitle: Time range & location
      char subBuf[64];
      if (!ev.allDay) {
        const auto evEnd = epochToComponents(ev.endTime);
        if (!ev.location.empty()) {
          snprintf(subBuf, sizeof(subBuf), "%02d:%02d–%02d:%02d · %s", evStart.hour, evStart.minute,
                   evEnd.hour, evEnd.minute, ev.location.c_str());
        } else {
          snprintf(subBuf, sizeof(subBuf), "%02d:%02d – %02d:%02d (%s)", evStart.hour, evStart.minute,
                   evEnd.hour, evEnd.minute, durStr);
        }
      } else {
        snprintf(subBuf, sizeof(subBuf), "%s", ev.location.c_str());
      }

      if (subBuf[0] != '\0') {
        std::string truncatedSub = renderer.truncatedText(SMALL_FONT_ID, subBuf, maxTitleW);
        renderer.drawText(SMALL_FONT_ID, cardX + innerPadX, currentY + innerPadY + 24, truncatedSub.c_str(), true);
      }

      currentY += cardH + 16;
    }
    // -------------------------------------------------------------
    // CASE B: UPCOMING OR FUTURE EVENT -> Clean Timeline Row
    // -------------------------------------------------------------
    else {
      // Time text on left
      const int timeX = startX + 10;
      renderer.drawText(UI_10_FONT_ID, timeX, currentY, timeStr, true,
                        isNext ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      if (durStr[0] != '\0') {
        renderer.drawText(SMALL_FONT_ID, timeX, currentY + 22, durStr, true);
      }

      // Vertical separator bar
      const int barX = startX + 78;
      const int barH = 38;
      renderer.fillRect(barX, currentY, isNext ? 3 : 2, barH, true);

      // Title & location on right
      const int detailsX = startX + 90;
      int titleOffsetX = 0;

      if (isNext) {
        // Outline rounded pill [NEXT]
        const int badgeW = 42;
        const int badgeH = 17;
        renderer.drawRoundedRect(detailsX, currentY + 1, badgeW, badgeH, 1, 4, true);
        const int textW = renderer.getTextWidth(SMALL_FONT_ID, "NEXT");
        const int textH = renderer.getLineHeight(SMALL_FONT_ID);
        renderer.drawText(SMALL_FONT_ID, detailsX + (badgeW - textW) / 2, currentY + 1 + (badgeH - textH) / 2,
                          "NEXT", true, EpdFontFamily::BOLD);
        titleOffsetX = badgeW + 8;
      }

      const int maxTitleW = width - 90 - titleOffsetX;
      std::string truncatedTitle = renderer.truncatedText(UI_10_FONT_ID, ev.title.c_str(), maxTitleW,
                                                          isNext ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, detailsX + titleOffsetX, currentY, truncatedTitle.c_str(), true,
                        isNext ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

      if (!ev.location.empty()) {
        std::string truncatedLoc = renderer.truncatedText(SMALL_FONT_ID, ev.location.c_str(), width - 90);
        renderer.drawText(SMALL_FONT_ID, detailsX, currentY + 22, truncatedLoc.c_str(), true);
      }

      currentY += 48;
    }
  }
}

void CalendarTheme::drawSleepScreen(GfxRenderer& renderer, const std::vector<CalendarEvent>& events,
                                   int64_t currentLocalEpoch) const {
  if (currentLocalEpoch <= 0) {
    time_t rawNow = time(nullptr);
    struct tm* tmInfo = localtime(&rawNow);
    if (tmInfo && tmInfo->tm_year + 1900 >= 2025) {
      currentLocalEpoch = toEpoch(tmInfo->tm_year + 1900, tmInfo->tm_mon + 1, tmInfo->tm_mday,
                                  tmInfo->tm_hour, tmInfo->tm_min, tmInfo->tm_sec);
    }
  }

  renderer.clearScreen();

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int marginX = 24;
  const int marginY = 24;
  const int contentW = screenW - marginX * 2;

  const auto now = epochToComponents(currentLocalEpoch);

  // 1. Top Header
  drawHeader(renderer, now.year, now.month, marginX, marginY, contentW);

  // 2. Mini Week Grid
  drawMiniWeek(renderer, now.year, now.month, now.day, marginX, marginY + 40, contentW);

  // 3. Timeline
  const int timelineY = marginY + 110;
  const int timelineH = screenH - timelineY - marginY;
  drawTimeline(renderer, events, currentLocalEpoch, marginX, timelineY, contentW, timelineH);
}
