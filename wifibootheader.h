#ifndef _WIFIBOOTHEADER_H_
#define _WIFIBOOTHEADER_H_

#pragma pack(1)

#include "little.h"

#define NUM2BCD(n)  ((n<99) ? (((n/10)*0x10)|(n%10)) : 0x99)

typedef struct {
    char id[8];
    char uploader[24];
    unsigned char time_sec;
    unsigned char time_min;
    unsigned char time_hour;
    unsigned char time_dayofweek;
    unsigned char time_day;
    unsigned char time_mon;
    unsigned char time_year;
    unsigned char time_century;
    unsigned_int icon_size;
    unsigned_int logo_size;
    unsigned_int banner_size;
    char reserved[0x5C];
} infoBlock;

#pragma pack()

#endif // _WIFIBOOTHEADER_H_
