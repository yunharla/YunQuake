#pragma once

struct usercmd_t
{
	vec3_t viewangles;

	// intended velocities
	float forwardmove;
	float sidemove;
	float upmove;
};

struct lightstyle_t
{
	int  length;
	char map[MAX_STYLESTRING];
};

struct scoreboard_t
{
	char  name[MAX_SCOREBOARDNAME];
	float entertime;
	int   frags;
	int   colors; // two 4 bit fields
	byte  translations[VID_GRADES * 256];
};

struct cshift_t
{
	int destcolor[3];
	int percent; // 0-256
};

#define	CSHIFT_CONTENTS	0
#define	CSHIFT_DAMAGE	1
#define	CSHIFT_BONUS	2
#define	CSHIFT_POWERUP	3
#define	NUM_CSHIFTS		4

#define	NAME_LENGTH	64


//
// client_state_t should hold all pieces of the client state
//

#define	SIGNONS		4			// signon messages to receive before connected

#define	MAX_DLIGHTS		32

struct dlight_t
{
	vec3_t origin;
	float  radius;
	float  die; // stop lighting after this time
	float  decay; // drop this each second
	float  minlight; // don't add when contributing less
	int    key;
};

struct model_t;
#define	MAX_BEAMS	24

struct beam_t
{
	int      entity;
	model_t* model;
	float    endtime;
	vec3_t   start, end;
};

#define	MAX_EFRAGS		640

#define	MAX_MAPSTRING	2048
#define	MAX_DEMOS		8
#define	MAX_DEMONAME	16

enum class cactive_t
{
	ca_dedicated,
	// a dedicated server with no ability to start a client
	ca_disconnected,
	// full screen console with no connection
	ca_connected // valid netcon, talking to a server
};

//
// the client_static_t structure is persistant through an arbitrary number
// of server connections
//
struct client_static_t
{
	cactive_t state;

	// personalization data sent to server	
	char mapstring[MAX_QPATH];
	char spawnparms[MAX_MAPSTRING]; // to restart a level

	// demo loop control
	int  demonum; // -1 = don't play demos
	char demos[MAX_DEMOS][MAX_DEMONAME]; // when not playing

	// demo recording info must be here, because record is started before
	// entering a map (and clearing client_state_t)
	qboolean demorecording;
	qboolean demoplayback;
	qboolean timedemo;
	int      forcetrack; // -1 = use normal cd track
	FILE*    demofile;
	int      td_lastframe; // to meter out one message a frame
	int      td_startframe; // host_framecount at start
	float    td_starttime; // realtime at second frame of timedemo


	// connection information
	int        signon; // 0 to SIGNONS
	qsocket_t* netcon;
	sizebuf_t  message; // writing buffer to send to server
};

extern client_static_t cls;

struct model_t;

//
// the client_state_t structure is wiped completely at every
// server signon
//
struct client_state_t
{
	int movemessages; // since connecting to this server
	// throw out the first couple, so the player
	// doesn't accidentally do something the 
	// first frame
	usercmd_t cmd; // last command sent to the server

	// information for local display
	int   stats[MAX_CL_STATS]; // health, etc
	int   items; // inventory bit flags
	float item_gettime[32]; // cl.time of aquiring item, for blinking
	float faceanimtime; // use anim frame if cl.time < this

	cshift_t cshifts[NUM_CSHIFTS]; // color shifts for damage, powerups
	cshift_t prev_cshifts[NUM_CSHIFTS]; // and content types

	// the client maintains its own idea of view angles, which are
	// sent to the server each frame.  The server sets punchangle when
	// the view is temporarliy offset, and an angle reset commands at the start
	// of each level and after teleporting.
	vec3_t mviewangles[2]; // during demo playback viewangles is lerped
	// between these
	vec3_t viewangles;

	vec3_t mvelocity[2]; // update by server, used for lean+bob
	// (0 is newest)
	vec3_t velocity; // lerped between mvelocity[0] and [1]

	vec3_t punchangle; // temporary offset

