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

const char* DAY_INITIALS[] = {"S", "M", "T", "W", "T", "F", "S"};

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

  // Compute Sunday of this week
  const int64_t sundayEpoch = todayEpoch - todayComponents.dayOfWeek * 86400LL;

  const int colWidth = width / 7;

  for (int i = 0; i < 7; ++i) {
    const int colX = startX + i * colWidth + colWidth / 2;
    const int64_t dayEpoch = sundayEpoch + i * 86400LL;
    const auto dt = epochToComponents(dayEpoch);

    // Day letter
    const int letterW = renderer.getTextWidth(SMALL_FONT_ID, DAY_INITIALS[i]);
    renderer.drawText(SMALL_FONT_ID, colX - letterW / 2, startY, DAY_INITIALS[i], true);

    // Day number
    char numBuf[8];
    snprintf(numBuf, sizeof(numBuf), "%d", dt.day);
    const int numW = renderer.getTextWidth(UI_10_FONT_ID, numBuf);
    const int numY = startY + 22;

    const bool isToday = (i == todayComponents.dayOfWeek);
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

  // Filter out events that ended before now
  std::vector<const CalendarEvent*> upcomingEvents;
  upcomingEvents.reserve(events.size());
  for (const auto& ev : events) {
    if (ev.endTime >= currentLocalEpoch) {
      upcomingEvents.push_back(&ev);
    }
  }

  if (upcomingEvents.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, startY + height / 3, "No upcoming events", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, startY + height / 3 + 28, "Sync via Home menu to update calendar", true);
    return;
  }

  // Identify whether there is an active event NOW, or the first one is NEXT
  bool hasActiveNow = false;
  for (const auto* ev : upcomingEvents) {
    if (ev->startTime <= currentLocalEpoch && ev->endTime >= currentLocalEpoch) {
      hasActiveNow = true;
      break;
    }
  }

  int lastRenderedDay = -1;
  bool nextEventMarked = false;

  for (size_t idx = 0; idx < upcomingEvents.size(); ++idx) {
    const auto& ev = *upcomingEvents[idx];
    if (currentY >= maxY) break;

    const auto evStart = epochToComponents(ev.startTime);
    const bool isToday = (evStart.year == now.year && evStart.month == now.month && evStart.day == now.day);
    const int daysDiff = static_cast<int>((ev.startTime / 86400LL) - (currentLocalEpoch / 86400LL));

    // Draw date separator when day transitions
    if (evStart.day != lastRenderedDay) {
      lastRenderedDay = evStart.day;

      char dayHeader[64];
      if (isToday) {
        snprintf(dayHeader, sizeof(dayHeader), "Today · %s, %s %d", DAY_NAMES[evStart.dayOfWeek],
                 MONTH_NAMES[evStart.month - 1], evStart.day);
      } else if (daysDiff == 1) {
        snprintf(dayHeader, sizeof(dayHeader), "Tomorrow · %s, %s %d", DAY_NAMES[evStart.dayOfWeek],
                 MONTH_NAMES[evStart.month - 1], evStart.day);
      } else {
        snprintf(dayHeader, sizeof(dayHeader), "%s, %s %d", DAY_NAMES[evStart.dayOfWeek],
                 MONTH_NAMES[evStart.month - 1], evStart.day);
      }

      currentY += 10;
      if (currentY + 24 >= maxY) break;

      const int textW = renderer.getTextWidth(SMALL_FONT_ID, dayHeader);
      renderer.drawText(SMALL_FONT_ID, startX, currentY, dayHeader, true, EpdFontFamily::BOLD);
      const int lineStartX = startX + textW + 12;
      if (lineStartX < startX + width) {
        renderer.drawLine(lineStartX, currentY + 7, startX + width, currentY + 7, true);
      }
      currentY += 28;
    }

    if (currentY + 48 >= maxY) break;

    const bool isCurrent = (ev.startTime <= currentLocalEpoch && ev.endTime >= currentLocalEpoch);
    const bool isNext = (!hasActiveNow && !nextEventMarked && ev.startTime > currentLocalEpoch);
    if (isNext) {
      nextEventMarked = true;
    }

    // Left column: Time and duration
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

    // Active or Next indicator
    if (isCurrent) {
      const int arrowX = startX + 2;
      const int arrowY = currentY + 4;
      renderer.drawLine(arrowX, arrowY, arrowX + 6, arrowY + 4, true);
      renderer.drawLine(arrowX + 6, arrowY + 4, arrowX, arrowY + 8, true);
      renderer.drawLine(arrowX, arrowY, arrowX, arrowY + 8, true);
    }

    // Time text
    const int timeX = startX + 14;
    renderer.drawText(UI_10_FONT_ID, timeX, currentY, timeStr, true,
                      (isCurrent || isNext) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (durStr[0] != '\0') {
      renderer.drawText(SMALL_FONT_ID, timeX, currentY + 22, durStr, true);
    }

    // Vertical accent bar
    const int barX = startX + 96;
    const int barH = 40;
    if (isCurrent) {
      renderer.fillRect(barX, currentY, 4, barH, true);
    } else if (isNext) {
      renderer.fillRect(barX, currentY, 3, barH, true);
    } else {
      renderer.fillRect(barX, currentY, 1, barH, true);
    }

    // Right column: Badges, Title and Location
    const int detailsX = startX + 110;
    int titleOffsetX = 0;

    if (isCurrent) {
      // Solid black pill with inverted white text "NOW"
      const int badgeW = 40;
      const int badgeH = 17;
      const int badgeX = detailsX;
      const int badgeY = currentY + 1;
      renderer.fillRoundedRect(badgeX, badgeY, badgeW, badgeH, 4, Color::Black);
      const int textW = renderer.getTextWidth(SMALL_FONT_ID, "NOW");
      const int textH = renderer.getLineHeight(SMALL_FONT_ID);
      renderer.drawText(SMALL_FONT_ID, badgeX + (badgeW - textW) / 2, badgeY + (badgeH - textH) / 2, "NOW", false, EpdFontFamily::BOLD);
      titleOffsetX = badgeW + 8;
    } else if (isNext) {
      // Outline rounded pill with black text "NEXT"
      const int badgeW = 44;
      const int badgeH = 17;
      const int badgeX = detailsX;
      const int badgeY = currentY + 1;
      renderer.drawRoundedRect(badgeX, badgeY, badgeW, badgeH, 1, 4, true);
      const int textW = renderer.getTextWidth(SMALL_FONT_ID, "NEXT");
      const int textH = renderer.getLineHeight(SMALL_FONT_ID);
      renderer.drawText(SMALL_FONT_ID, badgeX + (badgeW - textW) / 2, badgeY + (badgeH - textH) / 2, "NEXT", true, EpdFontFamily::BOLD);
      titleOffsetX = badgeW + 8;
    }

    const int maxTitleW = width - 116 - titleOffsetX;
    std::string truncatedTitle = renderer.truncatedText(UI_10_FONT_ID, ev.title.c_str(), maxTitleW,
                                                        (isCurrent || isNext) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, detailsX + titleOffsetX, currentY, truncatedTitle.c_str(), true,
                      (isCurrent || isNext) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    if (!ev.location.empty()) {
      std::string truncatedLoc = renderer.truncatedText(SMALL_FONT_ID, ev.location.c_str(), width - 116);
      renderer.drawText(SMALL_FONT_ID, detailsX, currentY + 22, truncatedLoc.c_str(), true);
    }

    currentY += 52;
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
