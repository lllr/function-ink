#include "CalendarConfigStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <utility>

void CalendarConfigStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["calendars"].to<JsonArray>();
  for (const auto& cal : calendars) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = cal.name;
    obj["url"] = cal.url;
    obj["enabled"] = cal.enabled;
  }
}

bool CalendarConfigStore::fromJson(JsonVariantConst doc) {
  calendars.clear();
  JsonArrayConst arr = doc["calendars"].as<JsonArrayConst>();
  calendars.reserve(std::min(arr.size(), MAX_CALENDARS));

  for (JsonObjectConst obj : arr) {
    if (calendars.size() >= CalendarConfigStore::MAX_CALENDARS) break;
    CalendarSource cal;
    cal.name = obj["name"] | "Calendar";
    cal.url = obj["url"] | "";
    cal.enabled = obj["enabled"] | true;
    if (!cal.url.empty()) {
      calendars.push_back(std::move(cal));
    }
  }

  return true;
}

bool CalendarConfigStore::loadFromFile() {
  calendars.clear();
  loaded_ = true;
  return PersistableStore<CalendarConfigStore>::loadFromFile();
}

void CalendarConfigStore::ensureLoaded() const {
  if (loaded_) return;
  const_cast<CalendarConfigStore*>(this)->loadFromFile();
}

void CalendarConfigStore::release() {
  std::vector<CalendarSource>().swap(calendars);
  loaded_ = false;
}

bool CalendarConfigStore::addCalendar(const CalendarSource& cal) {
  ensureLoaded();
  if (calendars.size() >= MAX_CALENDARS) {
    LOG_DBG("CAL", "Cannot add more calendars, limit reached");
    return false;
  }

  calendars.push_back(cal);
  return saveToFile();
}

bool CalendarConfigStore::updateCalendar(size_t index, const CalendarSource& cal) {
  ensureLoaded();
  if (index >= calendars.size()) {
    return false;
  }

  calendars[index] = cal;
  return saveToFile();
}

bool CalendarConfigStore::removeCalendar(size_t index) {
  ensureLoaded();
  if (index >= calendars.size()) {
    return false;
  }

  calendars.erase(calendars.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

const CalendarSource* CalendarConfigStore::getCalendar(size_t index) const {
  ensureLoaded();
  if (index >= calendars.size()) {
    return nullptr;
  }
  return &calendars[index];
}
