// Wearable alerting device.
// Vibrates when an emergency alert arrives on Adafruit IO and reports the
// wearer's response. One button: tap = acknowledge, triple tap = SOS,
// hold 5 s = "safe / rescued".
//
// Everything runs from one non-blocking state machine in loop(); no delay().

#include <Adafruit_DRV2605.h>
#include <AdafruitIO_WiFi.h>
#include <Bounce2.h>
#include <WiFi.h>
#include "config.h"  // Wi-Fi and Adafruit IO credentials

// ---------- Tuning ----------
const uint8_t BUTTON_PIN = A1;
const uint16_t DEBOUNCE_MS = 3;            // raise if one press counts twice

const unsigned long WIFI_TRY_MS = 30000;   // time given to each SSID
const unsigned long WIFI_RETRY_MS = 600000;// wait after every SSID failed
const unsigned long TAP_GAP_MS = 1000;     // max gap between taps of a triple tap
const uint8_t SOS_TAPS = 3;
const unsigned long HOLD_MS = 5000;        // hold time for "SAFE RESCUED"
const unsigned long ALERT_WINDOW_MS = 5000;// time to acknowledge each alert stage
const unsigned long STAGE_GAP_MS = 5000;   // pause between stage 1 and stage 2
const unsigned long RESULT_SHOW_MS = 3000; // result stays on dashboard, then reset

const uint8_t FX_CONFIRM = 37;             // DRV2605 library 1 effect numbers
const uint8_t FX_WIFI_FAIL = 10;

// ---------- Alert types ----------
// Values on the "alert" / "D1" feeds that start an alert, and what to report.
struct AlertType {
  int code, altCode;
  const char *startMsg;  // device1 status when the alert starts
  const char *tag;       // suffix of the D1 messages
  const char *ackCode;   // D1first value once stage 1 is acknowledged
  const char *doneCode;  // written back to the alert feed when handled
  uint8_t effect;        // vibration pattern
  unsigned long pulseMs; // pattern repeat period
};

const AlertType ALERTS[] = {
  {1, 4, "SOS sent fire",   "fire", "1", "11", 58, 200},  // transition click
  {2, 6, "SOS sent zombie", "zom",  "2", "22",  1, 100},  // strong click
  {3, 8, "SOS sent both",   "both", "3", "33", 70, 150},  // long ramp down
};

const AlertType *findAlert(int value) {
  for (const AlertType &a : ALERTS)
    if (value == a.code || value == a.altCode) return &a;
  return nullptr;
}

// ---------- States ----------
// Order matters: every state from IDLE on assumes we are online.
enum State { WIFI_CONNECTING, WIFI_BACKOFF, IO_CONNECTING,
             IDLE, STAGE1, STAGE_GAP, STAGE2, SHOW_RESULT };
const char *STATE_NAMES[] = { "WIFI_CONNECTING", "WIFI_BACKOFF", "IO_CONNECTING",
                              "IDLE", "STAGE1", "STAGE_GAP", "STAGE2", "SHOW_RESULT" };

State state;
unsigned long stateStart;
unsigned long nextPulse;

void enter(State s) {
  state = s;
  stateStart = nextPulse = millis();
  Serial.printf("-> %s\n", STATE_NAMES[s]);
}

unsigned long inState() { return millis() - stateStart; }

// ---------- Hardware ----------
Adafruit_DRV2605 drv;  // one driver; the DRV2605 has a fixed I2C address
Bounce button;

AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, "", "");  // Wi-Fi is joined by us, see joinWiFi()
AdafruitIO_Feed *alertFeed = io.feed("alert");
AdafruitIO_Feed *D1 = io.feed("D1");
AdafruitIO_Feed *D1first = io.feed("D1first");
AdafruitIO_Feed *device1 = io.feed("device1");

void buzz(uint8_t effect) {
  drv.setWaveform(0, effect);
  drv.setWaveform(1, 0);  // end of sequence
  drv.go();
}

// ---------- Inputs ----------
int incomingAlert = 0;   // last alert code from Adafruit IO, 0 = none
bool pendingSOS = false; // SOS requested while offline
bool announced = false;  // "SAFE online" sent

void onFeed(AdafruitIO_Data *data) {
  int v = data->toInt();
  if (findAlert(v)) incomingAlert = v;  // ignore echoes of our own writes
}

enum Gesture { NONE, TAP, TRIPLE_TAP, HOLD };
uint8_t taps = 0;
unsigned long lastTap = 0;
bool holdReported = false;

Gesture readButton() {
  button.update();
  if (button.fell()) {
    unsigned long now = millis();
    taps = (now - lastTap < TAP_GAP_MS) ? taps + 1 : 1;
    lastTap = now;
    if (taps == SOS_TAPS) { taps = 0; return TRIPLE_TAP; }
    return TAP;
  }
  if (button.read() == LOW) {
    if (!holdReported && button.currentDuration() >= HOLD_MS) { holdReported = true; return HOLD; }
  } else {
    holdReported = false;
  }
  return NONE;
}

