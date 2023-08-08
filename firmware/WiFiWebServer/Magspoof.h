#define L1 (LED_BUILTIN)  // LED1
#define PIN_A (6)  // MagSpoof-1
#define PIN_B (7)  // MagSpoof
#define NPIN (5)  // Button
#define CLOCK_US (500)
#define BETWEEN_ZERO (53)  // 53 zeros between track1 & 2
#define TRACKS (2)
#define DEBUGCAT

bool runMagspoof = false;
char tracks[2][128];

char revTrack[41];

const int sublen[] = {32, 48, 48};
const int bitlen[] = {7, 5, 5};

unsigned int curTrack = 0;
int dir;