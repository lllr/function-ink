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

void drawOpenTopCard(const GfxRenderer& renderer, int x, int y, int width, int height, int lineWidth, int radius) {
  const int right = x + width - 1;
  const int bottom = y + height - 1;
  const int maxRadius = std::min({radius, width / 2, height / 2});

  // Left vertical line from top down to bottom curve
  renderer.fillRect(x, y, lineWidth, height - maxRadius, true);
  // Right vertical line from top down to bottom curve
  renderer.fillRect(right - lineWidth + 1, y, lineWidth, height - maxRadius, true);
  // Bottom horizontal line between bottom curves
  renderer.fillRect(x + maxRadius, bottom - lineWidth + 1, width - 2 * maxRadius, lineWidth, true);
  // Bottom-left curve
  renderer.drawArc(maxRadius, x + maxRadius, bottom - maxRadius, -1, 1, lineWidth, true);
  // Bottom-right curve
  renderer.drawArc(maxRadius, right - maxRadius, bottom - maxRadius, 1, 1, lineWidth, true);
}

}  // namespace

void CalendarTheme::drawHeader(GfxRenderer& renderer, int year, int month, int startX, int startY, int width) const {
  char titleBuf[64];
  const char* monthName = (month >= 1 && month <= 12) ? MONTH_NAMES[month - 1] : "Calendar";
  snprintf(titleBuf, sizeof(titleBuf), "%s %d", monthName, year);

  // Draw month name + year in bold on the left
  renderer.drawText(UI_12_FONT_ID, startX, startY + 6, titleBuf, true, EpdFontFamily::BOLD);

  // Draw battery percentage + crisp clean icon on the top right
  const uint16_t percentage = powerManager.getBatteryPercentage();
  char pctBuf[16];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", percentage);
  const int pctW = renderer.getTextWidth(SMALL_FONT_ID, pctBuf);
  const int pctH = renderer.getLineHeight(SMALL_FONT_ID);

  const int battW = 24;
  const int battH = 12;
  const int battX = startX + width - battW - 2;
  const int battY = startY + 8;
  const int pctX = battX - pctW - 7;
  const int pctY = battY + (battH - pctH) / 2;

  // Percentage text vertically centered with battery
  renderer.drawText(SMALL_FONT_ID, pctX, pctY, pctBuf, true);

  // Battery body outline (crisp 1px rectangle)
  renderer.drawRect(battX, battY, battW, battH, true);
  // Battery terminal nub on the right
  renderer.fillRect(battX + battW, battY + 3, 2, battH - 6, true);

  // Battery level fill
  const int innerW = battW - 4;
  const int fillW = std::clamp(static_cast<int>((percentage * innerW) / 100), 0, innerW);
  if (fillW > 0) {
    renderer.fillRect(battX + 2, battY + 2, fillW, battH - 4, true);
  }
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

namespace {

int calculateEventSlots(const CalendarEvent& ev, int64_t currentLocalEpoch) {
  if (ev.allDay) return 2;
  const auto now = epochToComponents(currentLocalEpoch);
  const auto evEnd = epochToComponents(ev.endTime);
  int currentHour = now.hour;
  int endHour = evEnd.hour;
  if (evEnd.day != now.day) {
    endHour += 24 * (evEnd.day - now.day);
  }
  int slots = endHour - currentHour;
  if (evEnd.minute > 0) {
    slots++;
  }
  return std::max(1, slots);
}

void renderEventRow(GfxRenderer& renderer, const CalendarEvent& ev, const DateTimeComponents& evStart,
                    bool isCurrent, bool isNext, bool isOpenTop, int startX, int currentY, int width, int maxY,
                    int cardH) {
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
  // ACTIVE EVENT "HAPPENING NOW" -> Framed Card Box
  // -------------------------------------------------------------
  if (isCurrent) {
    // Crisp filled arrow ▶
    const int arrowX = startX;
    const int arrowY = currentY + 5;
    renderer.drawLine(arrowX, arrowY, arrowX, arrowY + 6, true);
    renderer.drawLine(arrowX + 1, arrowY + 1, arrowX + 1, arrowY + 5, true);
    renderer.drawLine(arrowX + 2, arrowY + 2, arrowX + 2, arrowY + 4, true);
    renderer.drawPixel(arrowX + 3, arrowY + 3, true);

    // Outlined badge [ NOW ] matching [ NEXT ] and [ SAT ]
    const int badgeX = startX + 8;
    const int badgeW = 42;
    const int badgeH = 17;
    renderer.drawRoundedRect(badgeX, currentY + 1, badgeW, badgeH, 1, 4, true);
    const int textW = renderer.getTextWidth(SMALL_FONT_ID, "NOW");
    const int textH = renderer.getLineHeight(SMALL_FONT_ID);
    renderer.drawText(SMALL_FONT_ID, badgeX + (badgeW - textW) / 2, currentY + 1 + (badgeH - textH) / 2,
                      "NOW", true, EpdFontFamily::BOLD);

    if (durStr[0] != '\0') {
      renderer.drawText(SMALL_FONT_ID, startX + 8, currentY + 22, durStr, true);
    }

    // Card box on the right
    const int cardX = startX + 78;
    const int cardW = width - 78;

    if (currentY + cardH <= maxY) {
      if (isOpenTop) {
        // Ongoing event that already started in the past: keep top corners open (no top stroke)
        drawOpenTopCard(renderer, cardX, currentY, cardW, cardH, 2, 6);
      } else {
        renderer.drawRoundedRect(cardX, currentY, cardW, cardH, 2, 6, true);
      }

      // Title inside card
      const int innerPadX = 14;
      const int innerPadY = 12;
      const int maxTitleW = cardW - innerPadX * 2;
      std::string truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, ev.title.c_str(), maxTitleW, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, cardX + innerPadX, currentY + innerPadY, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);

      // Subtitle inside card: Time range & location
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
    }
  }
  // -------------------------------------------------------------
  // UPCOMING OR FUTURE EVENT -> Clean Row
  // -------------------------------------------------------------
  else {
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
  }
}
}  // namespace