// ---------- Connectivity ----------
const uint8_t NUM_SSIDS = sizeof(ssids) / sizeof(ssids[0]);
uint8_t ssidIndex = 0;

void joinWiFi(uint8_t i) {
  ssidIndex = i;
  WiFi.begin(ssids[i], passwords[i]);
  enter(WIFI_CONNECTING);
}

void connectIO() {
  io.connect();
  enter(IO_CONNECTING);
}

void onOnline() {
  if (!announced) { device1->save("SAFE online"); announced = true; }
  if (pendingSOS) {
    device1->save("SOS during connection");
    D1->save("D1SOS");
    pendingSOS = false;
  }
  enter(IDLE);
}

// ---------- Alert flow ----------
const AlertType *alert = nullptr;
bool resolved = false;

const char *msg(const char *what) {  // "1st SOS" -> "D1 1st SOS fire"
  static char buf[40];
  snprintf(buf, sizeof buf, "D1 %s %s", what, alert->tag);
  return buf;
}

void reportSOS(const char *what) {
  D1->save(msg(what));
  device1->save(msg(what));
}

void vibrate() {
  if ((long)(millis() - nextPulse) >= 0) {
    buzz(alert->effect);
    nextPulse += alert->pulseMs;
  }
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  if (!drv.begin(&Wire1)) Serial.println("DRV2605 not found");
  drv.selectLibrary(1);
  drv.setMode(DRV2605_MODE_INTTRIG);

  button.attach(BUTTON_PIN, INPUT_PULLUP);
  button.interval(DEBOUNCE_MS);

  alertFeed->onMessage(onFeed);
  D1->onMessage(onFeed);

  joinWiFi(0);
}

void loop() {
  Gesture g = readButton();
  bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (state >= IDLE && wifiUp) io.run();  // io.run() blocks if Wi-Fi is down

  // An SOS while offline is queued and sent once we are back online.
  if (state < IDLE && g == TRIPLE_TAP) { pendingSOS = true; buzz(FX_CONFIRM); }

  switch (state) {
    case WIFI_CONNECTING:
      if (wifiUp) connectIO();
      else if (inState() >= WIFI_TRY_MS) {
        if (ssidIndex + 1 < NUM_SSIDS) joinWiFi(ssidIndex + 1);
        else { buzz(FX_WIFI_FAIL); enter(WIFI_BACKOFF); }
      }
      break;

    case WIFI_BACKOFF:
      if (inState() >= WIFI_RETRY_MS) joinWiFi(0);
      break;

    case IO_CONNECTING:
      if (!wifiUp) joinWiFi(0);
      else if (io.status() >= AIO_CONNECTED) onOnline();
      break;

    case IDLE:
      if (!wifiUp) joinWiFi(0);
      else if (io.status() < AIO_CONNECTED) connectIO();
      else if (g == TRIPLE_TAP) {
        device1->save("SOS");
        D1->save("D1SOS");
        buzz(FX_CONFIRM);
      } else if (g == HOLD) {
        device1->save("SAFE RESCUED");
        D1->save("SAFE");
        buzz(FX_CONFIRM);
      } else if ((alert = findAlert(incomingAlert))) {
        incomingAlert = 0;
        device1->save(alert->startMsg);
        enter(STAGE1);
      }
      break;

    case STAGE1:  // first warning: tap to say "I noticed"
      vibrate();
      if (g == TAP || g == TRIPLE_TAP) {
        drv.stop();
        D1first->save(alert->ackCode);
        enter(STAGE_GAP);
      } else if (inState() >= ALERT_WINDOW_MS) {
        drv.stop();
        reportSOS("1st SOS");
        alertFeed->save(alert->doneCode);
        enter(IDLE);
      }
      break;

    case STAGE_GAP:
      if (inState() >= STAGE_GAP_MS) enter(STAGE2);
      break;

    case STAGE2:  // second warning: tap to say "I am safe"
      vibrate();
      if (g == TAP || g == TRIPLE_TAP) {
        drv.stop();
        D1->save(msg("safe from"));
        alertFeed->save(alert->doneCode);
        device1->save("SAFE");
        resolved = true;
        enter(SHOW_RESULT);
      } else if (inState() >= ALERT_WINDOW_MS) {
        drv.stop();
        reportSOS("2nd SOS");
        resolved = false;
        enter(SHOW_RESULT);
      }
      break;

    case SHOW_RESULT:
      if (inState() >= RESULT_SHOW_MS) {
        D1first->save("0");
        if (resolved) D1->save("0");  // an unanswered SOS stays visible on D1
        enter(IDLE);
      }
      break;
  }
}
