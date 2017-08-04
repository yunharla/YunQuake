/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"

/*

A server can allways be started, even if the system started out as a client
to a remote system.

A client can NOT be started if the system started as a dedicated server.

Memory is cleared / released when a server or client begins, not when they end.

*/

quakeparms_t host_parms;

bool host_initialized; // true if into command execution

double host_frametime;
double host_time;
double realtime; // without any filtering or bounding
double oldrealtime; // last frame run
int host_framecount;

int host_hunklevel;

int minimum_memory;

client_t* host_client; // current client

jmp_buf host_abortserver;

byte* host_colormap;

cvar_t host_framerate = {"host_framerate","0"}; // set for slow motion
cvar_t host_speeds = {"host_speeds","0"}; // set for running times
cvar_t host_maxfps = {"host_maxfps", "72", true}; //johnfitz
cvar_t host_timescale = {"host_timescale", "0"}; //johnfitz
cvar_t max_edicts = {"max_edicts", "1024", true}; //johnfitz

cvar_t sys_ticrate = {"sys_ticrate","0.05"}; // dedicated server
cvar_t serverprofile = {"serverprofile","0"};

cvar_t fraglimit = {"fraglimit","0",false,true};
cvar_t timelimit = {"timelimit","0",false,true};
cvar_t teamplay = {"teamplay","0",false,true};
cvar_t samelevel = {"samelevel","0"};
cvar_t noexit = {"noexit","0",false,true};
cvar_t skill = {"skill","1"}; // 0 - 3
cvar_t deathmatch = {"deathmatch","0"}; // 0, 1, or 2
cvar_t coop = {"coop","0"}; // 0 or 1

cvar_t pausable = {"pausable","1"};

cvar_t developer = {"developer","0"};

cvar_t temp1 = {"temp1","0"};

cvar_t devstats = {"devstats","0"}; //johnfitz -- track developer statistics that vary every frame

/*
================
Max_Edicts_f -- johnfitz
================
*/
void Max_Edicts_f()
{
	static float oldval = 1024; //must match the default value for max_edicts

	//TODO: clamp it here?

	if (max_edicts.value == oldval)
		return;

	if (cls.state == cactive_t::ca_connected || sv.active)
		Con_Printf("changes will not take effect until the next level load.\n");

	oldval = max_edicts.value;
}

/*
================
Host_EndGame
================
*/
void Host_EndGame(char* message, ...)
{
	va_list argptr;
	char string[1024];

	va_start (argptr,message);
	vsprintf(string, message, argptr);
	va_end (argptr);
	Con_DPrintf("Host_EndGame: %s\n", string);

	if (sv.active)
		Host_ShutdownServer(false);

	if (cls.state == cactive_t::ca_dedicated)
		Sys_Error("Host_EndGame: %s\n", string); // dedicated servers exit

	if (cls.demonum != -1)
		CL_NextDemo();
	else
		CL_Disconnect();

	longjmp(host_abortserver, 1);
}

/*
================
Host_Error

This shuts down both the client and server
================
*/
void Host_Error(char* error, ...)
{
	va_list argptr;
	char string[1024];
	static bool inerror = false;

	if (inerror)
		Sys_Error("Host_Error: recursively entered");
	inerror = true;

	SCR_EndLoadingPlaque(); // reenable screen updates

	va_start (argptr,error);
	vsprintf(string, error, argptr);
	va_end (argptr);
	Con_Printf("Host_Error: %s\n", string);

	if (sv.active)
		Host_ShutdownServer(false);

	if (cls.state == cactive_t::ca_dedicated)
		Sys_Error("Host_Error: %s\n", string); // dedicated servers exit

	CL_Disconnect();
	cls.demonum = -1;
	cl.intermission = 0; //johnfitz -- for errors during intermissions (changelevel with no map found, etc.)

	inerror = false;

	longjmp(host_abortserver, 1);
}