	// pitch drifting vars
	float    idealpitch;
	float    pitchvel;
	qboolean nodrift;
	float    driftmove;
	double   laststop;

	float viewheight;
	float crouch; // local amount for smoothing stepups

	qboolean paused; // send over by server
	qboolean onground;
	qboolean inwater;

	int intermission; // don't change view angle, full screen, etc
	int completed_time; // latched at intermission start

	double mtime[2]; // the timestamp of last two messages	
	double time; // clients view of time, should be between
	// servertime and oldservertime to generate
	// a lerp point for other data
	double oldtime; // previous cl.time, time-oldtime is used
	// to decay light values and smooth step ups


	float last_received_message; // (realtime) for net trouble icon

	//
	// information that is static for the entire time connected to a server
	//
	model_t* model_precache[MAX_MODELS];
	sfx_t*   sound_precache[MAX_SOUNDS];

	char levelname[40]; // for display on solo scoreboard
	int  viewentity; // cl_entitites[cl.viewentity] = player
	int  maxclients;
	int  gametype;

	// refresh related state
	model_t* worldmodel; // cl_entitites[0].model
	efrag_t* free_efrags;
	int      num_entities; // held in cl_entities array
	int      num_statics; // held in cl_staticentities array
	entity_t viewent; // the gun model

	int cdtrack, looptrack; // cd audio

	// frag scoreboard
	scoreboard_t* scores; // [cl.maxclients]
};


#define	MAX_TEMP_ENTITIES	64			// lightning bolts, etc
#define	MAX_STATIC_ENTITIES	128			// torches, etc

extern client_state_t cl;

// FIXME, allocate dynamically
extern efrag_t      cl_efrags[MAX_EFRAGS];
extern entity_t     cl_entities[MAX_EDICTS];
extern entity_t     cl_static_entities[MAX_STATIC_ENTITIES];
extern lightstyle_t cl_lightstyle[MAX_LIGHTSTYLES];
extern dlight_t     cl_dlights[MAX_DLIGHTS];
extern entity_t     cl_temp_entities[MAX_TEMP_ENTITIES];
extern beam_t       cl_beams[MAX_BEAMS];

//=============================================================================

//
// cl_main
//
dlight_t* CL_AllocDlight(int key);
void      CL_DecayLights();

void CL_Init();

void CL_EstablishConnection(char* host);
void CL_Signon1();
void CL_Signon2();
void CL_Signon3();
void CL_Signon4();

void CL_Disconnect();
void CL_Disconnect_f();
void CL_NextDemo();

#define			MAX_VISEDICTS	256
extern int       cl_numvisedicts;
extern entity_t* cl_visedicts[MAX_VISEDICTS];

//
// cl_input
//
struct kbutton_t
{
	int down[2]; // key nums holding it down
	int state; // low bit is down state
};

extern kbutton_t in_mlook, in_klook;
extern kbutton_t in_strafe;
extern kbutton_t in_speed;

void CL_InitInput();
void CL_SendCmd();
void CL_SendMove(usercmd_t* cmd);

void CL_ParseTEnt();
void CL_UpdateTEnts();

void CL_ClearState();


int  CL_ReadFromServer();
void CL_WriteToServer(usercmd_t* cmd);
void CL_BaseMove(usercmd_t*      cmd);


float CL_KeyState(kbutton_t* key);
char* Key_KeynumToString(int keynum);

//
// cl_demo.c
//
void CL_StopPlayback();
int  CL_GetMessage();

void CL_Stop_f();
void CL_Record_f();
void CL_PlayDemo_f();
void CL_TimeDemo_f();
void CL_FinishTimeDemo();

//
// cl_parse.c
//
void CL_ParseServerMessage();
void CL_NewTranslation(int slot);

//
// view
//
void V_StartPitchDrift();
void V_StopPitchDrift();

void V_RenderView();
void V_UpdatePalette();
void V_Register();
void V_ParseDamage();
void V_SetContentsColor(int contents);


//
// cl_tent
//
void CL_InitTEnts();
void CL_SignonReply();
