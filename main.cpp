#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/time.h>
#include <stdint.h>
#include <fcntl.h>

#ifndef __WIN32__
#include <sys/socket.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
typedef int socklen_t;
typedef uint32_t in_addr_t;
#endif

#include "ndsheader.h"
#include "gbaheader.h"
#include "firmheader.h"
#include "wifibootheader.h"

#define IS_TYPE_3DS(type) (type == F_FIRM || type == F_3DSX)

char cmdbuf[3072];
uint32_t cmdlen=0;

enum F_Type {
	F_NDS,
	F_GBA,
	F_FIRM,
	F_3DSX,
	F_UNKNOWN
};

//---------------------------------------------------------------------------------
void shutdownSocket(int socket) {
//---------------------------------------------------------------------------------
#ifdef __WIN32__
	shutdown (socket, SD_SEND);
	closesocket (socket);
#else
	close(socket);
#endif
}   

/*---------------------------------------------------------------------------------
	Subtract the `struct timeval' values Y from X,
	storing the result in RESULT.
	Return 1 if the difference is negative, otherwise 0.  

	From http://www.gnu.org/software/libtool/manual/libc/Elapsed-Time.html
---------------------------------------------------------------------------------*/
int timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y) {
//---------------------------------------------------------------------------------
	struct timeval tmp;
	tmp.tv_sec = y->tv_sec;
	tmp.tv_usec = y->tv_usec;
	
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_usec < tmp.tv_usec) {
		int nsec = (tmp.tv_usec - x->tv_usec) / 1000000 + 1;
		tmp.tv_usec -= 1000000 * nsec;
		tmp.tv_sec += nsec;
	}

	if (x->tv_usec - tmp.tv_usec > 1000000) {
		int nsec = (x->tv_usec - tmp.tv_usec) / 1000000;
		tmp.tv_usec += 1000000 * nsec;
		tmp.tv_sec -= nsec;
	}

	/*	Compute the time remaining to wait.
		tv_usec is certainly positive. */
	result->tv_sec = x->tv_sec - tmp.tv_sec;
	result->tv_usec = x->tv_usec - tmp.tv_usec;

	/* Return 1 if result is negative. */
	return x->tv_sec < tmp.tv_sec;
}

//---------------------------------------------------------------------------------
void timeval_add (struct timeval *result, struct timeval *x, struct timeval *y) {
//---------------------------------------------------------------------------------
	result->tv_sec = x->tv_sec + y->tv_sec;
	result->tv_usec = x->tv_usec + y->tv_usec;

	if ( result->tv_usec > 1000000) {
		result->tv_sec += result->tv_usec / 1000000;
		result->tv_usec = result->tv_usec % 1000000;
	}
}

//---------------------------------------------------------------------------------
static const char* getMess(F_Type type, bool a) {
//---------------------------------------------------------------------------------
	switch (type) {
		case F_NDS:
			return a ? "dsboot" : "bootds";
		case F_GBA:
			return a ? "gbaboot" : "bootgba";
		case F_FIRM:
			return a ? "3dsfirmboot": "bootfirm3ds";
		default:
			return NULL;
	}
}

//---------------------------------------------------------------------------------
static struct in_addr findDS(F_Type type) {
//---------------------------------------------------------------------------------
    struct sockaddr_in s, remote, rs;
	char recvbuf[256];
	const char* mess = getMess(type, true);
	const char* rmess = getMess(type, false);

	if (mess == NULL || rmess == NULL) {
		remote.sin_addr.s_addr = INADDR_NONE;
		return remote.sin_addr;
	}

	int broadcastSock = socket(PF_INET, SOCK_DGRAM, 0);
	if(broadcastSock < 0) perror("create send socket");

	int optval = 1, len;
	setsockopt(broadcastSock, SOL_SOCKET, SO_BROADCAST, (char *)&optval, sizeof(optval));

	memset(&s, '\0', sizeof(struct sockaddr_in));
    s.sin_family = AF_INET;
    s.sin_port = htons(17491);
    s.sin_addr.s_addr = INADDR_BROADCAST;

	memset(&rs, '\0', sizeof(struct sockaddr_in));
    rs.sin_family = AF_INET;
    rs.sin_port = htons(17491);
    rs.sin_addr.s_addr = INADDR_ANY;

	int recvSock = socket(PF_INET, SOCK_DGRAM, 0);

	if (recvSock < 0)  perror("create receive socket");

	if(bind(recvSock, (struct sockaddr*) &rs, sizeof(rs)) < 0) perror("bind");
#ifndef __WIN32__
	fcntl(recvSock, F_SETFL, O_NONBLOCK);
#else
	u_long opt = 1;
	ioctlsocket(recvSock, FIONBIO, &opt);
#endif
	struct timeval wanted, now, result;
	
