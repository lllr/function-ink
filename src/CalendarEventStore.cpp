#include "CalendarEventStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <utility>

void CalendarEventStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["events"].to<JsonArray>();
  for (const auto& ev : events) {
    JsonObject obj = arr.add<JsonObject>();
    obj["start"] = ev.startTime;
    obj["end"] = ev.endTime;
    obj["title"] = ev.title;
    obj["loc"] = ev.location;
    obj["desc"] = ev.description;
    obj["allDay"] = ev.allDay;
  }
}

bool CalendarEventStore::fromJson(JsonVariantConst doc) {
  events.clear();
  JsonArrayConst arr = doc["events"].as<JsonArrayConst>();
  events.reserve(std::min(arr.size(), MAX_EVENTS));

  for (JsonObjectConst obj : arr) {
    if (events.size() >= CalendarEventStore::MAX_EVENTS) break;
    CalendarEvent ev;
    ev.startTime = obj["start"] | 0;
    ev.endTime = obj["end"] | 0;
    ev.title = obj["title"] | "";
    ev.location = obj["loc"] | "";
    ev.description = obj["desc"] | "";
    ev.allDay = obj["allDay"] | false;
    if (ev.startTime > 0 || ev.allDay) {
      events.push_back(std::move(ev));
    }
  }

  // Sort events chronologically
  std::sort(events.begin(), events.end(),
            [](const CalendarEvent& a, const CalendarEvent& b) { return a.startTime < b.startTime; });

  return true;
}

bool CalendarEventStore::loadFromFile() {
  events.clear();
  loaded_ = true;
  return PersistableStore<CalendarEventStore>::loadFromFile();
}

void CalendarEventStore::ensureLoaded() const {
  if (loaded_) return;
  const_cast<CalendarEventStore*>(this)->loadFromFile();
}

void CalendarEventStore::release() {
  std::vector<CalendarEvent>().swap(events);
  loaded_ = false;
}

bool CalendarEventStore::setEvents(std::vector<CalendarEvent> newEvents) {
  ensureLoaded();
  if (newEvents.size() > MAX_EVENTS) {
    newEvents.resize(MAX_EVENTS);
  }
  std::sort(newEvents.begin(), newEvents.end(),
            [](const CalendarEvent& a, const CalendarEvent& b) { return a.startTime < b.startTime; });
  events = std::move(newEvents);
  return saveToFile();
}

void CalendarEventStore::clear() {
  ensureLoaded();
  events.clear();
  saveToFile();
}
