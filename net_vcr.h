#pragma once

#define VCR_OP_CONNECT					1
#define VCR_OP_GETMESSAGE				2
#define VCR_OP_SENDMESSAGE				3
#define VCR_OP_CANSENDMESSAGE			4
#define VCR_MAX_MESSAGE					4

int        VCR_Init();
void       VCR_Listen(qboolean         state);
void       VCR_SearchForHosts(qboolean xmit);
qsocket_t* VCR_Connect(char*           host);
qsocket_t* VCR_CheckNewConnections();
int        VCR_GetMessage(qsocket_t*     sock);
int        VCR_SendMessage(qsocket_t*    sock, sizebuf_t* data);
qboolean   VCR_CanSendMessage(qsocket_t* sock);
void       VCR_Close(qsocket_t*          sock);
void       VCR_Shutdown();
