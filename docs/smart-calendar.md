# Smart Calendar Sleep Screensaver

**FunctionInk** introduces a Smart Calendar sleep screensaver for CrossInk / CrossPoint e-readers (ESP32-C3 Xteink X3/X4, ESP32-S3 Seeed Studio Sticky, and X4 Pro). It turns your e-reader into an ambient desktop calendar while asleep, updating automatically every hour with your schedule.

---

## Features

- **Daily Hourly Timeline**:
  - Displays your scheduled events in a clean vertical timeline.
  - Strict **24-hour time format** (`08:30`, `13:00`, `15:00`).
  - Highlights the current or upcoming event with an active indicator (`▶`).
  - Shows event duration (e.g. `1 hr`, `45 min`, `1 hr 30 min`) and location/details.
  - Solid and hatched visual indicator bars for concurrent and upcoming events.
- **Multi-Day Lookahead**:
  - In addition to today's schedule, displays events up to 3 days ahead, clearly delineated with day-separator lines (`Tuesday, May 19`).
- **Clean Header & Mini-Week Bar**:
  - Header displays **Month & Year** (e.g. `MAY 2026`) and **Battery %** without clock clutter.
  - Mini-week calendar bar displays day initials (`S M T W T F S`) with days of the month, circling today's date.
- **Hourly RTC Deep-Sleep Wakeup**:
  - The device remains in ultra-low-power deep sleep between hours.
  - At the top of every hour (`:00`), the hardware RTC timer wakes up the microcontroller.
  - In a lightweight fast boot path (~1 second), only the e-ink screen is initialized and refreshed with the updated timeline and active event marker.
  - The device immediately returns to deep sleep, preserving battery life for weeks.
- **Private Google Calendar Sync via iCal (.ics)**:
  - Supports private Google Calendar URLs (`basic.ics`) as well as any standard RFC 5545 `.ics` feed.
  - Configurable via the built-in Web Portal.
  - On-demand manual sync button accessible directly from the **Home** menu.

---

## How It Works

```
┌─────────────────────────────────────────────────────────────┐
│ 1. SETUP (Web Portal)                                       │
│    Add Google Calendar private .ics link in Web Settings    │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. MANUAL SYNC (Home > Sync Calendar)                       │
│    - Connects to Wi-Fi & synchronizes RTC clock via NTP     │
│    - Fetches .ics feed via HTTPS (redirect-aware)           │
│    - Memory-safe streaming parse (RFC 5545)                 │
│    - Saves [Today + 3 Days] events to SD card JSON store    │
│    - Disconnects Wi-Fi immediately                          │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. AMBIENT SCREENSAVER (Deep Sleep)                         │
│    - Short-press Power button enters Deep Sleep             │
│    - CalendarTheme renders daily timeline on e-ink screen   │
│    - Arms ESP32 RTC timer for next hour mark (:00)          │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼ (Every hour at :00)
┌─────────────────────────────────────────────────────────────┐
│ 4. HOURLY RTC TIMER WAKEUP (~1s refresh)                    │
│    - Wakes from sleep without full OS or Wi-Fi stack        │
│    - Redraws timeline & active indicator (▶)               │
│    - Flushes 1-bit e-ink display                            │
│    - Re-arms timer and enters Deep Sleep again              │
└─────────────────────────────────────────────────────────────┘
```

---

## Setup & Configuration

### Step 1: Obtain your Google Calendar iCal Address

