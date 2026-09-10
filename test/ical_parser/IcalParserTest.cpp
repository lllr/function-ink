#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "CalendarEvent.h"
#include "IcalParser.h"

int main() {
  const std::string sampleIcs =
      "BEGIN:VCALENDAR\r\n"
      "VERSION:2.0\r\n"
      "PRODID:-//Google Inc//Google Calendar 70.9054//EN\r\n"
      "BEGIN:VEVENT\r\n"
      "UID:event1@google.com\r\n"
      "DTSTART:20260518T130000Z\r\n"
      "DTEND:20260518T143000Z\r\n"
      "SUMMARY:Project\r\n"
      "  Tailspin\r\n"
      "LOCATION:Conference Room\\, Baker\r\n"
      "DESCRIPTION:Discussion on Q2 objectives\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "UID:event2@google.com\r\n"
      "DTSTART:20260518T083000Z\r\n"
      "DTEND:20260518T093000Z\r\n"
      "SUMMARY:Weekly Marketing Lunch\r\n"
      "LOCATION:TBD\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "UID:event3_past@google.com\r\n"
      "DTSTART:20250101T080000Z\r\n"
      "DTEND:20250101T090000Z\r\n"
      "SUMMARY:Old Past Event\r\n"
      "END:VEVENT\r\n"
      "END:VCALENDAR\r\n";

  std::vector<CalendarEvent> events;
  // Window covering May 18, 2026
  // 2026-05-18 00:00:00 UTC = 1779062400
  const int64_t minEpoch = 1779062400LL;
  const int64_t maxEpoch = minEpoch + 4 * 86400LL;
  const int utcOffsetSeconds = 7200;  // UTC+2

  bool ok = IcalParser::parse(sampleIcs, utcOffsetSeconds, minEpoch, maxEpoch, events, 50);
  assert(ok);
  assert(events.size() == 2);  // Old past event excluded!

  // Check chronological sort: 08:30Z should be first, 13:00Z second
  assert(events[0].title == "Weekly Marketing Lunch");
  assert(events[0].location == "TBD");

  // Check line unfolding and unescaping
  assert(events[1].title == "Project Tailspin");
  assert(events[1].location == "Conference Room, Baker");

  std::cout << "All IcalParser unit tests passed successfully!\n";
  return 0;
}