	gettimeofday(&wanted, NULL);

	int timeout = 10;
	while(timeout) {
		gettimeofday(&now, NULL);
		if ( timeval_subtract(&result,&wanted,&now)) {
			if(sendto(broadcastSock, mess, strlen(mess), 0, (struct sockaddr *)&s, sizeof(s)) < 0) perror("sendto");
			result.tv_sec=0;
			result.tv_usec=150000;
			timeval_add(&wanted,&now,&result);
			timeout--;
		}
		socklen_t socklen = sizeof(remote);
		len = recvfrom(recvSock,recvbuf,sizeof(recvbuf),0,(struct sockaddr *)&remote,&socklen);
		if ( len != -1) {
			if ( strncmp(rmess,recvbuf,strlen(rmess)) == 0) {
				break;
			}
		}
	}
	if (timeout == 0) remote.sin_addr.s_addr = INADDR_NONE;
	shutdownSocket(broadcastSock);
	shutdownSocket(recvSock);
	return remote.sin_addr;
}

//---------------------------------------------------------------------------------
int sendData(int socket, int sendsize, char *buffer) {
//---------------------------------------------------------------------------------
	while(sendsize) {
		int len = send(socket, buffer, sendsize, 0);
		if (len <= 0) break;
		sendsize -= len;
		buffer += len;
	}
	return sendsize != 0;
}

//---------------------------------------------------------------------------------
F_Type getFileType(char *buffer) {
//---------------------------------------------------------------------------------
	firmHeader *firmHdr = (firmHeader*)buffer;
	gbaHeader *gbaHdr = (gbaHeader*)buffer;

	if (strncmp("FIRM", firmHdr->magic, strlen("FIRM")) == 0) {
		return F_FIRM;
	}

	if (strncmp("3DSX", buffer, strlen("3DSX")) == 0) {
		return F_3DSX;
	}

	if (gbaHdr->fixed == 0x96 && memcmp(gba_logo, gbaHdr->logo, 0xA0-0x04) == 0) {
		return F_GBA;
	}

	return F_NDS; // TODO: any method to check?

	//return F_UNKNOWN;
}

//---------------------------------------------------------------------------------
infoBlock genInfoBlock(F_Type type, size_t logo_size, size_t icon_size, size_t banner_size) {
//---------------------------------------------------------------------------------
	infoBlock info;
	memcpy(info.id, "BootINFO", 8);
	strcpy(info.uploader, "wifiboot-host v0");
	// TODO: proper time?
	info.logo_size = logo_size;
	info.icon_size = IS_TYPE_3DS(type) ? icon_size : 0;
	info.banner_size = IS_TYPE_3DS(type) ? banner_size : 0;
	return info;
}

