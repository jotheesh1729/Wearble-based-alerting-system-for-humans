#include <Adafruit_DRV2605.h> // Include the library for the DRV2605 haptic motor driver
#include <AdafruitIO_WiFi.h> // Include the library for connecting to Adafruit IO over WiFi
#include <Bounce2.h> // Include the library for debouncing button presses
#include <WiFi.h> // Include the library for WiFi connectivity
#include "config.h" // SET UP WIFI AND ADAFRUIT IO CREDENTIALS HERE

// Define the DRV2605 and Bounce objects
Adafruit_DRV2605 drv; // Haptic driver object
Adafruit_DRV2605 drv1; // Haptic driver object 1
Adafruit_DRV2605 drv2; // Haptic driver object 2

Bounce button; // Button debouncer object

int buttonState = 0; // Current state of the button
int toggleValue = 0; // Current value of the toggle
int pressCount = 0; // Number of times the button has been pressed
unsigned long lastPressTime = 0; // Time when the button was last pressed
bool saveSOSAfterConnect = false; // Boolean to determine if SOS should be saved after connecting to WiFi
bool sosTriggered = false; // Boolean to indicate if the SOS has been triggered
bool safePrinted = false; // Flag to track if "Safe" has been printed
bool buttonPressed = false; // Flag to track if the button is currently pressed
unsigned long buttonPressedStartTime = 0; // Time when the button was pressed

AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, "", ""); // Adafruit IO object for connecting over WiFi
AdafruitIO_Feed *alert = io.feed("alert"); // Adafruit IO feed object for alerts
AdafruitIO_Feed *D1 = io.feed("D1"); // Adafruit IO feed object for device 1
AdafruitIO_Feed *D1first = io.feed("D1first"); // Adafruit IO feed object for the first data point of device 1
AdafruitIO_Feed *device1 = io.feed("device1"); // Adafruit IO feed object for the status of device 1

void setup() {

  drv.begin(&Wire1); // Begin communication with DRV2605 haptic driver
  drv.selectLibrary(1); // Select the library for the DRV2605
  drv.setMode(DRV2605_MODE_INTTRIG); // Set the mode of the DRV2605 to internal trigger mode

  drv1.begin(&Wire1); // Begin communication with drv1 (haptic driver 1)
  drv1.selectLibrary(1); // Select the library for drv1
  drv1.setMode(DRV2605_MODE_INTTRIG); // Set the mode of drv1 to internal trigger mode

  drv2.begin(&Wire1); // Begin communication with drv2 (haptic driver 2)
  drv2.selectLibrary(1); // Select the library for drv2
  drv2.setMode(DRV2605_MODE_INTTRIG); // Set the mode of drv2 to internal trigger mode

  button.attach(A1, INPUT_PULLUP); // Attach the button to pin A1 with internal pull-up resistor
  button.interval(3); // Set the debounce interval for the button

  connectToWiFi(); // Function to connect to WiFi network

  io.connect(); // Connect to Adafruit IO
  alert->onMessage(feedCallback); // Set up callback function for 'alert' feed
  D1->onMessage(feedCallback); // Set up callback function for 'D1' feed

  while (io.status() < AIO_CONNECTED) { // Wait until connected to Adafruit IO
    delay(500);
  }

  device1->save("SAFE online");
  sosTriggered = true; // Set sosTriggered flag to indicate that SOS has been triggered
}

