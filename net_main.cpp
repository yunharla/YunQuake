#include "quakedef.h"
#include "net_vcr.h"

qsocket_t* net_activeSockets = nullptr;
qsocket_t* net_freeSockets = nullptr;
int net_numsockets = 0;

qboolean serialAvailable = qfalse;
qboolean ipxAvailable = qfalse;
qboolean tcpipAvailable = qfalse;

int net_hostport;
int DEFAULTnet_hostport = 26000;

char my_ipx_address[NET_NAMELEN];
char my_tcpip_address[NET_NAMELEN];

void (*GetComPortConfig)(int portNumber, int* port, int* irq, int* baud, qboolean* useModem);
void (*SetComPortConfig)(int portNumber, int port, int irq, int baud, qboolean useModem);
void (*GetModemConfig)(int portNumber, char* dialType, char* clear, char* init, char* hangup);
void (*SetModemConfig)(int portNumber, char* dialType, char* clear, char* init, char* hangup);

static qboolean listening = qfalse;

qboolean slistInProgress = qfalse;
qboolean slistSilent = qfalse;
qboolean slistLocal = qtrue;
static double slistStartTime;
static int slistLastShown;

static void Slist_Send();
static void Slist_Poll();
PollProcedure slistSendProcedure = {nullptr, 0.0, Slist_Send};
PollProcedure slistPollProcedure = {nullptr, 0.0, Slist_Poll};


sizebuf_t net_message;
int net_activeconnections = 0;

int messagesSent = 0;
int messagesReceived = 0;
int unreliableMessagesSent = 0;
int unreliableMessagesReceived = 0;

cvar_t net_messagetimeout = {"net_messagetimeout","300"};
cvar_t hostname = {"hostname", "UNNAMED"};

qboolean configRestored = qfalse;
cvar_t config_com_port = {"_config_com_port", "0x3f8", qtrue};
cvar_t config_com_irq = {"_config_com_irq", "4", qtrue};
cvar_t config_com_baud = {"_config_com_baud", "57600", qtrue};
cvar_t config_com_modem = {"_config_com_modem", "1", qtrue};
cvar_t config_modem_dialtype = {"_config_modem_dialtype", "T", qtrue};
cvar_t config_modem_clear = {"_config_modem_clear", "ATZ", qtrue};
cvar_t config_modem_init = {"_config_modem_init", "", qtrue};
cvar_t config_modem_hangup = {"_config_modem_hangup", "AT H", qtrue};

#ifdef IDGODS
cvar_t	idgods = {"idgods", "0"};
#endif

int vcrFile = -1;
qboolean recording = qfalse;

// these two macros are to make the code more readable
#define sfunc	net_drivers[sock->driver]
#define dfunc	net_drivers[net_driverlevel]

int net_driverlevel;


double net_time;

double SetNetTime()
{
	net_time = Sys_FloatTime();
	return net_time;
}


/*
===================
NET_NewQSocket

Called by drivers when a new communications endpoint is required
The sequence and buffer fields will be filled in properly
===================
*/
qsocket_t* NET_NewQSocket()
{
	if (net_freeSockets == nullptr)
		return nullptr;

	if (net_activeconnections >= svs.maxclients)
		return nullptr;

	// get one from free list
	auto sock = net_freeSockets;
	net_freeSockets = sock->next;

	// add it to active list
	sock->next = net_activeSockets;
	net_activeSockets = sock;

	sock->disconnected = qfalse;
	sock->connecttime = net_time;
	Q_strcpy(sock->address, "UNSET ADDRESS");
	sock->driver = net_driverlevel;
	sock->socket = 0;
	sock->driverdata = nullptr;
	sock->canSend = qtrue;
	sock->sendNext = qfalse;
	sock->lastMessageTime = net_time;
	sock->ackSequence = 0;
	sock->sendSequence = 0;
	sock->unreliableSendSequence = 0;
	sock->sendMessageLength = 0;
	sock->receiveSequence = 0;
	sock->unreliableReceiveSequence = 0;
	sock->receiveMessageLength = 0;

	return sock;
}