/*
================
Host_FindMaxClients
================
*/
void Host_FindMaxClients()
{
	svs.maxclients = 1;

	auto i = COM_CheckParm("-dedicated");
	if (i)
	{
		cls.state = cactive_t::ca_dedicated;
		if (i != (com_argc - 1))
		{
			svs.maxclients = Q_atoi(com_argv[i + 1]);
		}
		else
			svs.maxclients = 8;
	}
	else
		cls.state = cactive_t::ca_disconnected;

	i = COM_CheckParm("-listen");
	if (i)
	{
		if (cls.state == cactive_t::ca_dedicated)
			Sys_Error("Only one of -dedicated or -listen can be specified");
		if (i != (com_argc - 1))
			svs.maxclients = Q_atoi(com_argv[i + 1]);
		else
			svs.maxclients = 8;
	}
	if (svs.maxclients < 1)
		svs.maxclients = 8;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	svs.maxclientslimit = svs.maxclients;
	if (svs.maxclientslimit < 4)
		svs.maxclientslimit = 4;
	svs.clients = reinterpret_cast<client_t*>(Hunk_AllocName(svs.maxclientslimit * sizeof(client_t), "clients"));

	if (svs.maxclients > 1)
		Cvar_SetValue("deathmatch", 1.0);
	else
		Cvar_SetValue("deathmatch", 0.0);
}

/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal()
{
	Host_InitCommands();

	Cvar_RegisterVariable(&host_framerate, nullptr);
	Cvar_RegisterVariable(&host_speeds, nullptr);
	Cvar_RegisterVariable(&host_maxfps, nullptr); //johnfitz
	Cvar_RegisterVariable(&host_timescale, nullptr); //johnfitz

	Cvar_RegisterVariable(&max_edicts, Max_Edicts_f); //johnfitz
	Cvar_RegisterVariable(&devstats, nullptr); //johnfitz

	Cvar_RegisterVariable(&sys_ticrate, nullptr);
	Cvar_RegisterVariable(&serverprofile, nullptr);

	Cvar_RegisterVariable(&fraglimit, nullptr);
	Cvar_RegisterVariable(&timelimit, nullptr);
	Cvar_RegisterVariable(&teamplay, nullptr);
	Cvar_RegisterVariable(&samelevel, nullptr);
	Cvar_RegisterVariable(&noexit, nullptr);
	Cvar_RegisterVariable(&skill, nullptr);
	Cvar_RegisterVariable(&developer, nullptr);
	Cvar_RegisterVariable(&deathmatch, nullptr);
	Cvar_RegisterVariable(&coop, nullptr);

	Cvar_RegisterVariable(&pausable, nullptr);

	Cvar_RegisterVariable(&temp1, nullptr);

	Host_FindMaxClients();

	host_time = 1.0; // so a think at time 0 won't get called
}

void VID_SyncCvars();
/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
void Host_WriteConfiguration()
{
	// dedicated servers initialize the host but don't parse and set the
	// config.cfg cvars
	if (host_initialized & !isDedicated)
	{
		auto f = fopen(va("%s/config.cfg", com_gamedir), "w");
		if (!f)
		{
			Con_Printf("Couldn't write config.cfg.\n");
			return;
		}

		VID_SyncCvars(); //johnfitz -- write actual current mode to config file, in case cvars were messed with

		Key_WriteBindings(f);
		Cvar_WriteVariables(f);

		//johnfitz -- extra commands to preserve state
		fprintf(f, "vid_restart\n");
		if (in_mlook.state & 1) fprintf(f, "+mlook\n");
		//johnfitz

		fclose(f);

		//johnfitz -- also save fitzquake.rc
#if 0
		f = fopen (va("%s/fitzquake.rc", GAMENAME), "w"); //always save in id1
		if (!f)
		{
			Con_Printf ("Couldn't write fitzquake.rc.\n");
			return;
		}

		Cvar_WriteVariables (f);
		fprintf (f, "vid_restart\n");
		if (in_mlook.state & 1) fprintf (f, "+mlook\n");

		fclose (f);
#endif
		//johnfitz
	}
}


