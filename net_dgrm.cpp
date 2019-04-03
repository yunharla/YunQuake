#include "quakedef.h"
#include "net_dgrm.h"

// these two macros are to make the code more readable
#define sfunc	net_landrivers[sock->landriver]
#define dfunc	net_landrivers[net_landriverlevel]

static int net_landriverlevel;

/* statistic counters */
int packetsSent            = 0;
int packetsReSent          = 0;
int packetsReceived        = 0;
int receivedDuplicateCount = 0;
int shortPacketCount       = 0;
int droppedDatagrams;

static int myDriverLevel;

struct
{
	unsigned int length;
	unsigned int sequence;
	byte         data[MAX_DATAGRAM];
}                packetBuffer;

extern m_state_t m_return_state;
extern m_state_t m_state;
extern qboolean  m_return_onerror;
extern char      m_return_reason[32];


#ifdef DEBUG
char *StrAddr (qsockaddr *addr)
{
	static char buf[34];
	byte *p = (byte *)addr;
	int n;

	for (n = 0; n < 16; n++)
		sprintf (buf + n * 2, "%02x", *p++);
	return buf;
}
#endif


int Datagram_SendMessage(qsocket_t* sock, sizebuf_t* data)
{
	unsigned int dataLen;
	unsigned int eom;

#ifdef DEBUG
	if (data->cursize == 0)
		Sys_Error("Datagram_SendMessage: zero length message\n");

	if (data->cursize > NET_MAXMESSAGE)
		Sys_Error("Datagram_SendMessage: message too big %u\n", data->cursize);

	if (sock->canSend == qfalse)
		Sys_Error("SendMessage: called with canSend == qfalse\n");
#endif

	Q_memcpy(sock->sendMessage, data->data, data->cursize);
	sock->sendMessageLength = data->cursize;

	if (data->cursize <= MAX_DATAGRAM)
	{
		dataLen = data->cursize;
		eom     = NETFLAG_EOM;
	}
	else
	{
		dataLen = MAX_DATAGRAM;
		eom     = 0;
	}

	const auto packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length   = BigLong(packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong(sock->sendSequence++);
	Q_memcpy(packetBuffer.data, sock->sendMessage, dataLen);

	sock->canSend = qfalse;

	if (sfunc.Write(sock->socket, reinterpret_cast<byte *>(&packetBuffer), packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsSent++;
	return 1;
}


int SendMessageNext(qsocket_t* sock)
{
	unsigned int dataLen;
	unsigned int eom;

	if (sock->sendMessageLength <= MAX_DATAGRAM)
	{
		dataLen = sock->sendMessageLength;
		eom     = NETFLAG_EOM;
	}
	else
	{
		dataLen = MAX_DATAGRAM;
		eom     = 0;
	}
	const auto packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length   = BigLong(packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong(sock->sendSequence++);
	Q_memcpy(packetBuffer.data, sock->sendMessage, dataLen);

	sock->sendNext = qfalse;

	if (sfunc.Write(sock->socket, reinterpret_cast<byte *>(&packetBuffer), packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsSent++;
	return 1;
}


int ReSendMessage(qsocket_t* sock)
{
	unsigned int dataLen;
	unsigned int eom;

	if (sock->sendMessageLength <= MAX_DATAGRAM)
	{
		dataLen = sock->sendMessageLength;
		eom     = NETFLAG_EOM;
	}
	else
	{
		dataLen = MAX_DATAGRAM;
		eom     = 0;
	}
	const auto packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length   = BigLong(packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong(sock->sendSequence - 1);
	Q_memcpy(packetBuffer.data, sock->sendMessage, dataLen);

	sock->sendNext = qfalse;

	if (sfunc.Write(sock->socket, reinterpret_cast<byte *>(&packetBuffer), packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsReSent++;
	return 1;
}


qboolean Datagram_CanSendMessage(qsocket_t* sock)
{
	if (sock->sendNext)
		SendMessageNext(sock);

	return sock->canSend;
}


qboolean Datagram_CanSendUnreliableMessage(qsocket_t* sock)
{
	return qtrue;
}


int Datagram_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data)
{
#ifdef DEBUG
	if (data->cursize == 0)
		Sys_Error("Datagram_SendUnreliableMessage: zero length message\n");

	if (data->cursize > MAX_DATAGRAM)
		Sys_Error("Datagram_SendUnreliableMessage: message too big %u\n", data->cursize);
#endif

	const int packetLen = NET_HEADERSIZE + data->cursize;

	packetBuffer.length   = BigLong(packetLen | NETFLAG_UNRELIABLE);
	packetBuffer.sequence = BigLong(sock->unreliableSendSequence++);
	Q_memcpy(packetBuffer.data, data->data, data->cursize);

	if (sfunc.Write(sock->socket, reinterpret_cast<byte *>(&packetBuffer), packetLen, &sock->addr) == -1)
		return -1;

	packetsSent++;
	return 1;
}


int Datagram_GetMessage(qsocket_t* sock)
{
	auto             ret = 0;
	struct qsockaddr readaddr;

	if (!sock->canSend)
		if (net_time - sock->lastSendTime > 1.0)
			ReSendMessage(sock);

	while (true)
	{
		unsigned int length = sfunc.Read(sock->socket, reinterpret_cast<byte *>(&packetBuffer), NET_DATAGRAMSIZE, &readaddr);

		//	if ((rand() & 255) > 220)
		//		continue;

		if (length == 0)
			break;

		if (length == -1)
		{
			Con_Printf("Read error\n");
			return -1;
		}

		if (sfunc.AddrCompare(&readaddr, &sock->addr) != 0)
		{
#ifdef DEBUG
			Con_DPrintf("Forged packet received\n");
			Con_DPrintf("Expected: %s\n", StrAddr (&sock->addr));
			Con_DPrintf("Received: %s\n", StrAddr (&readaddr));
#endif
			continue;
		}

		if (length < NET_HEADERSIZE)
		{
			shortPacketCount++;
			continue;
		}

		length     = BigLong(packetBuffer.length);
		const auto flags = length & ~NETFLAG_LENGTH_MASK;
		length &= NETFLAG_LENGTH_MASK;

		if (flags & NETFLAG_CTL)
			continue;

		const unsigned int sequence = BigLong(packetBuffer.sequence);
		packetsReceived++;

		if (flags & NETFLAG_UNRELIABLE)
		{
			if (sequence < sock->unreliableReceiveSequence)
			{
				Con_DPrintf("Got a stale datagram\n");
				ret = 0;
				break;
			}
			if (sequence != sock->unreliableReceiveSequence)
			{
				const auto count = sequence - sock->unreliableReceiveSequence;
				droppedDatagrams += count;
				Con_DPrintf("Dropped %u datagram(s)\n", count);
			}
			sock->unreliableReceiveSequence = sequence + 1;

			length -= NET_HEADERSIZE;

			SZ_Clear(&net_message);
			SZ_Write(&net_message, packetBuffer.data, length);

			ret = 2;
			break;
		}

		if (flags & NETFLAG_ACK)
		{
			if (sequence != sock->sendSequence - 1)
			{
				Con_DPrintf("Stale ACK received\n");
				continue;
			}
			if (sequence == sock->ackSequence)
			{
				sock->ackSequence++;
				if (sock->ackSequence != sock->sendSequence)
					Con_DPrintf("ack sequencing error\n");
			}
			else
			{
				Con_DPrintf("Duplicate ACK received\n");
				continue;
			}
			sock->sendMessageLength -= MAX_DATAGRAM;
			if (sock->sendMessageLength > 0)
			{
				Q_memcpy(sock->sendMessage, sock->sendMessage + MAX_DATAGRAM, sock->sendMessageLength);
				sock->sendNext = qtrue;
			}
			else
			{
				sock->sendMessageLength = 0;
				sock->canSend           = qtrue;
			}
			continue;
		}

		if (flags & NETFLAG_DATA)
		{
			packetBuffer.length   = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
			packetBuffer.sequence = BigLong(sequence);
			sfunc.Write(sock->socket, reinterpret_cast<byte *>(&packetBuffer), NET_HEADERSIZE, &readaddr);

			if (sequence != sock->receiveSequence)
			{
				receivedDuplicateCount++;
				continue;
			}
			sock->receiveSequence++;

			length -= NET_HEADERSIZE;

			if (flags & NETFLAG_EOM)
			{
				SZ_Clear(&net_message);
				SZ_Write(&net_message, sock->receiveMessage, sock->receiveMessageLength);
				SZ_Write(&net_message, packetBuffer.data, length);
				sock->receiveMessageLength = 0;

				ret = 1;
				break;
			}

			Q_memcpy(sock->receiveMessage + sock->receiveMessageLength, packetBuffer.data, length);
			sock->receiveMessageLength += length;
		}
	}

	if (sock->sendNext)
		SendMessageNext(sock);

	return ret;
}


void PrintStats(qsocket_t* s)
{
	Con_Printf("canSend = %4u   \n", s->canSend);
	Con_Printf("sendSeq = %4u   ", s->sendSequence);
	Con_Printf("recvSeq = %4u   \n", s->receiveSequence);
	Con_Printf("\n");
}

void NET_Stats_f()
{
	qsocket_t* s;

	if (Cmd_Argc() == 1)
	{
		Con_Printf("unreliable messages sent   = %i\n", unreliableMessagesSent);
		Con_Printf("unreliable messages recv   = %i\n", unreliableMessagesReceived);
		Con_Printf("reliable messages sent     = %i\n", messagesSent);
		Con_Printf("reliable messages received = %i\n", messagesReceived);
		Con_Printf("packetsSent                = %i\n", packetsSent);
		Con_Printf("packetsReSent              = %i\n", packetsReSent);
		Con_Printf("packetsReceived            = %i\n", packetsReceived);
		Con_Printf("receivedDuplicateCount     = %i\n", receivedDuplicateCount);
		Con_Printf("shortPacketCount           = %i\n", shortPacketCount);
		Con_Printf("droppedDatagrams           = %i\n", droppedDatagrams);
	}
	else if (Q_strcmp(Cmd_Argv(1), "*") == 0)
	{
		for (s = net_activeSockets; s; s = s->next)
			PrintStats(s);
		for (s = net_freeSockets; s; s = s->next)
			PrintStats(s);
	}
	else
	{
		for (s = net_activeSockets; s; s = s->next)
			if (Q_strcasecmp(Cmd_Argv(1), s->address) == 0)
				break;
		if (s == nullptr)
			for (s = net_freeSockets; s; s = s->next)
				if (Q_strcasecmp(Cmd_Argv(1), s->address) == 0)
					break;
		if (s == nullptr)
			return;
		PrintStats(s);
	}
}


static qboolean testInProgress = qfalse;
static int      testPollCount;
static int      testDriver;
static int      testSocket;

static void   Test_Poll();
PollProcedure testPollProcedure = {nullptr, 0.0, Test_Poll};

static void Test_Poll()
{
	struct qsockaddr clientaddr;
	char             name[32];
	char             address[64];

	net_landriverlevel = testDriver;

	while (true)
	{
		const auto len = dfunc.Read(testSocket, net_message.data, net_message.maxsize, &clientaddr);
		if (len < sizeof(int))
			break;

		net_message.cursize = len;

		MSG_BeginReading();
		const auto control = BigLong(*reinterpret_cast<int *>(net_message.data));
		MSG_ReadLong();
		if (control == -1)
			break;
		if ((control & ~NETFLAG_LENGTH_MASK) != NETFLAG_CTL)
			break;
		if ((control & NETFLAG_LENGTH_MASK) != len)
			break;

		if (MSG_ReadByte() != CCREP_PLAYER_INFO)
			Sys_Error("Unexpected repsonse to Player Info request\n");

		MSG_ReadByte(); //playerNumber
		Q_strcpy(name, MSG_ReadString());
		const auto colors      = MSG_ReadLong();
		const auto frags       = MSG_ReadLong();
		const auto connectTime = MSG_ReadLong();
		Q_strcpy(address, MSG_ReadString());

		Con_Printf("%s\n  frags:%3i  colors:%u %u  time:%u\n  %s\n", name, frags, colors >> 4, colors & 0x0f, connectTime / 60, address);
	}

	testPollCount--;
	if (testPollCount)
	{
		SchedulePollProcedure(&testPollProcedure, 0.1);
	}
	else
	{
		dfunc.CloseSocket(testSocket);
		testInProgress = qfalse;
	}
}

static void Test_f()
{
	int              n;
	auto             max = MAX_SCOREBOARD;
	struct qsockaddr sendaddr;

	if (testInProgress)
		return;

	const auto host = Cmd_Argv(1);

	if (host && hostCacheCount)
	{
		for (n = 0; n < hostCacheCount; n++)
			if (Q_strcasecmp(host, hostcache[n].name) == 0)
			{
				if (hostcache[n].driver != myDriverLevel)
					continue;
				net_landriverlevel = hostcache[n].ldriver;
				max                = hostcache[n].maxusers;
				Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
				break;
			}
		if (n < hostCacheCount)
			goto JustDoIt;
	}

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;

		// see if we can resolve the host name
		if (dfunc.GetAddrFromName(host, &sendaddr) != -1)
			break;
	}
	if (net_landriverlevel == net_numlandrivers)
		return;

JustDoIt:
	testSocket = dfunc.OpenSocket(0);
	if (testSocket == -1)
		return;

	testInProgress = qtrue;
	testPollCount  = 20;
	testDriver     = net_landriverlevel;

	for (n = 0; n < max; n++)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_PLAYER_INFO);
		MSG_WriteByte(&net_message, n);
		*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
		dfunc.Write(testSocket, net_message.data, net_message.cursize, &sendaddr);
	}
	SZ_Clear(&net_message);
	SchedulePollProcedure(&testPollProcedure, 0.1);
}


static qboolean test2InProgress = qfalse;
static int      test2Driver;
static int      test2Socket;

static void   Test2_Poll();
PollProcedure test2PollProcedure = {nullptr, 0.0, Test2_Poll};

static void Test2_Poll()
{
	struct qsockaddr clientaddr;
	char             name[256];
	char             value[256];

	net_landriverlevel = test2Driver;
	name[0]            = 0;

	const auto len = dfunc.Read(test2Socket, net_message.data, net_message.maxsize, &clientaddr);
	if (len < sizeof(int))
		goto Reschedule;

	net_message.cursize = len;

	MSG_BeginReading();
	const auto control = BigLong(*reinterpret_cast<int *>(net_message.data));
	MSG_ReadLong();
	if (control == -1)
		goto Error;
	if ((control & ~NETFLAG_LENGTH_MASK) != NETFLAG_CTL)
		goto Error;
	if ((control & NETFLAG_LENGTH_MASK) != len)
		goto Error;

	if (MSG_ReadByte() != CCREP_RULE_INFO)
		goto Error;

	Q_strcpy(name, MSG_ReadString());
	if (name[0] == 0)
		goto Done;
	Q_strcpy(value, MSG_ReadString());

	Con_Printf("%-16.16s  %-16.16s\n", name, value);

	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
	MSG_WriteString(&net_message, name);
	*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
	dfunc.Write(test2Socket, net_message.data, net_message.cursize, &clientaddr);
	SZ_Clear(&net_message);

Reschedule:
	SchedulePollProcedure(&test2PollProcedure, 0.05);
	return;

Error:
	Con_Printf("Unexpected repsonse to Rule Info request\n");
Done:
	dfunc.CloseSocket(test2Socket);
	test2InProgress = qfalse;
}

static void Test2_f()
{
	int              n;
	struct qsockaddr sendaddr;

	if (test2InProgress)
		return;

	const auto host = Cmd_Argv(1);

	if (host && hostCacheCount)
	{
		for (n = 0; n < hostCacheCount; n++)
			if (Q_strcasecmp(host, hostcache[n].name) == 0)
			{
				if (hostcache[n].driver != myDriverLevel)
					continue;
				net_landriverlevel = hostcache[n].ldriver;
				Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
				break;
			}
		if (n < hostCacheCount)
			goto JustDoIt;
	}

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;

		// see if we can resolve the host name
		if (dfunc.GetAddrFromName(host, &sendaddr) != -1)
			break;
	}
	if (net_landriverlevel == net_numlandrivers)
		return;

JustDoIt:
	test2Socket = dfunc.OpenSocket(0);
	if (test2Socket == -1)
		return;

	test2InProgress = qtrue;
	test2Driver     = net_landriverlevel;

	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
	MSG_WriteString(&net_message, "");
	*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
	dfunc.Write(test2Socket, net_message.data, net_message.cursize, &sendaddr);
	SZ_Clear(&net_message);
	SchedulePollProcedure(&test2PollProcedure, 0.05);
}


int Datagram_Init()
{
	myDriverLevel = net_driverlevel;
	Cmd_AddCommand("net_stats", NET_Stats_f);

	if (COM_CheckParm("-nolan"))
		return -1;

	for (auto i = 0; i < net_numlandrivers; i++)
	{
		const auto csock = net_landrivers[i].Init();
		if (csock == -1)
			continue;
		net_landrivers[i].initialized = qtrue;
		net_landrivers[i].controlSock = csock;
	}

	Cmd_AddCommand("test", Test_f);
	Cmd_AddCommand("test2", Test2_f);

	return 0;
}


void Datagram_Shutdown()
{
	//
	// shutdown the lan drivers
	//
	for (auto i = 0; i < net_numlandrivers; i++)
	{
		if (net_landrivers[i].initialized)
		{
			net_landrivers[i].Shutdown();
			net_landrivers[i].initialized = qfalse;
		}
	}
}


void Datagram_Close(qsocket_t* sock)
{
	sfunc.CloseSocket(sock->socket);
}


void Datagram_Listen(qboolean state)
{
	for (auto i = 0; i < net_numlandrivers; i++)
		if (net_landrivers[i].initialized)
			net_landrivers[i].Listen(state);
}


static qsocket_t* _Datagram_CheckNewConnections()
{
	struct qsockaddr clientaddr;
	struct qsockaddr newaddr;

	const auto acceptsock = dfunc.CheckNewConnections();
	if (acceptsock == -1)
		return nullptr;

	SZ_Clear(&net_message);

	const auto len = dfunc.Read(acceptsock, net_message.data, net_message.maxsize, &clientaddr);
	if (len < sizeof(int))
		return nullptr;
	net_message.cursize = len;

	MSG_BeginReading();
	const auto control = BigLong(*reinterpret_cast<int *>(net_message.data));
	MSG_ReadLong();
	if (control == -1)
		return nullptr;
	if ((control & ~NETFLAG_LENGTH_MASK) != NETFLAG_CTL)
		return nullptr;
	if ((control & NETFLAG_LENGTH_MASK) != len)
		return nullptr;

	const auto command = MSG_ReadByte();
	if (command == CCREQ_SERVER_INFO)
	{
		if (Q_strcmp(MSG_ReadString(), "QUAKE") != 0)
			return nullptr;

		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_SERVER_INFO);
		dfunc.GetSocketAddr(acceptsock, &newaddr);
		MSG_WriteString(&net_message, dfunc.AddrToString(&newaddr));
		MSG_WriteString(&net_message, hostname.string);
		MSG_WriteString(&net_message, sv.name);
		MSG_WriteByte(&net_message, net_activeconnections);
		MSG_WriteByte(&net_message, svs.maxclients);
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
		dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);
		return nullptr;
	}

	if (command == CCREQ_PLAYER_INFO)
	{
		int       clientNumber;
		client_t* client;

		const auto playerNumber = MSG_ReadByte();
		auto activeNumber = -1;
		for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients; clientNumber++, client++)
		{
			if (client->active)
			{
				activeNumber++;
				if (activeNumber == playerNumber)
					break;
			}
		}
		if (clientNumber == svs.maxclients)
			return nullptr;

		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_PLAYER_INFO);
		MSG_WriteByte(&net_message, playerNumber);
		MSG_WriteString(&net_message, client->name);
		MSG_WriteLong(&net_message, client->colors);
		MSG_WriteLong(&net_message, static_cast<int>(client->edict->v.frags));
		MSG_WriteLong(&net_message, static_cast<int>(net_time - client->netconnection->connecttime));
		MSG_WriteString(&net_message, client->netconnection->address);
		*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
		dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);

		return nullptr;
	}

	if (command == CCREQ_RULE_INFO)
	{
		cvar_t* var;

		// find the search start location
		const auto prevCvarName = MSG_ReadString();
		if (*prevCvarName)
		{
			var = Cvar_FindVar(prevCvarName);
			if (!var)
				return nullptr;
			var = var->next;
		}
		else
			var = cvar_vars;

		// search for the next server cvar
		while (var)
		{
			if (var->server)
				break;
			var = var->next;
		}

		// send the response

		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_RULE_INFO);
		if (var)
		{
			MSG_WriteString(&net_message, var->name);
			MSG_WriteString(&net_message, var->string);
		}
		*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
		dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);

		return nullptr;
	}

	if (command != CCREQ_CONNECT)
		return nullptr;

	if (Q_strcmp(MSG_ReadString(), "QUAKE") != 0)
		return nullptr;

	if (MSG_ReadByte() != NET_PROTOCOL_VERSION)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_REJECT);
		MSG_WriteString(&net_message, "Incompatible version.\n");
		*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
		dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);
		return nullptr;
	}


	// see if this guy is already connected
	for (auto s = net_activeSockets; s; s = s->next)
	{
		if (s->driver != net_driverlevel)
			continue;
		const auto ret = dfunc.AddrCompare(&clientaddr, &s->addr);
		if (ret >= 0)
		{
			// is this a duplicate connection reqeust?
			if (ret == 0 && net_time - s->connecttime < 2.0)
			{
				// yes, so send a duplicate reply
				SZ_Clear(&net_message);
				// save space for the header, filled in later
				MSG_WriteLong(&net_message, 0);
				MSG_WriteByte(&net_message, CCREP_ACCEPT);
				dfunc.GetSocketAddr(s->socket, &newaddr);
				MSG_WriteLong(&net_message, dfunc.GetSocketPort(&newaddr));
				*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
				dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
				SZ_Clear(&net_message);
				return nullptr;
			}
			// it's somebody coming back in from a crash/disconnect
			// so close the old qsocket and let their retry get them back in
			NET_Close(s);
			return nullptr;
		}
	}

	// allocate a QSocket
	auto sock = NET_NewQSocket();
	if (sock == nullptr)
	{
		// no room; try to let him know
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_REJECT);
		MSG_WriteString(&net_message, "Server is full.\n");
		*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
		dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);
		return nullptr;
	}

	// allocate a network socket
	const auto newsock = dfunc.OpenSocket(0);
	if (newsock == -1)
	{
		NET_FreeQSocket(sock);
		return nullptr;
	}

	// connect to the client
	if (dfunc.Connect(newsock, &clientaddr) == -1)
	{
		dfunc.CloseSocket(newsock);
		NET_FreeQSocket(sock);
		return nullptr;
	}

	// everything is allocated, just fill in the details	
	sock->socket    = newsock;
	sock->landriver = net_landriverlevel;
	sock->addr      = clientaddr;
	Q_strcpy(sock->address, dfunc.AddrToString(&clientaddr));

	// send him back the info about the server connection he has been allocated
	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREP_ACCEPT);
	dfunc.GetSocketAddr(newsock, &newaddr);
	MSG_WriteLong(&net_message, dfunc.GetSocketPort(&newaddr));
	//	MSG_WriteString(&net_message, dfunc.AddrToString(&newaddr));
	*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
	dfunc.Write(acceptsock, net_message.data, net_message.cursize, &clientaddr);
	SZ_Clear(&net_message);

	return sock;
}

