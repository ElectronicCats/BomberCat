#include <iostream>

using namespace std;

char tracks[2][128];

int main() {
  string track1 = "%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?";
  string track2 = ";123456781234567=112220100000000000000?";
  string arg = track1 + track2;
  // const char* payload = "%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?;123456781234567=112220100000000000000?";
  cout << "track1: " << track1 << endl;  // Output: %B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?
  cout << "track2: " << track2 << endl;  // Output: ;123456781234567=112220100000000000000?
  // cout << "payload: " << payload << endl;

  // Copy the tracks into the char arrays using strcpy
  strcpy(tracks[0], track1.c_str());
  strcpy(tracks[1], track2.c_str());

  cout << "tracks[0]: " << tracks[0] << endl;  // Output: %B;123456781234567=112220100000000000000? | Expected output: %B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?
  cout << "tracks[1]: " << tracks[1] << endl;  // Output: ;123456781234567=112220100000000000000? | Expected output: ;123456781234567=112220100000000000000?

  return 0;
}