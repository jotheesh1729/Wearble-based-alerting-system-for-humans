// Minimal fakes of Arduino + the libraries the sketch uses, for host tests.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>

// Clock and pins
inline unsigned long fakeNow = 0;
inline unsigned long millis() { return fakeNow; }
const uint8_t A1 = 1, INPUT_PULLUP = 2, LOW = 0, HIGH = 1;
inline int fakePin = HIGH;  // button level (pull-up: pressed = LOW)

struct FakeSerial {
  void begin(long) {}
  void println(const char *) {}
  void printf(const char *, ...) {}
} inline Serial;

struct TwoWire {} inline Wire1;

// Haptic driver: remembers the last effect it played
const int DRV2605_MODE_INTTRIG = 0;
inline int fakeLastEffect = -1, fakeBuzzes = 0;
struct Adafruit_DRV2605 {
  int slot0 = 0;
  bool begin(TwoWire *) { return true; }
  void selectLibrary(int) {}
  void setMode(int) {}
  void setWaveform(int slot, int e) { if (slot == 0) slot0 = e; }
  void go() { fakeLastEffect = slot0; fakeBuzzes++; }
  void stop() {}
};

// Debouncer driven by fakePin
struct Bounce {
  int cur = HIGH, prev = HIGH;
  unsigned long changed = 0;
  void attach(int, int) {}
  void interval(int) {}
  void update() { prev = cur; cur = fakePin; if (cur != prev) changed = millis(); }
  bool fell() { return prev == HIGH && cur == LOW; }
  int read() { return cur; }
  unsigned long currentDuration() { return millis() - changed; }
};

// Wi-Fi
const int WL_CONNECTED = 3, WL_DISCONNECTED = 6;
inline bool fakeWifiUp = false;
inline std::string fakeLastSsid;
struct FakeWiFi {
  void begin(const char *ssid, const char *) { fakeLastSsid = ssid; }
  int status() { return fakeWifiUp ? WL_CONNECTED : WL_DISCONNECTED; }
} inline WiFi;

// Adafruit IO: every save() is logged as "feed=value"
inline std::vector<std::string> fakeSaves;
inline bool fakeIoUp = false;
const int AIO_DISCONNECTED = 2, AIO_CONNECTED = 20;
struct AdafruitIO_Data {
  int v;
  int toInt() { return v; }
};
struct AdafruitIO_Feed {
  std::string name;
  void (*cb)(AdafruitIO_Data *) = nullptr;
  void save(const char *v) { fakeSaves.push_back(name + "=" + v); }
  void onMessage(void (*f)(AdafruitIO_Data *)) { cb = f; }
  void deliver(int v) { AdafruitIO_Data d{v}; if (cb) cb(&d); }  // test-only
};
struct AdafruitIO_WiFi {
  std::vector<AdafruitIO_Feed *> feeds;
  AdafruitIO_WiFi(const char *, const char *, const char *, const char *) {}
  AdafruitIO_Feed *feed(const char *n) { feeds.push_back(new AdafruitIO_Feed{n}); return feeds.back(); }
  void connect() {}
  int status() { return fakeIoUp ? AIO_CONNECTED : AIO_DISCONNECTED; }
  void run() {}
};