1. On a desktop or mobile browser, open [Google Calendar](https://calendar.google.com/).
2. In the left sidebar, locate your calendar under **My calendars**, click the three dots (`⋮`) next to it, and select **Settings and sharing**.
3. Scroll down to the **Integrate calendar** section.
4. Copy the URL found under **Secret address in iCal format** (it looks like `https://calendar.google.com/calendar/ical/<id>/private-<token>/basic.ics`).
   > [!WARNING]
   > Keep this URL private; anyone with the secret address can read your calendar events without logging in.

### Step 2: Configure via Web Portal

1. On your e-reader, turn on the Web Server:
   - Navigate to **Settings > Wi-Fi / Web Portal** (or use the quick shortcut).
   - Ensure your device is connected to your local Wi-Fi.
2. Open the web interface:
   - Scan the QR code shown on the e-reader with your phone, or enter `http://<device-ip>` in your browser.
3. Scroll to the **Calendar Sync (.ics)** section on the Web Settings page.
4. Paste your calendar name and the `.ics` secret address, then click **Add Calendar**.

### Step 3: Synchronize Calendar

1. On the e-reader, return to the **Home** menu.
2. Select the **Sync Calendar** menu item.
3. The reader will:
   - Connect to Wi-Fi.
   - Synchronize its hardware RTC clock via NTP.
   - Download the `.ics` stream.
   - Parse and save events locally to `/.crosspoint/calendar_events.json`.
   - Disconnect Wi-Fi and return to Home.

### Step 4: Enable the Calendar Screensaver

1. Open **Settings > Sleep Screen**.
2. Select **Calendar**.
3. Press the Power button to put your reader to sleep. The screen will now display your hourly calendar!

---

## Technical Details & Architecture

### Memory Discipline (ESP32-C3 & ESP32-S3)
- The ESP32-C3 has ~380 KB internal SRAM and no PSRAM.
- To prevent heap fragmentation and allocation crashes during parsing:
  - The iCal parser (`lib/Calendar/IcalParser.cpp`) processes the payload in a single streaming pass.
  - Multi-line folded headers are reassembled dynamically.
  - Events occurring before the current day (`minEpoch`) are discarded immediately.
  - Only events within a 4-day window (`[today, today + 3 days]`) are retained.
  - Event capacity is strictly capped to 50 items.
  - HTTPS download buffers in `CalendarSyncActivity.cpp` are capped to 128 KB.

### Fast RTC Deep-Sleep Wakeup
- When sleep mode is entered (`src/main.cpp`), the firmware calculates the seconds remaining until the next exact hour:
  $$\Delta t = (59 - \text{minute}) \times 60 + (60 - \text{second})$$
- It invokes `esp_sleep_enable_timer_wakeup(\Delta t \times 1\,000\,000\mu s)`.
- When waking with cause `ESP_SLEEP_WAKEUP_TIMER`, `setup()` detects `CALENDAR_SLEEP` mode and runs a dedicated minimal initialization:
  - Bypasses Wi-Fi, SD-card scanning, and font loaders.
  - Initializes the e-ink display driver.
  - Draws the updated `CalendarTheme` using saved events.
  - Updates the e-ink panel and re-enters `esp_deep_sleep_start()`.
  - Total awake time is approximately 1 to 1.5 seconds.

---

## Flashing the Pre-Built Firmware

You can install FunctionInk using the pre-compiled `.bin` file found in `releases/`:
- `releases/crossink-v1.5.0-x3-x4-smart-calendar.bin` (for Xteink X3 / X4)
- `releases/crossink-v1.5.0-sticky-smart-calendar.bin` (for Seeed Studio Sticky)

### Option A: Web Portal OTA Update (Easiest)
1. If your device is already running CrossInk or CrossPoint, launch the Web Portal (**Settings > Wi-Fi / Web Portal**).
2. On your phone or computer, open `http://<device-ip>/settings`.
3. In the **Firmware Update** section, select the `.bin` file and click **Update**.
4. The device will flash the new firmware and reboot automatically.

### Option B: Web Installer / USB Flasher
1. Connect the e-reader to your computer via USB-C.
2. Open [Inky Web Installer](https://inky.crossink.dev/#flash-tools) or [ESPHome Web Flasher](https://web.esphome.io/).
3. Select the `.bin` file and flash to offset `0x10000` (or as prompted).