//---------------------------------------------------------------------------------
int sendNDSFile(in_addr_t dsaddr, char *buffer) {
//---------------------------------------------------------------------------------

	int retval = 0;
	ndsHeader *header;
	infoBlock info;
	char *arm9, *arm7;
	int arm7size, arm9size;

	int sock = socket(AF_INET,SOCK_STREAM,0);
	if (sock < 0)  perror("create connection socket");

	struct sockaddr_in s;
	memset(&s, '\0', sizeof(struct sockaddr_in));
    s.sin_family = AF_INET;
    s.sin_port = htons(17491);
    s.sin_addr.s_addr = dsaddr;

	if (connect(sock,(struct sockaddr *)&s,sizeof(s)) < 0 ) {
		struct in_addr address;
		address.s_addr = dsaddr;
		fprintf(stderr,"Connection to %s failed",inet_ntoa(address));
		free(buffer);
		return 1;
	}
	
	printf("Sending NDS header ...\n");
	if (sendData(sock,0x170,buffer)) {
		fprintf(stderr,"Failed sending header\n");
		retval = 1;
		goto error;
	}
	
	printf("Sending info block ...\n");
	info = genInfoBlock(F_NDS, 0, 0, 0);
	if (sendData(sock,0x90,(char*)&info)) {
		fprintf(stderr,"Failed sending info block\n");
		retval = 1;
		goto error;
	}

	int response, errorcode;

	if(recv(sock,(char*)&response,sizeof(response),0)!=sizeof(response)) {
		fprintf(stderr,"Invalid response\n");
		retval = 1;
		goto error;
	}

	errorcode = response & 0x0f;

	if(errorcode!=0) {
		switch(errorcode) {
			case 1:
				fprintf(stderr,"Invalid ARM9 address/length\n");
				break;
			case 2:
				fprintf(stderr,"Invalid ARM7 address/length\n");
				break;
		}
		retval = 1;
		goto error;
	}

	if (response & (1<<16)) {
		printf("Sending DSi header ...\n");
		if (sendData(sock,0x1000,buffer)) {
			fprintf(stderr,"Failed sending header\n");
			retval = 1;
			goto error;
		}
	}

	header = (ndsHeader *)buffer;
	arm7 = buffer + header->arm7_rom_offset;
	arm9 = buffer + header->arm9_rom_offset;
	
	arm7size = header->arm7_size;
	arm9size = header->arm9_size;
	
	printf("Sending arm7, %d bytes\n",arm7size);
	
	if(sendData(sock,arm7size,arm7)) {

		fprintf(stderr,"Failed sending arm7 binary\n");
		retval = 1;
		goto error;

	}

	printf("Sending arm9, %d bytes\n",arm9size);

	if(sendData(sock,arm9size,arm9)) {

		fprintf(stderr,"Failed sending arm9 binary\n");
		retval = 1;
		goto error;
	}

	if (response & (1<<16)) {

		char *arm7i = buffer + header->dsi7_rom_offset;
		char *arm9i = buffer + header->dsi9_rom_offset;

		int arm7isize = header->dsi7_size;
		int arm9isize = header->dsi9_size;

		if (arm7isize) {
			printf("Sending arm7i, %d bytes\n",arm7isize);

			if(sendData(sock,arm7isize,arm7i)) {

				fprintf(stderr,"Failed sending arm7i binary\n");
				retval = 1;
				goto error;
			}

		}

		if (arm9isize) {
			printf("Sending arm9i, %d bytes\n",arm9isize);

			if(sendData(sock,arm9isize,arm9i)) {

				fprintf(stderr,"Failed sending arm9i binary\n");
				retval = 1;
				goto error;
			}
		}

	}


	if(sendData(sock,cmdlen+4,cmdbuf)) {

		fprintf(stderr,"Failed sending command line\n");
		retval = 1;
	}
			

error:
	shutdownSocket(sock);
	free(buffer);
	return retval;
}

//---------------------------------------------------------------------------------
gbaFooter genGBAFooter(size_t rom_size) {
//---------------------------------------------------------------------------------
	gbaFooter footer;
	// it seems that wifiboot doesn't really care most of the structure
	// only read (hardcode offset) rom_size, save_type and 4 cycle registers
	memcpy(footer.magic, ".CAA", 4);
	footer.fixed = 1;
	footer.cfg_offset = 0x1000330UL;
	footer.cfg_count = 2<<4;
	footer.cfg_desc[1].flag = 1;
	footer.cfg_desc[1].offset = 0x1000000UL;
	footer.cfg_desc[1].size = 0x324;
	footer.config[0].rom_size = rom_size;
	footer.config[0].save_type = 0xF; // no save
	footer.config[0].padding2[0] = 0xFF;
	footer.config[0].padding2[1] = 0xFF;
	footer.config[0].lcd_ghosting = 0xC0;
	//memcpy(footer.config[0].video_lut,default_video_lut,0x300);
	return footer;
}

//---------------------------------------------------------------------------------
int sendGBAFile(in_addr_t dsaddr, char *buffer, size_t size, gbaFooter *footer) {
//---------------------------------------------------------------------------------

	int retval = 0;
	//gbaHeader *header;
	infoBlock info;

	int sock = socket(AF_INET,SOCK_STREAM,0);
	if (sock < 0)  perror("create connection socket");

	struct sockaddr_in s;
	memset(&s, '\0', sizeof(struct sockaddr_in));
    s.sin_family = AF_INET;
    s.sin_port = htons(17491);
    s.sin_addr.s_addr = dsaddr;

	if (connect(sock,(struct sockaddr *)&s,sizeof(s)) < 0 ) {
		struct in_addr address;
		address.s_addr = dsaddr;
		fprintf(stderr,"Connection to %s failed",inet_ntoa(address));
		free(buffer);
		return 1;
	}

	printf("Sending GBA header ...\n");
	if (sendData(sock,0xC0,buffer)) {
		fprintf(stderr,"Failed sending header\n");
		retval = 1;
		goto error;
	}

	printf("Sending info block ...\n");
	info = genInfoBlock(F_GBA, 0, 0, 0);
	if (sendData(sock,0x90,(char*)&info)) {
		fprintf(stderr,"Failed sending info block\n");
		retval = 1;
		goto error;
	}

	printf("Sending gba vc footer ...\n");
	footer->config[0].rom_size = size; // make sure size is correct
	if (sendData(sock,0x360,(char*)footer)) {
		fprintf(stderr,"Failed sending gba vc footer\n");
		retval = 1;
		goto error;
	}

	int response, errorcode;

	if(recv(sock,(char*)&response,sizeof(response),0)!=sizeof(response)) {
		fprintf(stderr,"Invalid response\n");
		retval = 1;
		goto error;
	}

	errorcode = response & 0x0f;

	if(errorcode!=0) {
		/*
		switch(errorcode) {
			case 1:
				fprintf(stderr,"Invalid ARM9 address/length\n");
				break;
			case 2:
				fprintf(stderr,"Invalid ARM7 address/length\n");
				break;
		}
		*/
		retval = 1;
		goto error;
	}

	printf("Sending rom, %lu bytes\n",size);

	if(sendData(sock,size,buffer)) {

		fprintf(stderr,"Failed sending arm9 binary\n");
		retval = 1;
		goto error;
	}

	if(sendData(sock,cmdlen+4,cmdbuf)) {

		fprintf(stderr,"Failed sending command line\n");
		retval = 1;
	}


error:
	shutdownSocket(sock);
	free(buffer);
	return retval;
}

