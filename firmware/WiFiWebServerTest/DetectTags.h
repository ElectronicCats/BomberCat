/**
 * Example detect tags and show their unique ID 
 * Authors: 
 *        Salvador Mendoza - @Netxing - salmg.net
 *        For Electronic Cats - electroniccats.com
 * 
 *  March 2020
 * 
 * This code is beerware; if you see me (or any other collaborator 
 * member) at the local, and you've found our code helpful, 
 * please buy us a round!
 * Distributed as-is; no warranty is given.
 */

#include "Electroniccats_PN7150.h"          
#define PN7150_IRQ   (11)
#define PN7150_VEN   (13)
#define PN7150_ADDR  (0x28)


WiFiServer server(80);

Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR);    // creates a global NFC device interface object, attached to pins 7 (IRQ) and 8 (VEN) and using the default I2C address 0x28
RfIntf_t RfInterface;                                              //Intarface to save data for multiple tags

uint8_t mode = 1;                                                  // modes: 1 = Reader/ Writer, 2 = Emulation

void ResetMode(){                                  //Reset the configuration mode after each reading
  WiFiClient client = server.available();
  client.println("Re-initializing...");
  nfc.ConfigMode(mode);                               
  nfc.StartDiscovery(mode);
}

void PrintBuf(const byte * data, const uint32_t numBytes){ //Print hex data buffer in format
  WiFiClient client = server.available();
  uint32_t szPos;
  for (szPos=0; szPos < numBytes; szPos++)
  {
    client.print(F("0x"));
    // Append leading 0 for small values
    if (data[szPos] <= 0xF)
      client.print(F("0"));
    client.print(data[szPos]&0xff, HEX);
    if ((numBytes > 1) && (szPos != numBytes - 1))
    {
      client.print(F(" "));
    }
  }
  client.println();
}
void displayCardInfo(RfIntf_t RfIntf){ //Funtion in charge to show the card/s in te field
  WiFiClient client = server.available();
  char tmp[16];
  while (1){
    switch(RfIntf.Protocol){  //Indetify card protocol
    case PROT_T1T:
    case PROT_T2T:
    case PROT_T3T:
    case PROT_ISODEP:
        client.print(" - POLL MODE: Remote activated tag type: ");
        client.println(RfIntf.Protocol);
        break;
    case PROT_ISO15693:
        client.println(" - POLL MODE: Remote ISO15693 card activated");
        break;
    case PROT_MIFARE:
        client.println(" - POLL MODE: Remote MIFARE card activated");
        break;
    default:
        client.println(" - POLL MODE: Undetermined target");
        return;
    }

    switch(RfIntf.ModeTech) { //Indetify card technology
      case (MODE_POLL | TECH_PASSIVE_NFCA):
          client.print("\tSENS_RES = ");
          sprintf(tmp, "0x%.2X",RfIntf.Info.NFC_APP.SensRes[0]);
          client.print(tmp); client.print(" ");
          sprintf(tmp, "0x%.2X",RfIntf.Info.NFC_APP.SensRes[1]);
          client.print(tmp); client.println(" ");
          
          client.print("\tNFCID = ");
          PrintBuf(RfIntf.Info.NFC_APP.NfcId, RfIntf.Info.NFC_APP.NfcIdLen);
          
          if(RfIntf.Info.NFC_APP.SelResLen != 0) {
              client.print("\tSEL_RES = ");
              sprintf(tmp, "0x%.2X",RfIntf.Info.NFC_APP.SelRes[0]);
              client.print(tmp); client.println(" ");
  
          }
      break;
  
      case (MODE_POLL | TECH_PASSIVE_NFCB):
          if(RfIntf.Info.NFC_BPP.SensResLen != 0) {
              client.print("\tSENS_RES = ");
              PrintBuf(RfIntf.Info.NFC_BPP.SensRes,RfIntf.Info.NFC_BPP.SensResLen);
          }
          break;
  
      case (MODE_POLL | TECH_PASSIVE_NFCF):
          client.print("\tBitrate = ");
          client.println((RfIntf.Info.NFC_FPP.BitRate == 1) ? "212" : "424");
          
          if(RfIntf.Info.NFC_FPP.SensResLen != 0) {
              client.print("\tSENS_RES = ");
              PrintBuf(RfIntf.Info.NFC_FPP.SensRes,RfIntf.Info.NFC_FPP.SensResLen);
          }
          break;
  
      case (MODE_POLL | TECH_PASSIVE_15693):
          client.print("\tID = ");
          PrintBuf(RfIntf.Info.NFC_VPP.ID,sizeof(RfIntf.Info.NFC_VPP.ID));
          
          client.print("\ntAFI = ");
          client.println(RfIntf.Info.NFC_VPP.AFI);
          
          client.print("\tDSFID = ");
          client.println(RfIntf.Info.NFC_VPP.DSFID,HEX);
      break;
  
      default:
          break;
    }
    if(RfIntf.MoreTags) { // It will try to identify more NFC cards if they are the same technology
      if(nfc.ReaderActivateNext(&RfIntf) == NFC_ERROR) break;
    }
    else break;
  }
}

void setupDetectTags(){
  //Serial.begin(9600);
  WiFiClient client = server.available();
  while(!Serial);
  client.println("Detect NFC tags with PN7150");
  
  client.println("Initializing...");                
  if (nfc.connectNCI()) { //Wake up the board
    client.println("Error while setting up the mode, check connections!");
    while (1);
  }
  
  if (nfc.ConfigureSettings()) {
    client.println("The Configure Settings is failed!");
    while (1);
  }
  
  if(nfc.ConfigMode(mode)){ //Set up the configuration mode
    client.println("The Configure Mode is failed!!");
    while (1);
  }
  nfc.StartDiscovery(mode); //NCI Discovery mode
  client.println("BomberCat, Yes Sir!");
  client.println("Waiting for an Card ...");
}