void NET_FreeQSocket(qsocket_t* sock)
{
	qsocket_t* s;

	// remove it from active list
	if (sock == net_activeSockets)
		net_activeSockets = net_activeSockets->next;
	else
	{
		for (s = net_activeSockets; s; s = s->next)
			if (s->next == sock)
			{
				s->next = sock->next;
				break;
			}
		if (!s)
			Sys_Error("NET_FreeQSocket: not active\n");
	}

	// add it to free list
	sock->next = net_freeSockets;
	net_freeSockets = sock;
	sock->disconnected = qtrue;
}


static void NET_Listen_f()
{
	if (Cmd_Argc() != 2)
	{
		Con_Printf("\"listen\" is \"%u\"\n", listening ? 1 : 0);
		return;
	}

	listening = Q_atoi(Cmd_Argv(1)) ? qtrue : qfalse;

	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == qfalse)
			continue;
		dfunc.Listen(listening);
	}
}


static void MaxPlayers_f()
{
	if (Cmd_Argc() != 2)
	{
		Con_Printf("\"maxplayers\" is \"%u\"\n", svs.maxclients);
		return;
	}

	if (sv.active)
	{
		Con_Printf("maxplayers can not be changed while a server is running.\n");
		return;
	}

	auto n = Q_atoi(Cmd_Argv(1));
	if (n < 1)
		n = 1;
	if (n > svs.maxclientslimit)
	{
		n = svs.maxclientslimit;
		Con_Printf("\"maxplayers\" set to \"%u\"\n", n);
	}

	if (n == 1 && listening)
		Cbuf_AddText("listen 0\n");

	if (n > 1 && !listening)
		Cbuf_AddText("listen 1\n");

	svs.maxclients = n;
	if (n == 1)
		Cvar_Set("deathmatch", "0");
	else
		Cvar_Set("deathmatch", "1");
}


static void NET_Port_f()
{
	if (Cmd_Argc() != 2)
	{
		Con_Printf("\"port\" is \"%u\"\n", net_hostport);
		return;
	}

	auto n = Q_atoi(Cmd_Argv(1));
	if (n < 1 || n > 65534)
	{
		Con_Printf("Bad value, must be between 1 and 65534\n");
		return;
	}

	DEFAULTnet_hostport = n;
	net_hostport = n;

	if (listening)
	{
		// force a change to the new port
		Cbuf_AddText("listen 0\n");
		Cbuf_AddText("listen 1\n");
	}
}


static void PrintSlistHeader()
{
	Con_Printf("Server          Map             Users\n");
	Con_Printf("--------------- --------------- -----\n");
	slistLastShown = 0;
}


static void PrintSlist()
{
	int n;

	for (n = slistLastShown; n < hostCacheCount; n++)
	{
		if (hostcache[n].maxusers)
			Con_Printf("%-15.15s %-15.15s %2u/%2u\n", hostcache[n].name, hostcache[n].map, hostcache[n].users, hostcache[n].maxusers);
		else
			Con_Printf("%-15.15s %-15.15s\n", hostcache[n].name, hostcache[n].map);
	}
	slistLastShown = n;
}


static void PrintSlistTrailer()
{
	if (hostCacheCount)
		Con_Printf("== end list ==\n\n");
	else
		Con_Printf("No Quake servers found.\n\n");
}


void NET_Slist_f()
{
	if (slistInProgress)
		return;

	if (! slistSilent)
	{
		Con_Printf("Looking for Quake servers...\n");
		PrintSlistHeader();
	}

	slistInProgress = qtrue;
	slistStartTime = Sys_FloatTime();

	SchedulePollProcedure(&slistSendProcedure, 0.0);
	SchedulePollProcedure(&slistPollProcedure, 0.1);

	hostCacheCount = 0;
}