void loop() {
  button.update(); // Update the button state

  if (button.fell()) { // Check if the button was pressed
    unsigned long currentPressTime = millis(); // Get the current time
    if (currentPressTime - lastPressTime < 1000) { // Check if the button was pressed within 1 second of the last press
      pressCount++; // Increment the press count
    } else {
      pressCount = 1; // Reset the press count to 1
    }

    lastPressTime = currentPressTime; // Update the last press time

    if (pressCount == 3) { // Check if the button was pressed three times consecutively

      device1->save("SOS"); // Save SOS message to device1
      sosTriggered = true; // Set the sosTriggered flag to true
      D1->save("D1SOS"); // Save SOS message to D1
      delay(2000);
      drv2.setWaveform(0, 37); // Set the waveform for drv2
      drv2.go();
      pressCount = 0; // Reset the press count
    }
  }

  if (WiFi.status() != WL_CONNECTED) { // Check if WiFi connection is lost
    connectToWiFi(); // Reconnect to WiFi
  }

  // Check Adafruit IO status
  if (io.status() < AIO_CONNECTED) { // Check if Adafruit IO connection is lost
    if (saveSOSAfterConnect) { // Check if SOS should be saved after connecting
      device1->save("SOS during connection"); // Save SOS message to device1
      delay(2000);
      drv2.setWaveform(0, 37); // Set the waveform for drv2
      drv2.go();
      saveSOSAfterConnect = false; // Reset the saveSOSAfterConnect flag
    }
    io.connect(); // Reconnect to Adafruit IO

    // Wait for it to connect.
    while (io.status() < AIO_CONNECTED) {
      delay(500);
    }

    if (io.status() == AIO_CONNECTED && sosTriggered) { // Check if connected and SOS was triggered
      device1->save("SOS during connection"); // Save SOS message to device1
      D1->save("D1SOS"); // Save SOS message to D1
      delay(2000);
      drv2.setWaveform(0, 37); // Set the waveform for drv2
      drv2.go();
      sosTriggered = false; // Reset the sosTriggered flag
    }
  }

   // Check if button is released
  if (button.fell()) { // Check if the button was pressed
    buttonPressed = true; // Set the buttonPressed flag to true
    buttonPressedStartTime = millis(); // Record the button pressed start time
  }

  if (button.rose()) { // Check if the button was released
    buttonPressed = false; // Set the buttonPressed flag to false
    if (safePrinted) {
      safePrinted = false; // Reset the flag when the button is released
    }
  }

  if (buttonPressed && !safePrinted && (millis() - buttonPressedStartTime) >= 5000) {
    device1->save("SAFE RESCUED");
    D1->save("SAFE");
    drv2.setWaveform(0, 37); // Set the waveform for drv2
    drv2.go(); // Print "Safe" to the Serial monitor
    safePrinted = true; // Set the flag to indicate that "Safe" has been printed
  }

  io.run();

  // If the toggle value is 1, activate the motor and wait for the button press
  if (toggleValue == 1 || toggleValue == 4) {
    unsigned long startTime = millis(); // Record the start time
    device1->save("SOS sent fire");
    while (toggleValue == 1 || toggleValue == 4) {
      // Wait for the button to be pressed and released, or for 10 seconds to elapse
      button.update();
      while (!button.fell() && (millis() - startTime) < 5000) {
        // run drv for 10sec, just wait for the button or the timeout
        drv.setWaveform(0, 58); // Continuous Transition click with between delay 200 ms
        drv.go();
        delay(200);
        button.update();
      }
      // Check if the button was pressed, or if the timeout occurred
      if (button.fell()) {
        delay(30); // Debounce the button
        drv.stop();
        D1first->save("1");
        delay(5000);
        startTime = millis(); // Reset the start time for the second activation

        while (toggleValue == 1 || toggleValue == 4) {
          // Wait for the button to be pressed and released, or for 10 seconds to elapse
          button.update();
          while (!button.fell() && (millis() - startTime) < 5000) {
            // run drv1 for 10sec, just wait for the button or the timeout
            drv.setWaveform(0, 58); // Continuous Transition click with between delay 200 ms
            drv1.go();
            delay(200);
            button.update();
          }
          // Check if the button was pressed, or if the timeout occurred
          if (button.fell()) {
            delay(30); // Debounce the button
            drv1.stop();
            D1->save("D1 safe from fire");
            alert->save("11");
            device1->save("SAFE");
            delay(3000);
            D1first->save("0");
            D1->save("0");
            toggleValue = 44;
            break;
          } else {
            // Timeout occurred
            drv1.stop();
            D1->save("D1 2nd SOS fire");
            device1->save("D1 2nd SOS fire");
            delay(3000);
            D1first->save("0");
            toggleValue = 44;
            break;
          }
          break;
        }
        break;
      } else {
        // Timeout occurred
        drv.stop();
        D1->save("D1 1st SOS fire");
        device1->save("D1 1st SOS fire");
        alert->save("11");
        toggleValue = 44;
        break;
      }
      break;
    }
  } else if (toggleValue == 2 || toggleValue == 6) {
    unsigned long startTime = millis(); // Record the start time
    device1->save("SOS sent zombie");
    while (toggleValue == 2 || toggleValue == 6) {
      // Wait for the button to be pressed and released, or for 10 seconds to elapse
      button.update();
      while (!button.fell() && (millis() - startTime) < 5000) {
        // run drv for 10sec, just wait for the button or the timeout
        drv.setWaveform(0, 1);  // continuous Strong click with delay 100 ms
        drv.go();
        delay(100);
        button.update();
      }
      // Check if the button was pressed, or if the timeout occurred
      if (button.fell()) {
        delay(30); // Debounce the button
        drv.stop();
        D1first->save("2");
        delay(5000);
        startTime = millis(); // Reset the start time for the second activation

        while (toggleValue == 2 || toggleValue == 6) {
          // Wait for the button to be pressed and released, or for 10 seconds to elapse
          button.update();
          while (!button.fell() && (millis() - startTime) < 5000) {
            // run drv1 for 10sec, just wait for the button or the timeout
            drv.setWaveform(0, 1);  // continuous Strong click with delay 100 ms
            drv1.go();
            delay(100);
            button.update();
          }

          // Check if the button was pressed, or if the timeout occurred
          if (button.fell()) {
            delay(30); // Debounce the button
            drv1.stop();
            D1->save("D1 safe from zom");
            alert->save("22");
            device1->save("SAFE");
            delay(3000);
            D1first->save("0");
            D1->save("0");
            toggleValue = 66;
            break;
          } else {
            // Timeout occurred
            drv1.stop();
            D1->save("D1 2nd SOS zom");
            device1->save("D1 2nd SOS zom");
            delay(3000);
            D1first->save("0");
            toggleValue = 66;
            break;
          }
          break;
        }
        break;
      } else {
        // Timeout occurred
        drv.stop();
        D1->save("D1 1st SOS zom");
        device1->save("D1 1st SOS fire");
        alert->save("22");
        toggleValue = 66;
        break;
      }
      break;
    }
  } else if (toggleValue == 3 || toggleValue == 8) {
    unsigned long startTime = millis(); // Record the start time
    device1->save("SOS sent both");
    while (toggleValue == 3 || toggleValue == 8) {
      // Wait for the button to be pressed and released, or for 10 seconds to elapse
      button.update();
      while (!button.fell() && (millis() - startTime) < 5000) {
        // run drv for 10sec, just wait for the button or the timeout
        drv.setWaveform(0, 70); // Transition ramp down long smooth with delay 150 ms
        drv.go();
        delay(150);
        button.update();
      }

      // Check if the button was pressed, or if the timeout occurred
      if (button.fell()) {
        delay(30); // Debounce the button
        drv.stop();
        D1first->save("3");
        delay(5000);
        startTime = millis(); // Reset the start time for the second activation

        while (toggleValue == 3 || toggleValue == 8) {

          // Wait for the button to be pressed and released, or for 10 seconds to elapse
          button.update();
          while (!button.fell() && (millis() - startTime) < 5000) {
            // run drv1 for 10sec, just wait for the button or the timeout
            drv.setWaveform(0, 70); // Transition ramp down long smooth with delay 150 ms
            drv1.go();
            delay(150);
            button.update();
          }

          // Check if the button was pressed, or if the timeout occurred
          if (button.fell()) {
            delay(30); // Debounce the button
            drv1.stop();
            D1->save("D1 safe from both");
            device1->save("SAFE");
            delay(3000);
            D1first->save("0");
            D1->save("0");
            toggleValue = 88;
            break;
          } else {
            // Timeout occurred
            drv1.stop();
            D1->save("D1 2nd SOS both");
            device1->save("D1 2nd SOS both");
            delay(3000);
            D1first->save("0");
            toggleValue = 88;
            break;
          }
          break;
        }
        break;
      } else {
        // Timeout occurred
        drv.stop();
        D1->save("D1 1st SOS both");
        device1->save("D1 1st SOS both");
        alert->save("33");
        toggleValue = 88;
        break;
      }
      break;
    }
  }
}

// Callback function for receiving messages from the Adafruit IO feed
void feedCallback(AdafruitIO_Data *data) {
  toggleValue = data->toInt(); // Convert received data to integer and assign it to toggleValue
}

void connectToWiFi() {
  for (int i = 0; i < sizeof(ssids) / sizeof(ssids[0]); i++) {

    WiFi.begin(ssids[i], passwords[i]); // Connect to WiFi using the SSID and password

    unsigned long startTime = millis(); // Record the start time
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 30000) {
      // Wait for WiFi connection or timeout (30 seconds)
      delay(500); // Wait for 500 milliseconds
    }

    if (WiFi.status() == WL_CONNECTED) { // Check if WiFi connection is successful
      return; // Exit the function
    }
  }


  drv2.setWaveform(0, 10); // Set the waveform for drv2
  drv2.go(); // Activate drv2
  delay(600000); // Wait for 10 minutes before attempting to connect again
  connectToWiFi(); // Retry connecting to WiFi
}
