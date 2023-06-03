#define L1 (LED_BUILTIN)  // LED1
#define PIN_A (6)  // MagSpoof-1
#define PIN_B (7)  // MagSpoof
#define NPIN (5)  // Button
#define CLOCK_US (500)
#define BETWEEN_ZERO (53)  // 53 zeros between track1 & 2
#define TRACKS (2)
#define DEBUGCAT

// consts get stored in flash as we don't adjust them
// const char* tracks[] = {
//     "%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?\0",  // Track 1
//     ";123456781234567=112220100000000000000?\0"                              // Track 2
// };

char tracks[128][2];

char revTrack[41];

const int sublen[] = {32, 48, 48};
const int bitlen[] = {7, 5, 5};

unsigned int curTrack = 0;
int dir;