static void Slist_Send()
{
	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (!slistLocal && net_driverlevel == 0)
			continue;
		if (net_drivers[net_driverlevel].initialized == qfalse)
			continue;
		dfunc.SearchForHosts(qtrue);
	}

	if (Sys_FloatTime() - slistStartTime < 0.5)
		SchedulePollProcedure(&slistSendProcedure, 0.75);
}


static void Slist_Poll()
{
	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (!slistLocal && net_driverlevel == 0)
			continue;
		if (net_drivers[net_driverlevel].initialized == qfalse)
			continue;
		dfunc.SearchForHosts(qfalse);
	}

	if (! slistSilent)
		PrintSlist();

	if (Sys_FloatTime() - slistStartTime < 1.5)
	{
		SchedulePollProcedure(&slistPollProcedure, 0.1);
		return;
	}

	if (! slistSilent)
		PrintSlistTrailer();
	slistInProgress = qfalse;
	slistSilent = qfalse;
	slistLocal = qtrue;
}


/*
===================
NET_Connect
===================
*/

int hostCacheCount = 0;
hostcache_t hostcache[HOSTCACHESIZE];

qsocket_t* NET_Connect(char* host)
{
	int n;
	auto numdrivers = net_numdrivers;

	SetNetTime();

	if (host && *host == 0)
		host = nullptr;

	if (host)
	{
		if (Q_strcasecmp(host, "local") == 0)
		{
			numdrivers = 1;
			goto JustDoIt;
		}

		if (hostCacheCount)
		{
			for (n = 0; n < hostCacheCount; n++)
				if (Q_strcasecmp(host, hostcache[n].name) == 0)
				{
					host = hostcache[n].cname;
					break;
				}
			if (n < hostCacheCount)
				goto JustDoIt;
		}
	}

	slistSilent = host ? qtrue : qfalse;
	NET_Slist_f();

	while (slistInProgress)
		NET_Poll();

	if (host == nullptr)
	{
		if (hostCacheCount != 1)
			return nullptr;
		host = hostcache[0].cname;
		Con_Printf("Connecting to...\n%s @ %s\n\n", hostcache[0].name, host);
	}

	if (hostCacheCount)
		for (n = 0; n < hostCacheCount; n++)
			if (Q_strcasecmp(host, hostcache[n].name) == 0)
			{
				host = hostcache[n].cname;
				break;
			}

JustDoIt:
	for (net_driverlevel = 0; net_driverlevel < numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == qfalse)
			continue;
		auto ret = dfunc.Connect(host);
		if (ret)
			return ret;
	}

	if (host)
	{
		Con_Printf("\n");
		PrintSlistHeader();
		PrintSlist();
		PrintSlistTrailer();
	}

	return nullptr;
}


/*
===================
NET_CheckNewConnections
===================
*/

struct
{
	double time;
	int op;
	long session;
} vcrConnect;

qsocket_t* NET_CheckNewConnections()
{
	SetNetTime();

	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == qfalse)
			continue;
		if (net_driverlevel && listening == qfalse)
			continue;
		auto ret = dfunc.CheckNewConnections();
		if (ret)
		{
			if (recording)
			{
				vcrConnect.time = host_time;
				vcrConnect.op = VCR_OP_CONNECT;
				vcrConnect.session = reinterpret_cast<long>(ret);
				Sys_FileWrite(vcrFile, &vcrConnect, sizeof vcrConnect);
				Sys_FileWrite(vcrFile, ret->address, NET_NAMELEN);
			}
			return ret;
		}
	}

	if (recording)
	{
		vcrConnect.time = host_time;
		vcrConnect.op = VCR_OP_CONNECT;
		vcrConnect.session = 0;
		Sys_FileWrite(vcrFile, &vcrConnect, sizeof vcrConnect);
	}

	return nullptr;
}

/*
===================
NET_Close
===================
*/
void NET_Close(qsocket_t* sock)
{
	if (!sock)
		return;

	if (sock->disconnected)
		return;

	SetNetTime();

	// call the driver_Close function
	sfunc.Close(sock);

	NET_FreeQSocket(sock);
}


