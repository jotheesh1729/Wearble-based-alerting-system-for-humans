// Host simulation of the sketch, no board needed:
//   c++ -std=c++17 -Itest/fakes test/test_alert.cpp -o /tmp/t && /tmp/t
#include "Arduino.h"
#include "../wearable_alert/wearable_alert.ino"
#include <cassert>
#include <iostream>

void run(unsigned long ms) {  // advance time, calling loop() every 10 ms
  for (unsigned long t = 0; t < ms; t += 10) { loop(); fakeNow += 10; }
}
void tap() { fakePin = LOW; run(50); fakePin = HIGH; run(50); }
bool saved(const std::string &s) {
  for (auto &x : fakeSaves) if (x == s) return true;
  return false;
}
void expect(const std::vector<std::string> &want) {
  if (fakeSaves != want) {
    std::cerr << "got:\n";
    for (auto &x : fakeSaves) std::cerr << "  " << x << "\n";
    assert(false);
  }
  fakeSaves.clear();
}

int main() {
  // Boot: second SSID works.
  setup();
  run(WIFI_TRY_MS + 100);
  assert(fakeLastSsid == "ssid2");
  fakeWifiUp = true; run(100);
  assert(state == IO_CONNECTING);
  fakeIoUp = true; run(100);
  assert(state == IDLE);
  expect({"device1=SAFE online"});

  // Fire alert, acknowledged twice -> safe, then dashboard reset.
  D1->deliver(1); run(100);
  assert(state == STAGE1 && fakeLastEffect == 58);
  tap();
  assert(state == STAGE_GAP);
  run(STAGE_GAP_MS);
  assert(state == STAGE2);
  tap();
  run(RESULT_SHOW_MS);
  assert(state == IDLE);
  expect({"device1=SOS sent fire", "D1first=1", "D1=D1 safe from fire", "alert=11",
          "device1=SAFE", "D1first=0", "D1=0"});

  // Zombie alert ignored -> 1st SOS.
  alertFeed->deliver(6); run(ALERT_WINDOW_MS + 200);
  assert(state == IDLE);
  expect({"device1=SOS sent zombie", "D1=D1 1st SOS zom", "device1=D1 1st SOS zom", "alert=22"});

  // Both: stage 1 acked, stage 2 ignored -> 2nd SOS, D1 left showing it.
  alertFeed->deliver(3); run(100); tap();
  run(STAGE_GAP_MS + ALERT_WINDOW_MS + RESULT_SHOW_MS);
  assert(state == IDLE);
  expect({"device1=SOS sent both", "D1first=3", "D1=D1 2nd SOS both",
          "device1=D1 2nd SOS both", "D1first=0"});

  // Echoes of our own writes don't start alerts.
  alertFeed->deliver(11); D1->deliver(0); run(100);
  assert(state == IDLE);

  // Triple tap -> SOS. Two taps -> nothing.
  tap(); tap(); run(TAP_GAP_MS + 100);
  expect({});
  tap(); tap(); tap();
  expect({"device1=SOS", "D1=D1SOS"});
  assert(fakeLastEffect == FX_CONFIRM);
  run(TAP_GAP_MS);

  // Hold 5 s -> SAFE RESCUED once.
  fakePin = LOW; run(HOLD_MS + 2000); fakePin = HIGH; run(50);
  expect({"device1=SAFE RESCUED", "D1=SAFE"});

  // SOS while Wi-Fi is down is queued and sent after reconnect.
  fakeWifiUp = false; run(100);
  assert(state == WIFI_CONNECTING);
  tap(); tap(); tap();
  expect({});
  fakeWifiUp = true; run(200);
  assert(state == IDLE);
  expect({"device1=SOS during connection", "D1=D1SOS"});

  // All SSIDs fail -> warning buzz, wait 10 min, retry from the first.
  fakeWifiUp = false; run(100);
  run(WIFI_TRY_MS * NUM_SSIDS);
  assert(state == WIFI_BACKOFF && fakeLastEffect == FX_WIFI_FAIL);
  run(WIFI_RETRY_MS);
  assert(state == WIFI_CONNECTING && fakeLastSsid == "ssid1");

  std::cout << "all tests passed\n";
}
