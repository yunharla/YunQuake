#include "quakedef.h"

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t cl_name     = {"_cl_name", "player", qtrue};
cvar_t cl_color    = {"_cl_color", "0", qtrue};
cvar_t cl_shownet  = {"cl_shownet", "0"}; // can be 0, 1, or 2
cvar_t cl_nolerp   = {"cl_nolerp", "0"};
cvar_t lookspring  = {"lookspring", "0", qtrue};
cvar_t lookstrafe  = {"lookstrafe", "0", qtrue};
cvar_t sensitivity = {"sensitivity", "3", qtrue};
cvar_t m_pitch     = {"m_pitch", "0.022", qtrue};
cvar_t m_yaw       = {"m_yaw", "0.022", qtrue};
cvar_t m_forward   = {"m_forward", "1", qtrue};
cvar_t m_side      = {"m_side", "0.8", qtrue};


client_static_t cls;
client_state_t  cl;
// FIXME: put these on hunk?
efrag_t      cl_efrags[MAX_EFRAGS];
entity_t     cl_entities[MAX_EDICTS];
entity_t     cl_static_entities[MAX_STATIC_ENTITIES];
lightstyle_t cl_lightstyle[MAX_LIGHTSTYLES];
dlight_t     cl_dlights[MAX_DLIGHTS];

