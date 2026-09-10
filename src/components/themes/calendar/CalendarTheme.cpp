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

  if (events.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, startY + height / 3, "No upcoming events", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, startY + height / 3 + 28, "Sync via Home menu to update calendar", true);
    return;
  }

  int lastRenderedDay = -1;

  for (const auto& ev : events) {
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

      currentY += 8;
      if (currentY + 20 >= maxY) break;

      renderer.drawText(SMALL_FONT_ID, startX, currentY, dayHeader, true, EpdFontFamily::BOLD);
      renderer.drawLine(startX + 180, currentY + 7, startX + width, currentY + 7, true);
      currentY += 24;
    }

    if (currentY + 45 >= maxY) break;

    const bool isPast = (ev.endTime < currentLocalEpoch);
    const bool isCurrent = (ev.startTime <= currentLocalEpoch && ev.endTime >= currentLocalEpoch);

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

    // Active event indicator arrow
    if (isCurrent) {
      const int arrowX = startX + 2;
      const int arrowY = currentY + 3;
      renderer.drawLine(arrowX, arrowY, arrowX + 6, arrowY + 4, true);
      renderer.drawLine(arrowX + 6, arrowY + 4, arrowX, arrowY + 8, true);
      renderer.drawLine(arrowX, arrowY, arrowX, arrowY + 8, true);
    }

    // Time text
    const int timeX = startX + 14;
    renderer.drawText(UI_10_FONT_ID, timeX, currentY, timeStr, true, isCurrent ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (durStr[0] != '\0') {
      renderer.drawText(SMALL_FONT_ID, timeX, currentY + 18, durStr, true);
    }

    // Vertical accent bar
    const int barX = startX + 92;
    const int barH = 38;
    if (isPast) {
      // Dotted/hatched line for past events
      for (int dy = 0; dy < barH; dy += 4) {
        renderer.drawPixel(barX, currentY + dy, true);
        renderer.drawPixel(barX + 1, currentY + dy, true);
      }
    } else {
      // Solid vertical bar
      renderer.fillRect(barX, currentY, 3, barH, true);
    }

    // Right column: Title and Location
    const int detailsX = startX + 106;
    const int maxTitleW = width - 110;
    std::string truncatedTitle = renderer.truncatedText(UI_10_FONT_ID, ev.title.c_str(), maxTitleW,
                                                        isCurrent ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, detailsX, currentY, truncatedTitle.c_str(), true,
                      isCurrent ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    if (!ev.location.empty()) {
      std::string truncatedLoc = renderer.truncatedText(SMALL_FONT_ID, ev.location.c_str(), maxTitleW);
      renderer.drawText(SMALL_FONT_ID, detailsX, currentY + 18, truncatedLoc.c_str(), true);
    }

    currentY += 46;
  }
}

void CalendarTheme::drawSleepScreen(GfxRenderer& renderer, const std::vector<CalendarEvent>& events,
                                   int64_t currentLocalEpoch) const {
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