/*
=================
NET_GetMessage

If there is a complete message, return it in net_message

returns 0 if no data is waiting
returns 1 if a message was received
returns -1 if connection is invalid
=================
*/

struct
{
	double time;
	int op;
	long session;
	int ret;
	int len;
} vcrGetMessage;

extern void PrintStats(qsocket_t* s);

int NET_GetMessage(qsocket_t* sock)
{
	if (!sock)
		return -1;

	if (sock->disconnected)
	{
		Con_Printf("NET_GetMessage: disconnected socket\n");
		return -1;
	}

	SetNetTime();

	auto ret = sfunc.QGetMessage(sock);

	// see if this connection has timed out
	if (ret == 0 && sock->driver)
	{
		if (net_time - sock->lastMessageTime > net_messagetimeout.value)
		{
			NET_Close(sock);
			return -1;
		}
	}


	if (ret > 0)
	{
		if (sock->driver)
		{
			sock->lastMessageTime = net_time;
			if (ret == 1)
				messagesReceived++;
			else if (ret == 2)
				unreliableMessagesReceived++;
		}

		if (recording)
		{
			vcrGetMessage.time = host_time;
			vcrGetMessage.op = VCR_OP_GETMESSAGE;
			vcrGetMessage.session = reinterpret_cast<long>(sock);
			vcrGetMessage.ret = ret;
			vcrGetMessage.len = net_message.cursize;
			Sys_FileWrite(vcrFile, &vcrGetMessage, 24);
			Sys_FileWrite(vcrFile, net_message.data, net_message.cursize);
		}
	}
	else
	{
		if (recording)
		{
			vcrGetMessage.time = host_time;
			vcrGetMessage.op = VCR_OP_GETMESSAGE;
			vcrGetMessage.session = reinterpret_cast<long>(sock);
			vcrGetMessage.ret = ret;
			Sys_FileWrite(vcrFile, &vcrGetMessage, 20);
		}
	}

	return ret;
}


/*
==================
NET_SendMessage

Try to send a complete length+message unit over the reliable stream.
returns 0 if the message cannot be delivered reliably, but the connection
		is still considered valid
returns 1 if the message was sent properly
returns -1 if the connection died
==================
*/
struct
{
	double time;
	int op;
	long session;
	int r;
} vcrSendMessage;

int NET_SendMessage(qsocket_t* sock, sizebuf_t* data)
{
	if (!sock)
		return -1;

	if (sock->disconnected)
	{
		Con_Printf("NET_SendMessage: disconnected socket\n");
		return -1;
	}

	SetNetTime();
	auto r = sfunc.QSendMessage(sock, data);
	if (r == 1 && sock->driver)
		messagesSent++;

	if (recording)
	{
		vcrSendMessage.time = host_time;
		vcrSendMessage.op = VCR_OP_SENDMESSAGE;
		vcrSendMessage.session = reinterpret_cast<long>(sock);
		vcrSendMessage.r = r;
		Sys_FileWrite(vcrFile, &vcrSendMessage, 20);
	}

	return r;
}


int NET_SendUnreliableMessage(qsocket_t* sock, sizebuf_t* data)
{
	if (!sock)
		return -1;

	if (sock->disconnected)
	{
		Con_Printf("NET_SendMessage: disconnected socket\n");
		return -1;
	}

	SetNetTime();
	auto r = sfunc.SendUnreliableMessage(sock, data);
	if (r == 1 && sock->driver)
		unreliableMessagesSent++;

	if (recording)
	{
		vcrSendMessage.time = host_time;
		vcrSendMessage.op = VCR_OP_SENDMESSAGE;
		vcrSendMessage.session = reinterpret_cast<long>(sock);
		vcrSendMessage.r = r;
		Sys_FileWrite(vcrFile, &vcrSendMessage, 20);
	}

	return r;
}


