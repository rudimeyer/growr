#include "NtpClient.h"

UDP Udp;
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

unsigned long NtpTime::getTime(const char* ntpServer, const unsigned int ntpPort) {
  Udp.begin(ntpPort);

  // Flush any existing packages
  if(Udp.parsePacket()) {
    Udp.flush();
  }
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0; // Stratum, or type of clock
  packetBuffer[2] = 6; // Polling Interval
  packetBuffer[3] = 0xEC; // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(ntpServer, ntpPort);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();

  unsigned long epoch = 0;

  // wait to see if a reply is available
  delay(500);
  Serial.println( Udp.parsePacket() );
  for (int i = 0; i < 10; i++) {
    if(i > 0) {
      delay(500);
    }
    if(Udp.parsePacket()) {
      Serial.println("packet received");
      // We've received a packet, read the data from it
      Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

      //the timestamp starts at byte 40 of the received packet and is four bytes long.
      // Extract the four bytes and combine the four bytes (two words) into a long integer
      // this is NTP time (seconds since Jan 1 1900):
      unsigned long secsSince1900 = (packetBuffer[40] << 8 | packetBuffer[41]) << 16 | (packetBuffer[42] << 8 | packetBuffer[43]);
      Serial.print("Seconds since Jan 1 1900 = " );
      Serial.println(secsSince1900);

      // now convert NTP time into everyday time:
      Serial.print("Unix time:");
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;
      // subtract seventy years:
      epoch = secsSince1900 - seventyYears;
      
      // Adjust for the delays since request packet was send
      epoch = epoch - (long)(((i * 500) + 500) / 1000);
            
      // print Unix time:
      Serial.print("Epoch: ");
      Serial.println(epoch);
      break;
    }
  }
  
  Udp.stop();
  return epoch;
}

bool NtpTime::setTime(const char* ntpServer, const unsigned int ntpPort) {
  int retry = 10;
  int receivedTime;
  for (int i = 0; i < retry; i++) {
    receivedTime = getTime(ntpServer, ntpPort);
    if(receivedTime > 0) {
      Time.setTime(receivedTime);
      return true;
    }
    delay(1000);
  }
  return false;
}