#ifndef _FIRMHEADER_H_
#define _FIRMHEADER_H_

#pragma pack(1)

#include "little.h"

typedef struct
{
    unsigned_int offset;
    unsigned_int address;
    unsigned_int size;
    unsigned_int procType;
    unsigned char hash[0x20];
} firmSection;

typedef struct
{
    char magic[4];
    unsigned_int reserved1;
    unsigned_int arm11Entry;
    unsigned_int arm9Entry;
    unsigned char reserved2[0x30];
    firmSection section[4];
} firmHeader;

#pragma pack()

#endif // _FIRMHEADER_H_
