#include "Electroniccats_PN7150.h"
#define PN7150_IRQ (11)
#define PN7150_VEN (13)
#define PN7150_ADDR (0x28)
#define DETECT_TAGS_DELAY_MS (200)
#define READ_ATTEMPTS (2)

Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR, PN7150); // creates a global NFC device interface object, attached to pins 7 (IRQ) and 8 (VEN) and using the default I2C address 0x28,specify PN7150 or PN7160 in constructor

String pollMode, nfcID, sensRes, selRes, bitRate, afi, dsfid;
bool runDetectTags = false;
bool clearNFCValuesFlag = false;
uint8_t nfcExecutionCounter = 0;
bool nfcDiscoverySuccess = false;