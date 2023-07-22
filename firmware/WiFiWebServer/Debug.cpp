#include "Debug.h"

void Debug::setEnabled(bool enabled) {
  this->enabled = enabled;
}

bool Debug::isEnabled() {
  return this->enabled;
}

void Debug::waitForSerialConnection() {
  if (!this->enabled) {
    return;
  }
  while (!Serial) {  // Wait for the serial connection to be established
    delay(1);
  }
}

void Debug::write(uint8_t message) {
  if (this->enabled) {
    Serial.write(message);
  }
}

void Debug::write(const uint8_t *buffer, size_t size) {
  if (this->enabled) {
    Serial.write(buffer, size);
  }
}

void Debug::print(String message) {
  if (this->enabled) {
    Serial.print(message);
  }
}

void Debug::println(String message) {
  if (this->enabled) {
    Serial.println(message);
  }
}

void Debug::print(int message) {
  if (this->enabled) {
    Serial.print(message);
  }
}

void Debug::println(int message) {
  if (this->enabled) {
    Serial.println(message);
  }
}

void Debug::print(long message) {
  if (this->enabled) {
    Serial.print(message);
  }
}

void Debug::println(long message) {
  if (this->enabled) {
    Serial.println(message);
  }
}

void Debug::print(double message) {
  if (this->enabled) {
    Serial.print(message);
  }
}

void Debug::println(double message) {
  if (this->enabled) {
    Serial.println(message);
  }
}

void Debug::print(float message) {
  if (this->enabled) {
    Serial.print(message);
  }
}

void Debug::println(float message) {
  if (this->enabled) {
    Serial.println(message);
  }
}

void Debug::print(char message) {
  if (this->enabled) {
    Serial.print(message);
  }
}

void Debug::println(char message) {
  if (this->enabled) {
    Serial.println(message);
  }
}

void Debug::print(bool message) {
  if (this->enabled) {
    Serial.print(message ? "true" : "false");
  }
}

void Debug::println(bool message) {
  if (this->enabled) {
    Serial.println(message ? "true" : "false");
  }
}

void Debug::print(const char message[]) {
  if (this->enabled) {
    Serial.print(message);
  }
}

void Debug::println(const char message[]) {
  if (this->enabled) {
    Serial.println(message);
  }
}

void Debug::print(arduino::IPAddress message) {
  if (this->enabled) {
    Serial.print(message);
  }
}

void Debug::println(arduino::IPAddress message) {
  if (this->enabled) {
    Serial.println(message);
  }
}

void Debug::print(String message, String message2) {
  if (this->enabled) {
    Serial.print(message);
    Serial.print(message2);
  }
}

void Debug::println(String message, String message2) {
  if (this->enabled) {
    Serial.print(message);
    Serial.println(message2);
  }
}

void Debug::print(String message, String message2, String message3) {
  if (this->enabled) {
    Serial.print(message);
    Serial.print(message2);
    Serial.print(message3);
  }
}

void Debug::println(String message, String message2, String message3) {
  if (this->enabled) {
    Serial.print(message);
    Serial.print(message2);
    Serial.println(message3);
  }
}

void Debug::print(String message, String message2, String message3, String message4) {
  if (this->enabled) {
    Serial.print(message);
    Serial.print(message2);
    Serial.print(message3);
    Serial.print(message4);
  }
}

void Debug::println(String message, String message2, String message3, String message4) {
  if (this->enabled) {
    Serial.print(message);
    Serial.print(message2);
    Serial.print(message3);
    Serial.println(message4);
  }
}
