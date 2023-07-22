unsigned char STATUSOK[] = {0x90, 0x00}, Cmd[256], CmdSize;

uint8_t uidcf[20] = {
    0x20, 0x02, 0x05, 0x01, /* CORE_SET_CONFIG_CMD */
    0x00, 0x02, 0x00, 0x01  /* TOTAL_DURATION */
};

uint8_t uidlen = 0;

uint8_t data[] = {0x90, 0x00};
uint8_t requestCmd[] = {0x00, 0xB0, 0x00, 0x00, 0x0F};
bool emulateNFCFlag = false;
int attempts = 0;

const int EMULATE_NFCID_DELAY_MS = 1000;
unsigned long emulateNFCIDTimer = 0;