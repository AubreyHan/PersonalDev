#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[SYSTEM] Clean slate initialized. Ready for new development.");
}

void loop() {
    delay(1000);
}
