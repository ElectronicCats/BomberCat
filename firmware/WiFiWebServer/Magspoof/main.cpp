#include <iostream>

using namespace std;

void compare();
void checkVoid();

char tracks[2][128];
const char* tracks2[] = {
    "%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?\0",  // Track 1
    ";123456781234567=112220100000000000000?\0"                              // Track 2
};

int main() {
  string track1 = "%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?\0";
  string track2 = ";123456781234567=112220100000000000000?";
  cout << "track1: " << track1 << endl;
  cout << "track2: " << track2 << endl;

  // Copy the tracks into the char arrays using strcpy
  strcpy(tracks[0], track1.c_str());
  strcpy(tracks[1], track2.c_str());

  cout << "tracks[0]: " << tracks[0] << endl;
  cout << "tracks[1]: " << tracks[1] << endl;
  cout << '\0' << endl;
  
  checkVoid();
  compare();

  strcpy(tracks[0], "hello");
  strcpy(tracks[1], "world");

  checkVoid();

  return 0;
}

// Compare tracks[0] with tracks2[0]
void compare() {
  if (strcmp(tracks[0], tracks2[0]) == 0) {
    cout << "tracks[0] == tracks2[0]" << endl;
  } else {
    cout << "tracks[0] != tracks2[0]" << endl;
  }
}

// Check if tracks[0] and tracks2[0] have \0 at the end
void checkVoid() {
  for (int i = 0; tracks[0][i] != '\0'; i++) {
    cout << tracks[0][i];
  }
  cout << endl;
}