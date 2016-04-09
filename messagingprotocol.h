#pragma once
#include "dchat.h"
#include "messagemanagement.h"
#include "clientmanagement.h"

int LOCALPORT;
int SEQ_NO; 

packet_t* parsePacket(char*);
void *receive_UDP(void* t);
void multicast_UDP(packettype_t packettype, char sender[], char messagebody[]);
void getLocalIp(char*);
