/************************************************************
  MagSpoof version for Bomber Cat
  by Salvador Mendoza (salmg.net)
  Electronic Cats (https://electroniccats.com/)

  This example demonstrates how to use Bomber Cat by Electronic Cats
  https://github.com/ElectronicCats/BomberCat

  Development environment specifics:
  IDE: Arduino 1.8.9
  Hardware Platform:
  Bomber Cat
  - RP2040

  Electronic Cats invests time and resources providing this open source code,
  please support Electronic Cats and open-source hardware by purchasing
  products from Electronic Cats!

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
*/

#define L1 (LED_BUILTIN)   // LED1
#define PIN_A (6)          // MagSpoof-1
#define PIN_B (7)          // MagSpoofZF
#define NPIN (5)           // Button
#define CLOCK_US (500)     // 500us clock, it simulates the speed of the magnetic card swiping
#define BETWEEN_ZERO (53)  // 53 zeros between track1 & 2
#define TRACKS (2)
#define DEBUGCAT

char tracks[2][128];  // 2 tracks, 128 chars each (max)

char revTrack[41];  // 40 chars + null

const int sublen[] = {32, 48, 48};

const int bitlen[] = {7, 5, 5};

unsigned int curTrack = 0;
int dir;

void setupTracks() {
  String track1 = "%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?";
  String track2 = ";123456781234567=112220100000000000000?";

  // Copy the tracks into the char arrays using strcpy
  strcpy(tracks[0], track1.c_str());
  strcpy(tracks[1], track2.c_str());

  Serial.println("Default tracks:");
  Serial.print("Track 1: ");
  Serial.println(tracks[0]);
  Serial.print("Track 2: ");
  Serial.println(tracks[1]);
}

void updateTracks(String track1, String track2) {
  strcpy(tracks[0], track1.c_str());
  strcpy(tracks[1], track2.c_str());

  Serial.println("Updated tracks:");
  Serial.print("Track 1: ");
  Serial.println(tracks[0]);
  Serial.print("Track 2: ");
  Serial.println(tracks[1]);
}

void blink(int pin, int msdelay, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}

// send a single bit out
void playBit(int sendBit) {
  dir ^= 1;
  digitalWrite(PIN_A, dir);
  digitalWrite(PIN_B, !dir);
  delayMicroseconds(CLOCK_US);

  if (sendBit) {
    dir ^= 1;
    digitalWrite(PIN_A, dir);
    digitalWrite(PIN_B, !dir);
  }
  delayMicroseconds(CLOCK_US);
}

// when reversing
void reverseTrack(int track) {
  int i = 0;
  track--;  // index 0
  dir = 0;

  while (revTrack[i++] != '\0')
    ;
  i--;
  while (i--)
    for (int j = bitlen[track] - 1; j >= 0; j--)
      playBit((revTrack[i] >> j) & 1);
}

// plays out a full track, calculating CRCs and LRC
void playTrack(int track) {
  int tmp, crc, lrc = 0;
  dir = 0;
  track--;  // index 0
  // enable H-bridge and LED
  // digitalWrite(ENABLE_PIN, HIGH);

  // First put out a bunch of leading zeros.
  for (int i = 0; i < 25; i++)
    playBit(0);

  for (int i = 0; tracks[track][i] != '\0'; i++) {
    crc = 1;
    tmp = tracks[track][i] - sublen[track];

    for (int j = 0; j < bitlen[track] - 1; j++) {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      playBit(tmp & 1);
      tmp >>= 1;
    }
    playBit(crc);
  }

  // finish calculating and send last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track] - 1; j++) {
    crc ^= tmp & 1;
    playBit(tmp & 1);
    tmp >>= 1;
  }
  playBit(crc);

  // if track 1, play 2nd track in reverse (like swiping back?)
  if (track == 0) {
    // if track 1, also play track 2 in reverse
    // zeros in between
    for (int i = 0; i < BETWEEN_ZERO; i++)
      playBit(0);

    // send second track in reverse
    reverseTrack(2);
  }

  // finish with 0's
  for (int i = 0; i < 5 * 5; i++)
    playBit(0);

  digitalWrite(PIN_A, LOW);
  digitalWrite(PIN_B, LOW);
}

// stores track for reverse usage later
void storeRevTrack(int track) {
  int i, tmp, crc, lrc = 0;
  track--;  // index 0
  dir = 0;

  for (i = 0; tracks[track][i] != '\0'; i++) {
    crc = 1;
    tmp = tracks[track][i] - sublen[track];

    for (int j = 0; j < bitlen[track] - 1; j++) {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      tmp & 1 ? (revTrack[i] |= 1 << j) : (revTrack[i] &= ~(1 << j));
      tmp >>= 1;
    }
    crc ? (revTrack[i] |= 1 << 4) : (revTrack[i] &= ~(1 << 4));
  }

  // finish calculating and send last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track] - 1; j++) {
    crc ^= tmp & 1;
    tmp & 1 ? (revTrack[i] |= 1 << j) : (revTrack[i] &= ~(1 << j));
    tmp >>= 1;
  }
  crc ? (revTrack[i] |= 1 << 4) : (revTrack[i] &= ~(1 << 4));

  i++;
  revTrack[i] = '\0';
}

void magspoof() {
  if (digitalRead(NPIN) == 0) {
    Serial.println("Activating MagSpoof...");
    playTrack(1 + (curTrack++ % 2));
    blink(L1, 150, 3);
    delay(400);
  }
}

void setup() {
  pinMode(PIN_A, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(L1, OUTPUT);
  pinMode(NPIN, INPUT_PULLUP);

  Serial.begin(9600);
  while (!Serial)
    ;

  // blink to show we started up
  blink(L1, 200, 2);
  setupTracks();

  String track1 = "%B4784556940589010^HOGAN/PAUL      ^08043210000000725000000?";
  String track2 = ";4784556940589010=08043210000072500000?";
  // Uncomment to modify tracks
  // updateTracks(track1, track2);

  Serial.println("Press the MagSpoof button");
}
void loop() {
  magspoof();
}
