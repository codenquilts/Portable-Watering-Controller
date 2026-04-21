# Portable Watering Bin + MQTT

ESP32-based portable watering controller with:

- automatic morning and evening schedules
- manual watering from web UI and MQTT
- tank-level safety lockout
- Wi-Fi setup portal
- MQTT state reporting and remote control
- optional SMTP email alerts for low tank and error conditions

## What The System Does

This project controls a watering pump from an ESP32. It can run on a schedule, be started manually, and be monitored remotely.

Main functions:

- Scheduled watering
  The system supports separate `morning` and `evening` schedules.
- Day-of-week control
  Each schedule can be enabled only on selected days.
- Manual watering
  Watering can be started from the local web page or from MQTT.
- Stop and emergency stop
  The pump can be stopped remotely at any time.
- Tank protection
  If the tank falls below the configured minimum level, watering is refused or stopped.
- Runtime logging
  The controller records the last run start time, stop time, duration, and reason.
- Remote monitoring
  Pump state, tank level, time, schedule settings, and events are published over MQTT.
- Email warnings
  Optional SMTP email alerts can be sent for low tank warnings and runtime errors.

## Home Assistant Discovery

When MQTT is enabled, the controller publishes Home Assistant discovery payloads under `homeassistant/<component>/<device>/...`.
This allows supported Home Assistant installations to discover sensor and binary sensor entities automatically.

## How It Works

- The ESP32 keeps local time and checks schedules once per minute.
- A schedule runs only when:
  its enable flag is on, the current weekday is allowed, the current time matches `start_hhmm`, the system is boot-ready, and the tank is above the minimum level.
- Every run is timed, so the pump stops automatically after the configured number of minutes.
- The web UI allows local control and configuration.
- MQTT allows remote control, status monitoring, and schedule changes.

## Topic Base

All MQTT topics are under:

```text
watering/<device>/
```

`<device>` is based on the device name, converted to lowercase and sanitized.

MQTT broker details are saved on the device and can be changed from the web UI after flashing. `src/config.h` still provides first-boot defaults for `MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`, and `MQTT_PASS`, but users do not need to rebuild the firmware to change them.

Optional SMTP settings are also saved on the device and editable in the web UI. `src/config.h` only provides first-boot defaults for `SMTP_HOST`, `SMTP_PORT`, `SMTP_USER`, `SMTP_PASS`, `SMTP_FROM`, and `SMTP_USE_SSL`.

Example:

```text
watering/portablewatering/
```

## MQTT Command Topics

### Pump Control

- `cmd/pump`
  Use: basic pump on/off control.
  Payloads: `ON`, `OFF`, `1`, `0`, `true`, `false`
  Function:
  `ON` starts the pump using the morning runtime as the timed default.
  `OFF` stops the pump.

- `cmd/manual_run`
  Use: start a timed manual watering run.
  Payloads: minute value such as `1`, `5`, `10`
  Function:
  Starts the pump for the requested number of minutes.
  The value is clamped to the configured safe range, currently `1` to `15` minutes.

- `cmd/stop`
  Use: manual stop or emergency stop.
  Payloads: `1`, `true`, `on`, `stop`, `emergency`
  Function:
  Stops the pump immediately.

- `cmd/run_now`
  Use: manually start one of the saved schedules.
  Payloads: `MORNING`, `EVENING`
  Function:
  Starts the configured morning or evening runtime immediately.

- `cmd/reboot`
  Use: reboot the controller remotely.
  Payloads: `1`, `true`
  Function:
  Restarts the ESP32.

### Schedule Control

- `cmd/schedule/morning/enabled`
  Use: enable or disable the morning schedule.
  Payloads: `1`, `0`, `true`, `false`, `on`, `off`

- `cmd/schedule/morning/start_hhmm`
  Use: set the morning start time.
  Payload examples: `630`, `0630`

- `cmd/schedule/morning/run_min`
  Use: set morning runtime in minutes.
  Payload example: `5`

- `cmd/schedule/morning/days_mask`
  Use: set allowed days for the morning schedule.
  Payload example: `127` for every day, `62` for Mon-Fri, `65` for weekend

- `cmd/schedule/evening/enabled`
  Use: enable or disable the evening schedule.
  Payloads: `1`, `0`, `true`, `false`, `on`, `off`