/*
==================
NET_CanSendMessage

Returns qtrue or qfalse if the given qsocket can currently accept a
message to be transmitted.
==================
*/
qboolean NET_CanSendMessage(qsocket_t* sock)
{
	if (!sock)
		return qfalse;

	if (sock->disconnected)
		return qfalse;

	SetNetTime();

	int r = sfunc.CanSendMessage(sock);

	if (recording)
	{
		vcrSendMessage.time = host_time;
		vcrSendMessage.op = VCR_OP_CANSENDMESSAGE;
		vcrSendMessage.session = reinterpret_cast<long>(sock);
		vcrSendMessage.r = r;
		Sys_FileWrite(vcrFile, &vcrSendMessage, 20);
	}

	return r;
}


int NET_SendToAll(sizebuf_t* data, int blocktime)
{
	int i;
	auto count = 0;
	qboolean state1 [MAX_SCOREBOARD];
	qboolean state2 [MAX_SCOREBOARD];

	for (i = 0 , host_client = svs.clients; i < svs.maxclients; i++ , host_client++)
	{
		if (!host_client->netconnection)
			continue;
		if (host_client->active)
		{
			if (host_client->netconnection->driver == 0)
			{
				NET_SendMessage(host_client->netconnection, data);
				state1[i] = qtrue;
				state2[i] = qtrue;
				continue;
			}
			count++;
			state1[i] = qfalse;
			state2[i] = qfalse;
		}
		else
		{
			state1[i] = qtrue;
			state2[i] = qtrue;
		}
	}

	auto start = Sys_FloatTime();
	while (count)
	{
		count = 0;
		for (i = 0 , host_client = svs.clients; i < svs.maxclients; i++ , host_client++)
		{
			if (! state1[i])
			{
				if (NET_CanSendMessage(host_client->netconnection))
				{
					state1[i] = qtrue;
					NET_SendMessage(host_client->netconnection, data);
				}
				else
				{
					NET_GetMessage(host_client->netconnection);
				}
				count++;
				continue;
			}

			if (! state2[i])
			{
				if (NET_CanSendMessage(host_client->netconnection))
				{
					state2[i] = qtrue;
				}
				else
				{
					NET_GetMessage(host_client->netconnection);
				}
				count++;
			}
		}
		if (Sys_FloatTime() - start > blocktime)
			break;
	}
	return count;
}


//=============================================================================

/*
====================
NET_Init
====================
*/

void NET_Init()
{
	if (COM_CheckParm("-playback"))
	{
		net_numdrivers = 1;
		net_drivers[0].Init = VCR_Init;
	}

	if (COM_CheckParm("-record"))
		recording = qtrue;

	auto i = COM_CheckParm("-port");
	if (!i)
		i = COM_CheckParm("-udpport");
	if (!i)
		i = COM_CheckParm("-ipxport");

	if (i)
	{
		if (i < com_argc - 1)
			DEFAULTnet_hostport = Q_atoi(com_argv[i + 1]);
		else
			Sys_Error("NET_Init: you must specify a number after -port");
	}
	net_hostport = DEFAULTnet_hostport;

	if (COM_CheckParm("-listen") || cls.state == cactive_t::ca_dedicated)
		listening = qtrue;
	net_numsockets = svs.maxclientslimit;
	if (cls.state != cactive_t::ca_dedicated)
		net_numsockets++;

	SetNetTime();

	for (i = 0; i < net_numsockets; i++)
	{
		auto s = static_cast<qsocket_t *>(Hunk_AllocName(sizeof(qsocket_t), "qsocket"));
		s->next = net_freeSockets;
		net_freeSockets = s;
		s->disconnected = qtrue;
	}

	// allocate space for network message buffer
	SZ_Alloc(&net_message, NET_MAXMESSAGE);

	Cvar_RegisterVariable(&net_messagetimeout);
	Cvar_RegisterVariable(&hostname);
	Cvar_RegisterVariable(&config_com_port);
	Cvar_RegisterVariable(&config_com_irq);
	Cvar_RegisterVariable(&config_com_baud);
	Cvar_RegisterVariable(&config_com_modem);
	Cvar_RegisterVariable(&config_modem_dialtype);
	Cvar_RegisterVariable(&config_modem_clear);
	Cvar_RegisterVariable(&config_modem_init);
	Cvar_RegisterVariable(&config_modem_hangup);
#ifdef IDGODS
	Cvar_RegisterVariable (&idgods);
#endif

	Cmd_AddCommand("slist", NET_Slist_f);
	Cmd_AddCommand("listen", NET_Listen_f);
	Cmd_AddCommand("maxplayers", MaxPlayers_f);
	Cmd_AddCommand("port", NET_Port_f);

	// initialize all the drivers
	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		auto controlSocket = net_drivers[net_driverlevel].Init();
		if (controlSocket == -1)
			continue;
		net_drivers[net_driverlevel].initialized = qtrue;
		net_drivers[net_driverlevel].controlSock = controlSocket;
		if (listening)
			net_drivers[net_driverlevel].Listen(qtrue);
	}

	if (*my_ipx_address)
		Con_DPrintf("IPX address %s\n", my_ipx_address);
	if (*my_tcpip_address)
		Con_DPrintf("TCP/IP address %s\n", my_tcpip_address);
}