int       cl_numvisedicts;
entity_t* cl_visedicts[MAX_VISEDICTS];

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState()
{
	int i;

	if (!sv.active)
		Host_ClearMemory();

	// wipe the entire cl structure
	memset(&cl, 0, sizeof cl);

	SZ_Clear(&cls.message);

	// clear other arrays	
	memset(cl_efrags, 0, sizeof cl_efrags);
	memset(cl_entities, 0, sizeof cl_entities);
	memset(cl_dlights, 0, sizeof cl_dlights);
	memset(cl_lightstyle, 0, sizeof cl_lightstyle);
	memset(cl_temp_entities, 0, sizeof cl_temp_entities);
	memset(cl_beams, 0, sizeof cl_beams);

	//
	// allocate the efrags and chain together into a free list
	//
	cl.free_efrags                = cl_efrags;
	for (i                        = 0; i < MAX_EFRAGS - 1; i++)
		cl.free_efrags[i].entnext = &cl.free_efrags[i + 1];
	cl.free_efrags[i].entnext     = nullptr;
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect()
{
	// stop sounds (especially looping!)
	S_StopAllSounds(qtrue);

	// bring the console down and fade the colors back to normal
	//	SCR_BringDownConsole ();

	// if running a local server, shut it down
	if (cls.demoplayback)
		CL_StopPlayback();
	else if (cls.state == cactive_t::ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f();

		Con_DPrintf("Sending clc_disconnect\n");
		SZ_Clear(&cls.message);
		MSG_WriteByte(&cls.message, clc_disconnect);
		NET_SendUnreliableMessage(cls.netcon, &cls.message);
		SZ_Clear(&cls.message);
		NET_Close(cls.netcon);

		cls.state = cactive_t::ca_disconnected;
		if (sv.active)
			Host_ShutdownServer(qfalse);
	}

	cls.demoplayback = cls.timedemo = qfalse;
	cls.signon       = 0;
}

void CL_Disconnect_f()
{
	CL_Disconnect();
	if (sv.active)
		Host_ShutdownServer(qfalse);
}


/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection(char* host)
{
	if (cls.state == cactive_t::ca_dedicated)
		return;

	if (cls.demoplayback)
		return;

	CL_Disconnect();

	cls.netcon = NET_Connect(host);
	if (!cls.netcon)
		Host_Error("CL_Connect: connect failed\n");
	Con_DPrintf("CL_EstablishConnection: connected to %s\n", host);

	cls.demonum = -1; // not in the demo loop now
	cls.state   = cactive_t::ca_connected;
	cls.signon  = 0; // need all the signon messages before playing
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply()
{
	char str[8192];

	Con_DPrintf("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte(&cls.message, clc_stringcmd);
		MSG_WriteString(&cls.message, "prespawn");
		break;

	case 2:
		MSG_WriteByte(&cls.message, clc_stringcmd);
		MSG_WriteString(&cls.message, va("name \"%s\"\n", cl_name.string));

		MSG_WriteByte(&cls.message, clc_stringcmd);
		MSG_WriteString(&cls.message, va("color %i %i\n", static_cast<int>(cl_color.value) >> 4, static_cast<int>(cl_color.value) & 15));

		MSG_WriteByte(&cls.message, clc_stringcmd);
		sprintf(str, "spawn %s", cls.spawnparms);
		MSG_WriteString(&cls.message, str);
		break;

	case 3:
		MSG_WriteByte(&cls.message, clc_stringcmd);
		MSG_WriteString(&cls.message, "begin");
		Cache_Report(); // print remaining memory
		break;

	case 4:
		SCR_EndLoadingPlaque(); // allow normal screen updates
		break;
	default: break;
	}
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo()
{
	char str[1024];

	if (cls.demonum == -1)
		return; // don't play demos

	SCR_BeginLoadingPlaque();

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;
		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf("No demos listed with startdemos\n");
			cls.demonum = -1;
			return;
		}
	}

	sprintf(str, "playdemo %s\n", cls.demos[cls.demonum]);
	Cbuf_InsertText(str);
	cls.demonum++;
}

/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f()
{
	entity_t* ent;
	int       i;

	for (i = 0, ent = cl_entities; i < cl.num_entities; i++, ent++)
	{
		Con_Printf("%3i:", i);
		if (!ent->model)
		{
			Con_Printf("EMPTY\n");
			continue;
		}
		Con_Printf("%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n", ent->model->name, ent->frame, ent->origin[0], ent->origin[1], ent->origin[2], ent->angles[0], ent->angles[1], ent->angles[2]);
	}
}


/*
===============
SetPal

Debugging tool, just flashes the screen
===============
*/
void SetPal(int i)
{
}

/*
===============
CL_AllocDlight

===============
*/
dlight_t* CL_AllocDlight(const int key)
{
	int       i;
	dlight_t* dl;

	// first look for an exact key match
	if (key)
	{
		dl     = cl_dlights;
		for (i = 0; i < MAX_DLIGHTS; i++, dl++)
		{
			if (dl->key == key)
			{
				memset(dl, 0, sizeof*dl);
				dl->key = key;
				return dl;
			}
		}
	}

	// then look for anything else
	dl     = cl_dlights;
	for (i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < cl.time)
		{
			memset(dl, 0, sizeof*dl);
			dl->key = key;
			return dl;
		}
	}

	dl = &cl_dlights[0];
	memset(dl, 0, sizeof*dl);
	dl->key = key;
	return dl;
}


/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights()
{
	const float time = cl.time - cl.oldtime;
	auto        dl   = cl_dlights;

	for (auto i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < cl.time || !dl->radius)
			continue;

		dl->radius -= time * dl->decay;
		if (dl->radius < 0)
			dl->radius = 0;
	}
}


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
float CL_LerpPoint()
{
	float f = cl.mtime[0] - cl.mtime[1];

	if (!f || cl_nolerp.value || cls.timedemo || sv.active)
	{
		cl.time = cl.mtime[0];
		return 1;
	}

	if (f > 0.1)
	{
		// dropped packet, or start of demo
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f           = 0.1;
	}
	float frac = (cl.time - cl.mtime[1]) / f;
	//Con_Printf ("frac: %f\n",frac);
	if (frac < 0)
	{
		if (frac < -0.01)
		{
			SetPal(1);
			cl.time = cl.mtime[1];
			//				Con_Printf ("low frac\n");
		}
		frac = 0;
	}
	else if (frac > 1)
	{
		if (frac > 1.01)
		{
			SetPal(2);
			cl.time = cl.mtime[0];
			//				Con_Printf ("high frac\n");
		}
		frac = 1;
	}
	else
		SetPal(0);

	return frac;
}