/*
=================
SV_ClientPrintf

Sends text across to be displayed
FIXME: make this just a stuffed echo?
=================
*/
void SV_ClientPrintf(char* fmt, ...)
{
	va_list argptr;
	char string[1024];

	va_start (argptr,fmt);
	vsprintf(string, fmt, argptr);
	va_end (argptr);

	MSG_WriteByte(&host_client->message, svc_print);
	MSG_WriteString(&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf(char* fmt, ...)
{
	va_list argptr;
	char string[1024];

	va_start (argptr,fmt);
	vsprintf(string, fmt, argptr);
	va_end (argptr);

	for (auto i = 0; i < svs.maxclients; i++)
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte(&svs.clients[i].message, svc_print);
			MSG_WriteString(&svs.clients[i].message, string);
		}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
void Host_ClientCommands(char* fmt, ...)
{
	va_list argptr;
	char string[1024];

	va_start (argptr,fmt);
	vsprintf(string, fmt, argptr);
	va_end (argptr);

	MSG_WriteByte(&host_client->message, svc_stufftext);
	MSG_WriteString(&host_client->message, string);
}

/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient(bool crash)
{
	int i;
	client_t* client;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage(host_client->netconnection))
		{
			MSG_WriteByte(&host_client->message, svc_disconnect);
			NET_SendMessage(host_client->netconnection, &host_client->message);
		}

		if (host_client->edict && host_client->spawned)
		{
			// call the prog function for removing a client
			// this will set the body to a dead frame, among other things
			auto saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
			PR_ExecuteProgram(pr_global_struct->ClientDisconnect);
			pr_global_struct->self = saveSelf;
		}

		Sys_Printf("Client %s removed\n", host_client->name);
	}

	// break the net connection
	NET_Close(host_client->netconnection);
	host_client->netconnection = nullptr;

	// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

	// send notification to all clients
	for (i = 0 , client = svs.clients; i < svs.maxclients; i++ , client++)
	{
		if (!client->active)
			continue;
		MSG_WriteByte(&client->message, svc_updatename);
		MSG_WriteByte(&client->message, host_client - svs.clients);
		MSG_WriteString(&client->message, "");
		MSG_WriteByte(&client->message, svc_updatefrags);
		MSG_WriteByte(&client->message, host_client - svs.clients);
		MSG_WriteShort(&client->message, 0);
		MSG_WriteByte(&client->message, svc_updatecolors);
		MSG_WriteByte(&client->message, host_client - svs.clients);
		MSG_WriteByte(&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer(bool crash)
{
	int i;
	int count;
	sizebuf_t buf;
	char message[4];

	if (!sv.active)
		return;

	sv.active = false;

	// stop all client sounds immediately
	if (cls.state == cactive_t::ca_connected)
		CL_Disconnect();

	// flush any pending messages - like the score!!!
	auto start = Sys_FloatTime();
	do
	{
		count = 0;
		for (i = 0 , host_client = svs.clients; i < svs.maxclients; i++ , host_client++)
		{
			if (host_client->active && host_client->message.cursize)
			{
				if (NET_CanSendMessage(host_client->netconnection))
				{
					NET_SendMessage(host_client->netconnection, &host_client->message);
					SZ_Clear(&host_client->message);
				}
				else
				{
					NET_GetMessage(host_client->netconnection);
					count++;
				}
			}
		}
		if ((Sys_FloatTime() - start) > 3.0)
			break;
	}
	while (count);

	// make sure all the clients know we're disconnecting
	buf.data = reinterpret_cast<byte*>(message);
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte(&buf, svc_disconnect);
	count = NET_SendToAll(&buf, 5);
	if (count)
		Con_Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	for (i = 0 , host_client = svs.clients; i < svs.maxclients; i++ , host_client++)
		if (host_client->active)
			SV_DropClient(crash);

	//
	// clear structures
	//
	memset(&sv, 0, sizeof(sv));
	memset(svs.clients, 0, svs.maxclientslimit * sizeof(client_t));
}


/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.
================
*/
void Host_ClearMemory()
{
	Con_DPrintf("Clearing memory\n");
	D_FlushCaches();
	Mod_ClearAll();
	if (host_hunklevel)
		Hunk_FreeToLowMark(host_hunklevel);

	cls.signon = 0;
	memset(&sv, 0, sizeof(sv));
	memset(&cl, 0, sizeof(cl));
}


//==============================================================================
//
// Host Frame
//
//==============================================================================

/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
bool Host_FilterTime(float time)
{
	realtime += time;

	//johnfitz -- max fps cvar
	float maxfps = CLAMP (10.0, host_maxfps.value, 1000.0);
	if (!cls.timedemo && realtime - oldrealtime < 1.0 / maxfps)
		return false; // framerate is too high
	//johnfitz

	host_frametime = realtime - oldrealtime;
	oldrealtime = realtime;

	//johnfitz -- host_timescale is more intuitive than host_framerate
	if (host_timescale.value > 0)
		host_frametime *= host_timescale.value;
	//johnfitz
	else if (host_framerate.value > 0)
		host_frametime = host_framerate.value;
	else // don't allow really long or short frames
		host_frametime = CLAMP (0.001, host_frametime, 0.1); //johnfitz -- use CLAMP

	return true;
}

/*
===================
Host_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetConsoleCommands()
{
	while (true)
	{
		auto cmd = Sys_ConsoleInput();
		if (!cmd)
			break;
		Cbuf_AddText(cmd);
	}
}

/*
==================
Host_ServerFrame
==================
*/
void Host_ServerFrame()
{
	int i, active; //johnfitz

	// run the world state
	pr_global_struct->frametime = host_frametime;

	// set the time and clear the general datagram
	SV_ClearDatagram();

	// check for new clients
	SV_CheckForNewClients();

	// read client messages
	SV_RunClients();

	// move things around and think
	// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == keydest_t::key_game))
		SV_Physics();

	//johnfitz -- devstats
	if (cls.signon == SIGNONS)
	{
		for (i = 0 , active = 0; i < sv.num_edicts; i++)
		{
			auto ent = EDICT_NUM(i);
			if (!ent->free)
				active++;
		}
		if (active > 600 && dev_peakstats.edicts <= 600)
			Con_Warning("%i edicts exceeds standard limit of 600.\n", active);
		dev_stats.edicts = active;
		dev_peakstats.edicts = max(active, dev_peakstats.edicts);
	}
	//johnfitz

	// send all messages to the clients
	SV_SendClientMessages();
}

void CL_RunParticles();

/*
==================
Host_Frame

Runs all active servers
==================
*/
void _Host_Frame(float time)
{
	static double time1 = 0;
	static double time2 = 0;
	static double time3 = 0;

	if (setjmp(host_abortserver))
		return; // something bad happened, or the server disconnected

	// keep the random time dependent
	rand();

	// decide the simulation time
	if (!Host_FilterTime(time))
		return; // don't run too fast, or packets will flood out

	// get new key events
	Sys_SendKeyEvents();

	// allow mice or other external controllers to add commands
	IN_Commands();

	// process console commands
	Cbuf_Execute();

	NET_Poll();

	// if running the server locally, make intentions now
	if (sv.active)
		CL_SendCmd();

	//-------------------
	//
	// server operations
	//
	//-------------------

	// check for commands typed to the host
	Host_GetConsoleCommands();

	if (sv.active)
		Host_ServerFrame();

	//-------------------
	//
	// client operations
	//
	//-------------------

	// if running the server remotely, send intentions now after
	// the incoming messages have been read
	if (!sv.active)
		CL_SendCmd();

	host_time += host_frametime;

	// fetch results from server
	if (cls.state == cactive_t::ca_connected)
	{
		CL_ReadFromServer();
	}

	// update video
	if (host_speeds.value)
		time1 = Sys_FloatTime();

	SCR_UpdateScreen();

	CL_RunParticles(); //johnfitz -- seperated from rendering

	if (host_speeds.value)
		time2 = Sys_FloatTime();

	// update audio
	if (cls.signon == SIGNONS)
	{
		S_Update(r_origin, vpn, vright, vup);
		CL_DecayLights();
	}
	else
		S_Update(vec3_origin, vec3_origin, vec3_origin, vec3_origin);

	CDAudio_Update();

	if (host_speeds.value)
	{
		int pass1 = (time1 - time3) * 1000;
		time3 = Sys_FloatTime();
		int pass2 = (time2 - time1) * 1000;
		int pass3 = (time3 - time2) * 1000;
		Con_Printf("%3i tot %3i server %3i gfx %3i snd\n",
		           pass1 + pass2 + pass3, pass1, pass2, pass3);
	}

	host_framecount++;
}

void Host_Frame(float time)
{
	static double timetotal;
	static int timecount;

	if (!serverprofile.value)
	{
		_Host_Frame(time);
		return;
	}

	auto time1 = Sys_FloatTime();
	_Host_Frame(time);
	auto time2 = Sys_FloatTime();

	timetotal += time2 - time1;
	timecount++;

	if (timecount < 1000)
		return;

	int m = timetotal * 1000 / timecount;
	timecount = 0;
	timetotal = 0;
	int c = 0;
	for (int i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active)
			c++;
	}

	Con_Printf("serverprofile: %2i clients %2i msec\n", c, m);
}

void Cvar_Init();
void ExtraMaps_Init();
void Modlist_Init();

/*
====================
Host_Init
====================
*/
void Host_Init(quakeparms_t* parms)
{
	if (standard_quake)
		minimum_memory = MINIMUM_MEMORY;
	else
		minimum_memory = MINIMUM_MEMORY_LEVELPAK;

	if (COM_CheckParm("-minmemory"))
		parms->memsize = minimum_memory;

	host_parms = *parms;

	if (parms->memsize < minimum_memory)
		Sys_Error("Only %4.1f megs of memory available, can't execute game", parms->memsize / static_cast<float>(0x100000));

	com_argc = parms->argc;
	com_argv = parms->argv;

	Memory_Init(parms->membase, parms->memsize);
	Cbuf_Init();
	Cmd_Init();
	Cvar_Init(); //johnfitz
	V_Init();
	Chase_Init();
	COM_Init();
	Host_InitLocal();
	W_LoadWadFile(); //johnfitz -- filename is now hard-coded for honesty
	Key_Init();
	Con_Init();
	M_Init();
	PR_Init();
	Mod_Init();
	NET_Init();
	SV_Init();
	ExtraMaps_Init(); //johnfitz
	Modlist_Init(); //johnfitz

	//Con_Printf("Exe: "__TIME__" "__DATE__"\n");
	Con_Printf("%4.1f megabyte heap\n", parms->memsize / (1024 * 1024.0));

	if (cls.state != cactive_t::ca_dedicated)
	{
		host_colormap = static_cast<byte *>(COM_LoadHunkFile("gfx/colormap.lmp"));
		if (!host_colormap)
			Sys_Error("Couldn't load gfx/colormap.lmp");

#ifndef _WIN32 // on non win32, mouse comes before video for security reasons
		IN_Init ();
#endif
		VID_Init();
		TexMgr_Init(); //johnfitz
		Draw_Init();
		SCR_Init();
		R_Init();
		S_Init();
		CDAudio_Init();
		Sbar_Init();
		CL_Init();
#ifdef _WIN32 // on non win32, mouse comes before video for security reasons
		IN_Init();
#endif
	}

	Cbuf_InsertText("exec quake.rc\n");
	//	Cbuf_InsertText ("exec fitzquake.rc\n"); //johnfitz (inserted second so it'll be executed first)

	Cbuf_AddText("\n\nvid_unlock\n"); //johnfitz -- in case the vid mode was locked during vid_init, we can unlock it now.
	//note: added two newlines to the front becuase the command buffer swallows one of them.

	Hunk_AllocName(0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark();

	host_initialized = true;

	Con_Printf("\n========= Quake Initialized =========\n\n"); //johnfitz - was Sys_Printf
}


/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void Host_Shutdown()
{
	static bool isdown = false;

	if (isdown)
	{
		printf("recursive shutdown\n");
		return;
	}
	isdown = true;

	// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;

	Host_WriteConfiguration();

	CDAudio_Shutdown();
	NET_Shutdown();
	S_Shutdown();
	IN_Shutdown();

	if (cls.state != cactive_t::ca_dedicated)
	{
		VID_Shutdown();
	}
}