//---------------------------------------------------------------------------------
int send3DSFirmFile(in_addr_t dsaddr, char *buffer) {
//---------------------------------------------------------------------------------

	int retval = 0;
	firmHeader *header;
	infoBlock info;
	char *section;
	int sectionSize;

	int sock = socket(AF_INET,SOCK_STREAM,0);
	if (sock < 0)  perror("create connection socket");

	struct sockaddr_in s;
	memset(&s, '\0', sizeof(struct sockaddr_in));
    s.sin_family = AF_INET;
    s.sin_port = htons(17491);
    s.sin_addr.s_addr = dsaddr;

	if (connect(sock,(struct sockaddr *)&s,sizeof(s)) < 0 ) {
		struct in_addr address;
		address.s_addr = dsaddr;
		fprintf(stderr,"Connection to %s failed",inet_ntoa(address));
		free(buffer);
		return 1;
	}

	printf("Sending 3DS Firm header ...\n");
	if (sendData(sock,0x200,buffer)) {
		fprintf(stderr,"Failed sending header\n");
		retval = 1;
		goto error;
	}

	printf("Sending info block ...\n");
	info = genInfoBlock(F_FIRM, 0, 0, 0);
	if (sendData(sock,0x90,(char*)&info)) {
		fprintf(stderr,"Failed sending info block\n");
		retval = 1;
		goto error;
	}

	int response, errorcode;

	if(recv(sock,(char*)&response,sizeof(response),0)!=sizeof(response)) {
		fprintf(stderr,"Invalid response\n");
		retval = 1;
		goto error;
	}

	errorcode = response & 0x0f;

	if(errorcode!=0) {
		fprintf(stderr,"Invalid header\n");
		/*
		switch(errorcode) {
			case 1:
				fprintf(stderr,"Invalid ARM9 address/length\n");
				break;
			case 2:
				fprintf(stderr,"Invalid ARM7 address/length\n");
				break;
		}
		*/
		retval = 1;
		goto error;
	}

	header = (firmHeader *)buffer;

	for (int i=0; i<4; i++) {
		section = buffer + header->section[i].offset;
		sectionSize = header->section[i].size;

		if (sectionSize == 0) continue;

		printf("Sending section %d, %d bytes\n",i, sectionSize);

		if(sendData(sock,sectionSize,section)) {

			fprintf(stderr,"Failed sending section %d\n",i);
			retval = 1;
			goto error;

		}
	}


	if(sendData(sock,cmdlen+4,cmdbuf)) {

		fprintf(stderr,"Failed sending command line\n");
		retval = 1;
	}


error:
	shutdownSocket(sock);
	free(buffer);
	return retval;
}

//---------------------------------------------------------------------------------
static int readGBAFooter(char* file, gbaFooter *footer, bool silent) {
//---------------------------------------------------------------------------------
	if (!silent) printf("Reading gba footer file...\n");

	FILE *f = fopen(file,"rb");
	if (f==0) {
		fprintf(stderr,"Failed to open %s.\n",file);
		return 1;
	}

	fseek(f,0,SEEK_END);
	if (ftell(f) != 0x360) {
		fprintf(stderr,"Footer should be 0x360 bytes.\n");
	}
	fseek(f,0,SEEK_SET);

	if (fread((char*)footer,1,0x360,f) != 0x360){
		fprintf(stderr,"Failed to read file\n");
		fclose(f);
		return 1;
	}
	fclose(f);

	return 0;
}