/*
===============
CL_RelinkEntities
===============
*/
void CL_RelinkEntities()
{
	entity_t* ent;
	int       i, j;
	float     d;
	vec3_t    delta;
	vec3_t    oldorg;
	dlight_t* dl;

	// determine partial update time	
	const auto frac = CL_LerpPoint();

	cl_numvisedicts = 0;

	//
	// interpolate player info
	//
	for (i             = 0; i < 3; i++)
		cl.velocity[i] = cl.mvelocity[1][i] +
			frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	if (cls.demoplayback)
	{
		// interpolate the angles	
		for (j = 0; j < 3; j++)
		{
			d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;
			cl.viewangles[j] = cl.mviewangles[1][j] + frac * d;
		}
	}

	const auto bobjrotate = anglemod(100 * cl.time);

	// start on the entity after the world
	for (i = 1, ent = cl_entities + 1; i < cl.num_entities; i++, ent++)
	{
		if (!ent->model)
		{
			// empty slot
			if (ent->forcelink)
				R_RemoveEfrags(ent); // just became empty
			continue;
		}

		// if the object wasn't included in the last packet, remove it
		if (ent->msgtime != cl.mtime[0])
		{
			ent->model = nullptr;
			continue;
		}

		VectorCopy (ent->origin, oldorg);

		if (ent->forcelink)
		{
			// the entity was not updated in the last message
			// so move to the final spot
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->angles);
		}
		else
		{
			// if the delta is large, assume a teleport and don't lerp
			auto f = frac;
			for (j = 0; j < 3; j++)
			{
				delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];
				if (delta[j] > 100 || delta[j] < -100)
					f = 1; // assume a teleportation, not a motion
			}

			// interpolate the origin and angles
			for (j = 0; j < 3; j++)
			{
				ent->origin[j] = ent->msg_origins[1][j] + f * delta[j];

				d = ent->msg_angles[0][j] - ent->msg_angles[1][j];
				if (d > 180)
					d -= 360;
				else if (d < -180)
					d += 360;
				ent->angles[j] = ent->msg_angles[1][j] + f * d;
			}
		}

		// rotate binary objects locally
		if (ent->model->flags & EF_ROTATE)
			ent->angles[1] = bobjrotate;

		if (ent->effects & EF_BRIGHTFIELD)
			R_EntityParticles(ent);
		if (ent->effects & EF_MUZZLEFLASH)
		{
			vec3_t fv, rv, uv;

			dl = CL_AllocDlight(i);
			VectorCopy (ent->origin, dl->origin);
			dl->origin[2] += 16;
			AngleVectors(ent->angles, fv, rv, uv);

			VectorMA(dl->origin, 18, fv, dl->origin);
			dl->radius   = 200 + (rand() & 31);
			dl->minlight = 32;
			dl->die      = cl.time + 0.1;
		}
		if (ent->effects & EF_BRIGHTLIGHT)
		{
			dl = CL_AllocDlight(i);
			VectorCopy (ent->origin, dl->origin);
			dl->origin[2] += 16;
			dl->radius = 400 + (rand() & 31);
			dl->die    = cl.time + 0.001;
		}
		if (ent->effects & EF_DIMLIGHT)
		{
			dl = CL_AllocDlight(i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200 + (rand() & 31);
			dl->die    = cl.time + 0.001;
		}

		if (ent->model->flags & EF_GIB)
			R_RocketTrail(oldorg, ent->origin, 2);
		else if (ent->model->flags & EF_ZOMGIB)
			R_RocketTrail(oldorg, ent->origin, 4);
		else if (ent->model->flags & EF_TRACER)
			R_RocketTrail(oldorg, ent->origin, 3);
		else if (ent->model->flags & EF_TRACER2)
			R_RocketTrail(oldorg, ent->origin, 5);
		else if (ent->model->flags & EF_ROCKET)
		{
			R_RocketTrail(oldorg, ent->origin, 0);
			dl = CL_AllocDlight(i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200;
			dl->die    = cl.time + 0.01;
		}
		else if (ent->model->flags & EF_GRENADE)
			R_RocketTrail(oldorg, ent->origin, 1);
		else if (ent->model->flags & EF_TRACER3)
			R_RocketTrail(oldorg, ent->origin, 6);

		ent->forcelink = qfalse;

		if (i == cl.viewentity && !chase_active.value)
			continue;

		if (cl_numvisedicts < MAX_VISEDICTS)
		{
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}
}


/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer()
{
	int ret;

	cl.oldtime = cl.time;
	cl.time += host_frametime;

	do
	{
		ret = CL_GetMessage();
		if (ret == -1)
			Host_Error("CL_ReadFromServer: lost server connection");
		if (!ret)
			break;

		cl.last_received_message = realtime;
		CL_ParseServerMessage();
	}
	while (ret && cls.state == cactive_t::ca_connected);

	if (cl_shownet.value)
		Con_Printf("\n");

	CL_RelinkEntities();
	CL_UpdateTEnts();

	//
	// bring the links up to date
	//
	return 0;
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd()
{
	usercmd_t cmd;

	if (cls.state != cactive_t::ca_connected)
		return;

	if (cls.signon == SIGNONS)
	{
		// get basic movement from keyboard
		CL_BaseMove(&cmd);

		// allow mice or other external controllers to add to the move
		IN_Move(&cmd);

		// send the unreliable message
		CL_SendMove(&cmd);
	}

	if (cls.demoplayback)
	{
		SZ_Clear(&cls.message);
		return;
	}

	// send the reliable message
	if (!cls.message.cursize)
		return; // no message at all

	if (!NET_CanSendMessage(cls.netcon))
	{
		Con_DPrintf("CL_WriteToServer: can't send\n");
		return;
	}

	if (NET_SendMessage(cls.netcon, &cls.message) == -1)
		Host_Error("CL_WriteToServer: lost server connection");

	SZ_Clear(&cls.message);
}

/*
=================
CL_Init
=================
*/
void CL_Init()
{
	SZ_Alloc(&cls.message, 1024);

	CL_InitInput();
	CL_InitTEnts();

	//
	// register our commands
	//
	Cvar_RegisterVariable(&cl_name);
	Cvar_RegisterVariable(&cl_color);
	Cvar_RegisterVariable(&cl_upspeed);
	Cvar_RegisterVariable(&cl_forwardspeed);
	Cvar_RegisterVariable(&cl_backspeed);
	Cvar_RegisterVariable(&cl_sidespeed);
	Cvar_RegisterVariable(&cl_movespeedkey);
	Cvar_RegisterVariable(&cl_yawspeed);
	Cvar_RegisterVariable(&cl_pitchspeed);
	Cvar_RegisterVariable(&cl_anglespeedkey);
	Cvar_RegisterVariable(&cl_shownet);
	Cvar_RegisterVariable(&cl_nolerp);
	Cvar_RegisterVariable(&lookspring);
	Cvar_RegisterVariable(&lookstrafe);
	Cvar_RegisterVariable(&sensitivity);

	Cvar_RegisterVariable(&m_pitch);
	Cvar_RegisterVariable(&m_yaw);
	Cvar_RegisterVariable(&m_forward);
	Cvar_RegisterVariable(&m_side);

	//	Cvar_RegisterVariable (&cl_autofire);

	Cmd_AddCommand("entities", CL_PrintEntities_f);
	Cmd_AddCommand("disconnect", CL_Disconnect_f);
	Cmd_AddCommand("record", CL_Record_f);
	Cmd_AddCommand("stop", CL_Stop_f);
	Cmd_AddCommand("playdemo", CL_PlayDemo_f);
	Cmd_AddCommand("timedemo", CL_TimeDemo_f);
}