qsocket_t* Datagram_CheckNewConnections()
{
	qsocket_t* ret = nullptr;

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
		if (net_landrivers[net_landriverlevel].initialized)
			if ((ret = _Datagram_CheckNewConnections()) != nullptr)
				break;
	return ret;
}


static void _Datagram_SearchForHosts(qboolean xmit)
{
	int              ret;
	int              n;
	struct qsockaddr readaddr;
	struct qsockaddr myaddr;

	dfunc.GetSocketAddr(dfunc.controlSock, &myaddr);
	if (xmit)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
		MSG_WriteString(&net_message, "QUAKE");
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
		dfunc.Broadcast(dfunc.controlSock, net_message.data, net_message.cursize);
		SZ_Clear(&net_message);
	}

	while ((ret = dfunc.Read(dfunc.controlSock, net_message.data, net_message.maxsize, &readaddr)) > 0)
	{
		if (ret < sizeof(int))
			continue;
		net_message.cursize = ret;

		// don't answer our own query
		if (dfunc.AddrCompare(&readaddr, &myaddr) >= 0)
			continue;

		// is the cache full?
		if (hostCacheCount == HOSTCACHESIZE)
			continue;

		MSG_BeginReading();
		const auto control = BigLong(*reinterpret_cast<int *>(net_message.data));
		MSG_ReadLong();
		if (control == -1)
			continue;
		if ((control & ~NETFLAG_LENGTH_MASK) != NETFLAG_CTL)
			continue;
		if ((control & NETFLAG_LENGTH_MASK) != ret)
			continue;

		if (MSG_ReadByte() != CCREP_SERVER_INFO)
			continue;

		dfunc.GetAddrFromName(MSG_ReadString(), &readaddr);
		// search the cache for this server
		for (n = 0; n < hostCacheCount; n++)
			if (dfunc.AddrCompare(&readaddr, &hostcache[n].addr) == 0)
				break;

		// is it already there?
		if (n < hostCacheCount)
			continue;

		// add it
		hostCacheCount++;
		Q_strcpy(hostcache[n].name, MSG_ReadString());
		Q_strcpy(hostcache[n].map, MSG_ReadString());
		hostcache[n].users    = MSG_ReadByte();
		hostcache[n].maxusers = MSG_ReadByte();
		if (MSG_ReadByte() != NET_PROTOCOL_VERSION)
		{
			Q_strcpy(hostcache[n].cname, hostcache[n].name);
			hostcache[n].cname[14] = 0;
			Q_strcpy(hostcache[n].name, "*");
			Q_strcat(hostcache[n].name, hostcache[n].cname);
		}
		Q_memcpy(&hostcache[n].addr, &readaddr, sizeof(struct qsockaddr));
		hostcache[n].driver  = net_driverlevel;
		hostcache[n].ldriver = net_landriverlevel;
		Q_strcpy(hostcache[n].cname, dfunc.AddrToString(&readaddr));

		// check for a name conflict
		for (auto i = 0; i < hostCacheCount; i++)
		{
			if (i == n)
				continue;
			if (Q_strcasecmp(hostcache[n].name, hostcache[i].name) == 0)
			{
				i = Q_strlen(hostcache[n].name);
				if (i < 15 && hostcache[n].name[i - 1] > '8')
				{
					hostcache[n].name[i]     = '0';
					hostcache[n].name[i + 1] = 0;
				}
				else
					hostcache[n].name[i - 1]++;
				i = -1;
			}
		}
	}
}

