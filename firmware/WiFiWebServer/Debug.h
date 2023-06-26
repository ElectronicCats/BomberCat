#include "Arduino.h"

class Debug {
 private:
  bool enabled;

 public:
  void setEnabled(bool enabled);
  bool isEnabled();
  void waitForSerialConnection();
  void write(uint8_t message);
  void write(const uint8_t *buffer, size_t size);
  void print(String message);
  void println(String message);
  void print(int message);
  void println(int message);
  void print(long message);
  void println(long message);
  void print(double message);
  void println(double message);
  void print(float message);
  void println(float message);
  void print(char message);
  void println(char message);
  void print(bool message);
  void println(bool message);
  void print(const char message[]);
  void println(const char message[]);
  void print(arduino::IPAddress message);
  void println(arduino::IPAddress message);
  void print(String message, String message2);
  void println(String message, String message2);
  void print(String message, String message2, String message3);
  void println(String message, String message2, String message3);
  void print(String message, String message2, String message3, String message4);
  void println(String message, String message2, String message3, String message4);
};