void CalendarTheme::drawTimeline(GfxRenderer& renderer, const std::vector<CalendarEvent>& events,
                                int64_t currentLocalEpoch, int startX, int startY, int width, int height) const {
  const auto now = epochToComponents(currentLocalEpoch);
  const int maxY = startY + height - 16;

  // 1. Keep events that ended within the past 1 hour onwards
  const int64_t windowStart = currentLocalEpoch - 3600LL;

  std::vector<const CalendarEvent*> todayEvents;
  std::vector<const CalendarEvent*> futureEvents;
  todayEvents.reserve(events.size());
  futureEvents.reserve(events.size());

  for (const auto& ev : events) {
    if (ev.endTime < windowStart) continue;
    const auto evStart = epochToComponents(ev.startTime);
    const bool isToday = (evStart.year == now.year && evStart.month == now.month && evStart.day == now.day);
    if (isToday) {
      todayEvents.push_back(&ev);
    } else {
      futureEvents.push_back(&ev);
    }
  }

  if (todayEvents.empty() && futureEvents.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, startY + height / 3, "No upcoming events", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, startY + height / 3 + 28, "Sync via Home menu to update calendar", true);
    return;
  }

  // Active event happening NOW (if any)
  const CalendarEvent* activeNowEvent = nullptr;
  for (const auto* ev : todayEvents) {
    if (ev->startTime <= currentLocalEpoch && ev->endTime >= currentLocalEpoch) {
      activeNowEvent = ev;
      break;
    }
  }

  // Calculate bottom section height for future events (if any)
  int futureSectionH = 0;
  int futureEventsToDraw = 0;
  if (!futureEvents.empty()) {
    futureSectionH = 34;  // Day badge + horizontal line
    const int firstFutureDay = epochToComponents(futureEvents[0]->startTime).day;
    for (const auto* ev : futureEvents) {
      if (epochToComponents(ev->startTime).day == firstFutureDay && futureEventsToDraw < 2) {
        futureSectionH += 48;
        futureEventsToDraw++;
      } else {
        break;
      }
    }
  }

  const int futureStartY = futureSectionH > 0 ? (maxY - futureSectionH) : maxY;
  const int maxTodayY = futureSectionH > 0 ? (futureStartY - 16) : maxY;

  // -------------------------------------------------------------
  // 1. Render TODAY's events from the top
  // -------------------------------------------------------------
  int currentY = startY;
  bool nextEventMarked = false;

  if (todayEvents.empty()) {
    renderer.drawText(SMALL_FONT_ID, startX + 10, currentY + 8, "No remaining events today", true);
    currentY += 32;
  } else {
    for (const auto* ev : todayEvents) {
      const auto evStart = epochToComponents(ev->startTime);
      const bool isCurrent = (&*ev == activeNowEvent);
      const bool isNext = (!activeNowEvent && !nextEventMarked && ev->startTime > currentLocalEpoch);
      if (isNext) nextEventMarked = true;

      const int minCardH = !ev->location.empty() ? 76 : 64;
      int cardH = minCardH;
      int rowH = 48;

      if (isCurrent) {
        const int slots = calculateEventSlots(*ev, currentLocalEpoch);
        constexpr int slotH = 48;
        const int desiredH = slots * slotH;
        cardH = std::max(minCardH, desiredH - 8);
        rowH = std::max(minCardH + 12, desiredH);

        // Cap to available today height so it doesn't collide with bottom future section
        if (currentY + cardH > maxTodayY) {
          cardH = std::max(minCardH, maxTodayY - currentY - 8);
          rowH = maxTodayY - currentY;
        }
      }

      if (currentY + 40 > maxTodayY) break;

      // Check if active event already has time in the past: keep top corners open (no top stroke)
      const bool isOpenTop = isCurrent && (ev->startTime < currentLocalEpoch);

      renderEventRow(renderer, *ev, evStart, isCurrent, isNext, isOpenTop, startX, currentY, width, maxTodayY, cardH);
      currentY += rowH;
    }
  }

  // -------------------------------------------------------------
  // 2. Render FUTURE events attached to the BOTTOM
  // -------------------------------------------------------------
  if (futureSectionH > 0 && futureEventsToDraw > 0) {
    int bottomY = futureStartY;
    const auto firstEvStart = epochToComponents(futureEvents[0]->startTime);
    const int daysDiff = static_cast<int>((futureEvents[0]->startTime / 86400LL) - (currentLocalEpoch / 86400LL));

    // Draw Day Badge Pill, e.g. [THU] or [FRI 12]
    char badgeText[32];
    if (daysDiff == 1) {
      snprintf(badgeText, sizeof(badgeText), "%s", SHORT_DAY_NAMES[firstEvStart.dayOfWeek]);
    } else {
      snprintf(badgeText, sizeof(badgeText), "%s %d", SHORT_DAY_NAMES[firstEvStart.dayOfWeek], firstEvStart.day);
    }

    const int badgeW = renderer.getTextWidth(SMALL_FONT_ID, badgeText) + 18;
    const int badgeH = 20;
    renderer.drawRoundedRect(startX, bottomY, badgeW, badgeH, 1, 4, true);
    renderer.drawText(SMALL_FONT_ID, startX + 9, bottomY + 3, badgeText, true, EpdFontFamily::BOLD);

    // Connecting horizontal line
    renderer.drawLine(startX + badgeW + 10, bottomY + badgeH / 2, startX + width, bottomY + badgeH / 2, true);
    bottomY += badgeH + 14;

    for (int i = 0; i < futureEventsToDraw; ++i) {
      const auto* ev = futureEvents[i];
      const auto evStart = epochToComponents(ev->startTime);
      const bool isNext = (!activeNowEvent && !nextEventMarked && i == 0);
      if (isNext) nextEventMarked = true;

      renderEventRow(renderer, *ev, evStart, false, isNext, false, startX, bottomY, width, maxY, 64);
      bottomY += 48;
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