- `cmd/schedule/evening/start_hhmm`
  Use: set the evening start time.
  Payload examples: `1830`, `1900`

- `cmd/schedule/evening/run_min`
  Use: set evening runtime in minutes.
  Payload example: `5`

- `cmd/schedule/evening/days_mask`
  Use: set allowed days for the evening schedule.
  Payload example: `127`, `62`, `65`

## MQTT State Topics

Useful retained state topics include:

- `state/online`
- `state/ip`
- `state/wifi_mode`
- `state/uptime_s`
- `state/time_valid`
- `state/time_epoch`
- `state/time_hhmm`
- `state/time_src`
- `state/boot_ready`
- `state/voltage_v`
- `state/tank_ml`
- `state/tank_min_ml`
- `state/tank_reset_ml`
- `state/usage_ml`
- `state/low_tank`
- `state/pump_on`
- `state/pump_elapsed_s`
- `state/last_start_ts`
- `state/last_stop_ts`
- `state/last_run_s`
- `state/last_reason`

Schedule state topics:

- `state/schedule/morning/enabled`
- `state/schedule/morning/start_hhmm`
- `state/schedule/morning/run_min`
- `state/schedule/morning/days_mask`
- `state/schedule/morning/days_text`
- `state/schedule/morning/day/sun`
- `state/schedule/morning/day/mon`
- `state/schedule/morning/day/tue`
- `state/schedule/morning/day/wed`
- `state/schedule/morning/day/thu`
- `state/schedule/morning/day/fri`
- `state/schedule/morning/day/sat`
- `state/schedule/evening/enabled`
- `state/schedule/evening/start_hhmm`
- `state/schedule/evening/run_min`
- `state/schedule/evening/days_mask`
- `state/schedule/evening/days_text`
- `state/schedule/evening/day/sun`
- `state/schedule/evening/day/mon`
- `state/schedule/evening/day/tue`
- `state/schedule/evening/day/wed`
- `state/schedule/evening/day/thu`
- `state/schedule/evening/day/fri`
- `state/schedule/evening/day/sat`

Event topic:

- `event`
  Function:
  Publishes non-retained JSON event messages for actions and status changes.

## Days Mask Reference

Bit layout:

- `1` = Sunday
- `2` = Monday
- `4` = Tuesday
- `8` = Wednesday
- `16` = Thursday
- `32` = Friday
- `64` = Saturday

Common values:

- `127` = every day
- `62` = Monday to Friday
- `65` = Saturday and Sunday
- `0` = no active days

## Example MQTT Commands

```text
watering/portablewatering/cmd/manual_run = 5
watering/portablewatering/cmd/stop = emergency
watering/portablewatering/cmd/run_now = MORNING
watering/portablewatering/cmd/schedule/morning/enabled = 1
watering/portablewatering/cmd/schedule/morning/start_hhmm = 0630
watering/portablewatering/cmd/schedule/morning/run_min = 4
watering/portablewatering/cmd/schedule/morning/days_mask = 62
watering/portablewatering/cmd/schedule/evening/enabled = 1
watering/portablewatering/cmd/schedule/evening/start_hhmm = 1830
watering/portablewatering/cmd/schedule/evening/run_min = 6
watering/portablewatering/cmd/schedule/evening/days_mask = 127
```

## Web UI Functions

The built-in web interface supports:

- live status display
- morning and evening schedule editing
- day-of-week selection
- manual pump on and off
- run-now buttons for saved schedules
- tank reset
- device naming
- setup hotspot name and password
- notification email options
- MQTT broker settings
- SMTP email settings
- Wi-Fi setup and Wi-Fi reset actions

## Safety Notes

- The pump should not run dry.
- The controller checks `minLevelMl` before starting watering.
- If tank level falls below the minimum while running, the system stops the pump.
- All manual and MQTT runs are still timed for safety.

## Build

PlatformIO project environment:

```text
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
```

MQTT and optional SMTP settings can be configured after flashing from the device web UI. The compiled values in `src/config.h` are only defaults for a fresh device.

Typical build command:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

The distributable firmware binary is created at:

```text
.pio/build/esp32/firmware.bin
```