/*
====================
NET_Shutdown
====================
*/

void NET_Shutdown()
{
	SetNetTime();

	for (auto sock = net_activeSockets; sock; sock = sock->next)
		NET_Close(sock);

	//
	// shutdown the drivers
	//
	for (net_driverlevel = 0; net_driverlevel < net_numdrivers; net_driverlevel++)
	{
		if (net_drivers[net_driverlevel].initialized == qtrue)
		{
			net_drivers[net_driverlevel].Shutdown();
			net_drivers[net_driverlevel].initialized = qfalse;
		}
	}

	if (vcrFile != -1)
	{
		Con_Printf("Closing vcrfile.\n");
		Sys_FileClose(vcrFile);
	}
}


static PollProcedure* pollProcedureList = nullptr;

void NET_Poll()
{
	qboolean useModem;

	if (!configRestored)
	{
		if (serialAvailable)
		{
			if (config_com_modem.value == 1.0)
				useModem = qtrue;
			else
				useModem = qfalse;
			SetComPortConfig(0, static_cast<int>(config_com_port.value), static_cast<int>(config_com_irq.value), static_cast<int>(config_com_baud.value), useModem);
			SetModemConfig(0, config_modem_dialtype.string, config_modem_clear.string, config_modem_init.string, config_modem_hangup.string);
		}
		configRestored = qtrue;
	}

	SetNetTime();

	for (auto pp = pollProcedureList; pp; pp = pp->next)
	{
		if (pp->nextTime > net_time)
			break;
		pollProcedureList = pp->next;
		reinterpret_cast<void(*)(void*)>(pp->procedure)(pp->arg);
	}
}


void SchedulePollProcedure(PollProcedure* proc, double timeOffset)
{
	PollProcedure *pp, *prev;

	proc->nextTime = Sys_FloatTime() + timeOffset;
	for (pp = pollProcedureList , prev = nullptr; pp; pp = pp->next)
	{
		if (pp->nextTime >= proc->nextTime)
			break;
		prev = pp;
	}

	if (prev == nullptr)
	{
		proc->next = pollProcedureList;
		pollProcedureList = proc;
		return;
	}

	proc->next = pp;
	prev->next = proc;
}


#ifdef IDGODS
#define IDNET	0xc0f62800

qboolean IsID(qsockaddr *addr)
{
	if (idgods.value == 0.0)
		return qfalse;

	if (addr->sa_family != 2)
		return qfalse;

	if ((BigLong(*(int *)&addr->sa_data[2]) & 0xffffff00) == IDNET)
		return qtrue;
	return qfalse;
}
#endif
