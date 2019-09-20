#ifndef NTPCLIENT_h
#define NTPCLIENT_h

#include <application.h>
//#include <math.h>

class NtpTime{
public:
  bool setTime(const char*, const unsigned int);
  unsigned long getTime(const char*, const unsigned int);
};

//extern NtpTime TD;

#endif