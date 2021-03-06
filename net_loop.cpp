#include "quakedef.h"
#include "net_loop.h"

qboolean   localconnectpending = qfalse;
qsocket_t* loop_client         = nullptr;
qsocket_t* loop_server         = nullptr;

int Loop_Init()
{
	if (cls.state == cactive_t::ca_dedicated)
		return -1;
	return 0;
}


void Loop_Shutdown()
{
}


void Loop_Listen(qboolean state)
{
}


void Loop_SearchForHosts(qboolean xmit)
{
	if (!sv.active)
		return;

	hostCacheCount = 1;
	if (Q_strcmp(hostname.string, "UNNAMED") == 0)
		Q_strcpy(hostcache[0].name, "local");
	else
		Q_strcpy(hostcache[0].name, hostname.string);
	Q_strcpy(hostcache[0].map, sv.name);
	hostcache[0].users    = net_activeconnections;
	hostcache[0].maxusers = svs.maxclients;
	hostcache[0].driver   = net_driverlevel;
	Q_strcpy(hostcache[0].cname, "local");
}


qsocket_t* Loop_Connect(char* host)
{
	if (Q_strcmp(host, "local") != 0)
		return nullptr;

	localconnectpending = qtrue;

	if (!loop_client)
	{
		if ((loop_client = NET_NewQSocket()) == nullptr)
		{
			Con_Printf("Loop_Connect: no qsocket available\n");
			return nullptr;
		}
		Q_strcpy(loop_client->address, "localhost");
	}
	loop_client->receiveMessageLength = 0;
	loop_client->sendMessageLength    = 0;
	loop_client->canSend              = qtrue;

	if (!loop_server)
	{
		if ((loop_server = NET_NewQSocket()) == nullptr)
		{
			Con_Printf("Loop_Connect: no qsocket available\n");
			return nullptr;
		}
		Q_strcpy(loop_server->address, "LOCAL");
	}
	loop_server->receiveMessageLength = 0;
	loop_server->sendMessageLength    = 0;
	loop_server->canSend              = qtrue;

	loop_client->driverdata = static_cast<void *>(loop_server);
	loop_server->driverdata = static_cast<void *>(loop_client);

	return loop_client;
}


qsocket_t* Loop_CheckNewConnections()
{
	if (!localconnectpending)
		return nullptr;

	localconnectpending               = qfalse;
	loop_server->sendMessageLength    = 0;
	loop_server->receiveMessageLength = 0;
	loop_server->canSend              = qtrue;
	loop_client->sendMessageLength    = 0;
	loop_client->receiveMessageLength = 0;
	loop_client->canSend              = qtrue;
	return loop_server;
}


static int IntAlign(int value)
{
	return value + (sizeof(int) - 1) & ~(sizeof(int) - 1);
}


int Loop_GetMessage(qsocket_t* sock)
{
	if (sock->receiveMessageLength == 0)
		return 0;

	const int  ret    = sock->receiveMessage[0];
	auto length = sock->receiveMessage[1] + (sock->receiveMessage[2] << 8);
	// alignment byte skipped here
	SZ_Clear(&net_message);
	SZ_Write(&net_message, &sock->receiveMessage[4], length);

	length = IntAlign(length + 4);
	sock->receiveMessageLength -= length;

	if (sock->receiveMessageLength)
		Q_memcpy(sock->receiveMessage, &sock->receiveMessage[length], sock->receiveMessageLength);

	if (sock->driverdata && ret == 1)
		static_cast<qsocket_t *>(sock->driverdata)->canSend = qtrue;

	return ret;
}


int Loop_SendMessage(qsocket_t* sock, sizebuf_t* data)
{
	if (!sock->driverdata)
		return -1;

	const auto bufferLength = &static_cast<qsocket_t *>(sock->driverdata)->receiveMessageLength;

	if (*bufferLength + data->cursize + 4 > NET_MAXMESSAGE)
		Sys_Error("Loop_SendMessage: overflow\n");

	auto buffer = static_cast<qsocket_t *>(sock->driverdata)->receiveMessage + *bufferLength;

	// message type
	*buffer++ = 1;

	// length
	*buffer++ = data->cursize & 0xff;
	*buffer++ = data->cursize >> 8;

	// align
	buffer++;

	// message
	Q_memcpy(buffer, data->data, data->cursize);
	*bufferLength = IntAlign(*bufferLength + data->cursize + 4);

	sock->canSend = qfalse;
	return 1;
}


int Loop_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data)
{
	if (!sock->driverdata)
		return -1;

	const auto bufferLength = &static_cast<qsocket_t *>(sock->driverdata)->receiveMessageLength;

	if (*bufferLength + data->cursize + sizeof(byte) + sizeof(short) > NET_MAXMESSAGE)
		return 0;

	auto buffer = static_cast<qsocket_t *>(sock->driverdata)->receiveMessage + *bufferLength;

	// message type
	*buffer++ = 2;

	// length
	*buffer++ = data->cursize & 0xff;
	*buffer++ = data->cursize >> 8;

	// align
	buffer++;

	// message
	Q_memcpy(buffer, data->data, data->cursize);
	*bufferLength = IntAlign(*bufferLength + data->cursize + 4);
	return 1;
}


qboolean Loop_CanSendMessage(qsocket_t* sock)
{
	if (!sock->driverdata)
		return qfalse;
	return sock->canSend;
}


qboolean Loop_CanSendUnreliableMessage(qsocket_t* sock)
{
	return qtrue;
}


void Loop_Close(qsocket_t* sock)
{
	if (sock->driverdata)
		static_cast<qsocket_t *>(sock->driverdata)->driverdata = nullptr;
	sock->receiveMessageLength                                 = 0;
	sock->sendMessageLength                                    = 0;
	sock->canSend                                              = qtrue;
	if (sock == loop_client)
		loop_client = nullptr;
	else
		loop_server = nullptr;
}