//---------------------------------------------------------------------------------
static int readGBAFooter(char* file, gbaFooter *footer) {
//---------------------------------------------------------------------------------
	return readGBAFooter(file, footer, false);
}

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------
	int res = 0;
	char *address = NULL;
	char *footerfile = NULL;
	bool fb3dslink = false;
	int c;

	// wifiboot doesn't support address, it's mainly for 3dslink fallback
	while ((c = getopt (argc, argv, "3a:f:")) != -1) {
		switch(c) {
			case 'a':
				fprintf (stderr, "Warning: wifiboot doesn't support direct address, it's for 3dslink fallback only.\n");
				address = optarg;
				break;
			case 'f':
				footerfile = optarg;
				break;
			case '3':
				fb3dslink = true;
				break;
			case '?':
				if (optopt == 'a' || optopt == 'f')
					fprintf (stderr, "Option -%c requires an argument.\n", optopt);
				else if (isprint (optopt))
					fprintf (stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf (stderr, "Unknown option character `\\x%x'.\n",optopt);
				return 1;
		}
	}
	
	char *file = argv[optind];
	if (file == NULL) {
		//fprintf(stderr,"Usage: %s [-a address] file\n", argv[0]);
		fprintf(stderr,"Usage: %s [-3] file\n", argv[0]);
		return 1;
	}
	optind++;
	memset(cmdbuf, '\0', sizeof(cmdbuf));
	
	strcpy(&cmdbuf[4],"dslink:/");
	strcpy(&cmdbuf[12],file);

	cmdlen = strlen(&cmdbuf[4]) + 5;

	for (int index = optind; index < argc; index++) {
		int len=strlen(argv[index]);
		if ( (cmdlen + len + 1 ) >= (sizeof(cmdbuf) - 2) ) break;
		strcpy(&cmdbuf[cmdlen],argv[index]);
		cmdlen+= len + 1;
	}
	cmdlen -= 4;

	cmdbuf[0] = cmdlen & 0xff;
	cmdbuf[1] = (cmdlen>>8) & 0xff;
	cmdbuf[2] = (cmdlen>>16) & 0xff;
	cmdbuf[3] = (cmdlen>>24) & 0xff;

#ifdef __WIN32__
	WSADATA wsa_data;
	if (WSAStartup (MAKEWORD(2,2), &wsa_data)) {
		printf ("WSAStartup failed\n");
		return 1;
	}
#endif

	FILE *f = fopen(file,"rb");
	if (f==0) {
		fprintf(stderr,"Failed to open %s.\n",file);
		return 1;
	}

	fseek(f,0,SEEK_END);
	size_t size = ftell(f);
	fseek(f,0,SEEK_SET);

	char *buffer = (char*)malloc(size);
	if (buffer == NULL) {
		fprintf(stderr,"Failed to allocate file buffer\n");
		fclose(f);
		return 1;
	}

	if (fread(buffer,1,size,f) != size){
		fprintf(stderr,"Failed to read file\n");
		free(buffer);
		fclose(f);
		return 1;
	}
	fclose(f);

    F_Type type = getFileType(buffer);
	if (!IS_TYPE_3DS(type)) fb3dslink = false;

	struct in_addr dsaddr;
	dsaddr.s_addr = INADDR_NONE;

	dsaddr = findDS(type);

	if (dsaddr.s_addr == INADDR_NONE) {
		printf("No response from (3)DS or not supported file!\n");
		if (fb3dslink) goto cleanup;
		return 1;
	}

	switch (type) {
		case F_NDS:
			res = sendNDSFile(dsaddr.s_addr,buffer);
			break;
		case F_GBA:
			{
				gbaFooter footer;
				if (footerfile == NULL || readGBAFooter(footerfile,&footer) != 0) {
					footer = genGBAFooter(size);
				}
				res = sendGBAFile(dsaddr.s_addr,buffer,size,&footer);
			}
			break;
		case F_FIRM:
			res = send3DSFirmFile(dsaddr.s_addr,buffer);
			break;
		default:
			fprintf(stderr,"Unsupported file!\n");
			res = 1;
			break;
	}

	fb3dslink = false;

cleanup:
#ifdef __WIN32__
	WSACleanup ();
#endif
	if (fb3dslink) {
		fprintf(stderr,"3dslink fallback enabled, run it\n");
		if (address == NULL) {
			execlp("3dslink","3dslink",file,NULL);
		} else {
			execlp("3dslink","3dslink","-a",address,file,NULL);
		}
	}
	return res;
}