void Datagram_SearchForHosts(qboolean xmit)
{
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (hostCacheCount == HOSTCACHESIZE)
			break;
		if (net_landrivers[net_landriverlevel].initialized)
			_Datagram_SearchForHosts(xmit);
	}
}


static qsocket_t* _Datagram_Connect(char* host)
{
	struct qsockaddr sendaddr;
	struct qsockaddr readaddr;
	auto             ret = 0;
	char*            reason;

	// see if we can resolve the host name
	if (dfunc.GetAddrFromName(host, &sendaddr) == -1)
		return nullptr;

	const auto newsock = dfunc.OpenSocket(0);
	if (newsock == -1)
		return nullptr;

	auto sock = NET_NewQSocket();
	if (sock == nullptr)
		goto ErrorReturn2;
	sock->socket    = newsock;
	sock->landriver = net_landriverlevel;

	// connect to the host
	if (dfunc.Connect(newsock, &sendaddr) == -1)
		goto ErrorReturn;

	// send the connection request
	Con_Printf("trying...\n");
	SCR_UpdateScreen();
	auto start_time = net_time;

	for (auto reps = 0; reps < 3; reps++)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_CONNECT);
		MSG_WriteString(&net_message, "QUAKE");
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*reinterpret_cast<int *>(net_message.data) = BigLong(NETFLAG_CTL | net_message.cursize & NETFLAG_LENGTH_MASK);
		dfunc.Write(newsock, net_message.data, net_message.cursize, &sendaddr);
		SZ_Clear(&net_message);
		do
		{
			ret = dfunc.Read(newsock, net_message.data, net_message.maxsize, &readaddr);
			// if we got something, validate it
			if (ret > 0)
			{
				// is it from the right place?
				if (sfunc.AddrCompare(&readaddr, &sendaddr) != 0)
				{
#ifdef DEBUG
					Con_Printf("wrong reply address\n");
					Con_Printf("Expected: %s\n", StrAddr (&sendaddr));
					Con_Printf("Received: %s\n", StrAddr (&readaddr));
					SCR_UpdateScreen ();
#endif
					ret = 0;
					continue;
				}

				if (ret < sizeof(int))
				{
					ret = 0;
					continue;
				}

				net_message.cursize = ret;
				MSG_BeginReading();

				const auto control = BigLong(*reinterpret_cast<int *>(net_message.data));
				MSG_ReadLong();
				if (control == -1)
				{
					ret = 0;
					continue;
				}
				if ((control & ~NETFLAG_LENGTH_MASK) != NETFLAG_CTL)
				{
					ret = 0;
					continue;
				}
				if ((control & NETFLAG_LENGTH_MASK) != ret)
				{
					ret = 0;
				}
			}
		}
		while (ret == 0 && SetNetTime() - start_time < 2.5);
		if (ret)
			break;
		Con_Printf("still trying...\n");
		SCR_UpdateScreen();
		start_time = SetNetTime();
	}

	if (ret == 0)
	{
		reason = "No Response";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	if (ret == -1)
	{
		reason = "Network Error";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	ret = MSG_ReadByte();
	if (ret == CCREP_REJECT)
	{
		reason = MSG_ReadString();
		Con_Printf(reason);
		Q_strncpy(m_return_reason, reason, 31);
		goto ErrorReturn;
	}

	if (ret == CCREP_ACCEPT)
	{
		Q_memcpy(&sock->addr, &sendaddr, sizeof(struct qsockaddr));
		dfunc.SetSocketPort(&sock->addr, MSG_ReadLong());
	}
	else
	{
		reason = "Bad Response";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	dfunc.GetNameFromAddr(&sendaddr, sock->address);

	Con_Printf("Connection accepted\n");
	sock->lastMessageTime = SetNetTime();

	// switch the connection to the specified address
	if (dfunc.Connect(newsock, &sock->addr) == -1)
	{
		reason = "Connect to Game failed";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	m_return_onerror = qfalse;
	return sock;

ErrorReturn:
	NET_FreeQSocket(sock);
ErrorReturn2:
	dfunc.CloseSocket(newsock);
	if (m_return_onerror)
	{
		key_dest         = keydest_t::key_menu;
		m_state          = m_return_state;
		m_return_onerror = qfalse;
	}
	return nullptr;
}

qsocket_t* Datagram_Connect(char* host)
{
	qsocket_t* ret = nullptr;

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
		if (net_landrivers[net_landriverlevel].initialized)
			if ((ret = _Datagram_Connect(host)) != nullptr)
				break;
	return ret;
}
