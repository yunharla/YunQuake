#include "quakedef.h"
#include "winquake.h"
#include <commctrl.h>

#define MAX_MODE_LIST	30
#define VID_ROW_SIZE	3
#define WARP_WIDTH		320
#define WARP_HEIGHT		200
#define MAXWIDTH		10000
#define MAXHEIGHT		10000
#define BASEWIDTH		320
#define BASEHEIGHT		200

#define MODE_WINDOWED			0
#define NO_MODE					(MODE_WINDOWED - 1)
#define MODE_FULLSCREEN_DEFAULT	(MODE_WINDOWED + 1)

struct vmode_t
{
	modestate_t type;
	int         width;
	int         height;
	int         modenum;
	int         dib;
	int         fullscreen;
	int         bpp;
	int         halfscreen;
	char        modedesc[17];
};

struct lmode_t
{
	int width;
	int height;
};

lmode_t lowresmodes[] = {
	{320, 200},
	{320, 240},
	{400, 300},
	{512, 384},
};

const char* gl_vendor;
const char* gl_renderer;
const char* gl_version;
const char* gl_extensions;

qboolean scr_skipupdate;

static vmode_t  modelist[MAX_MODE_LIST];
static int      nummodes;
static vmode_t* pcurrentmode;
static vmode_t  badmode;

static DEVMODE  gdevmode;
static qboolean vid_initialized = qfalse;
static qboolean windowed, leavecurrentmode;
static qboolean vid_canalttab    = qfalse;
static qboolean vid_wassuspended = qfalse;
static int      windowed_mouse;
extern qboolean mouseactive; // from in_win.c
static HICON    hIcon;

int   DIBWidth, DIBHeight;
RECT  WindowRect;
DWORD WindowStyle, ExWindowStyle;

HWND mainwindow, dibwindow;

int             vid_modenum = NO_MODE;
int             vid_realmode;
int             vid_default = MODE_WINDOWED;
static int      windowed_default;
unsigned char   vid_curpal[256 * 3];
static qboolean fullsbardraw = qfalse;

static float vid_gamma = 1.0;

HGLRC baseRC;
HDC   maindc;

glvert_t glv;

cvar_t gl_ztrick = {"gl_ztrick", "1"};

HWND WINAPI InitializeWindow(HINSTANCE hInstance, int nCmdShow);

unsigned short d_8to16table[256];
unsigned       d_8to24table[256];
unsigned char  d_15to8table[65536];

float gldepthmin, gldepthmax;

modestate_t modestate = modestate_t::MS_UNINIT;

void VID_MenuDraw();
void VID_MenuKey(int key);

LONG WINAPI MainWndProc(HWND           hWnd, UINT    uMsg, WPARAM wParam, LPARAM lParam);
void        AppActivate(BOOL           fActive, BOOL minimize);
char*       VID_GetModeDescription(int mode);
void        ClearAllStates();
void        VID_UpdateWindowStatus();
void        GL_Init();

PROC glArrayElementEXT;
PROC glColorPointerEXT;
PROC glTexCoordPointerEXT;
PROC glVertexPointerEXT;

using lp3DFXFUNC = void (APIENTRY *)(int, int, int, int, int, const void*);
lp3DFXFUNC glColorTableEXT;
qboolean   isPermedia  = qfalse;
qboolean   gl_mtexable = qfalse;

//====================================

cvar_t vid_mode = {"vid_mode", "0", qfalse};
// Note that 0 is MODE_WINDOWED
cvar_t _vid_default_mode = {"_vid_default_mode", "0", qtrue};
// Note that 3 is MODE_FULLSCREEN_DEFAULT
cvar_t _vid_default_mode_win = {"_vid_default_mode_win", "3", qtrue};
cvar_t vid_wait              = {"vid_wait", "0"};
cvar_t vid_nopageflip        = {"vid_nopageflip", "0", qtrue};
cvar_t _vid_wait_override    = {"_vid_wait_override", "0", qtrue};
cvar_t vid_config_x          = {"vid_config_x", "800", qtrue};
cvar_t vid_config_y          = {"vid_config_y", "600", qtrue};
cvar_t vid_stretch_by_2      = {"vid_stretch_by_2", "1", qtrue};
cvar_t _windowed_mouse       = {"_windowed_mouse", "1", qtrue};

int  window_center_x, window_center_y, window_x, window_y, window_width, window_height;
RECT window_rect;

// direct draw software compatability stuff


void VID_LockBuffer()
{
}

void VID_UnlockBuffer()
{
}


void CenterWindow(HWND hWndCenter, int width, int height, BOOL lefttopjustify)
{
	auto CenterX = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
	auto CenterY = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
	if (CenterX > CenterY * 2)
		CenterX >>= 1; // dual screens
	CenterX = CenterX < 0 ? 0 : CenterX;
	CenterY = CenterY < 0 ? 0 : CenterY;
	SetWindowPos(hWndCenter, nullptr, CenterX, CenterY, 0, 0,
	             SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME);
}

qboolean VID_SetWindowedMode(int modenum)
{
	WindowRect.top = WindowRect.left = 0;

	WindowRect.right  = modelist[modenum].width;
	WindowRect.bottom = modelist[modenum].height;

	DIBWidth  = modelist[modenum].width;
	DIBHeight = modelist[modenum].height;

	WindowStyle = WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU |
		WS_MINIMIZEBOX;
	ExWindowStyle = 0;

	auto rect = WindowRect;
	AdjustWindowRectEx(&rect, WindowStyle, FALSE, 0);

	const int width  = rect.right - rect.left;
	const int height = rect.bottom - rect.top;

	// Create the DIB window
	dibwindow = CreateWindowEx(
	                           ExWindowStyle,
	                           "WinQuake",
	                           "GLQuake",
	                           WindowStyle,
	                           rect.left, rect.top,
	                           width,
	                           height,
	                           nullptr,
	                           nullptr,
	                           global_hInstance,
	                           nullptr);

	if (!dibwindow)
		Sys_Error("Couldn't create DIB window");

	// Center and show the DIB window
	CenterWindow(dibwindow, WindowRect.right - WindowRect.left,
	             WindowRect.bottom - WindowRect.top, qfalse);

	ShowWindow(dibwindow, SW_SHOWDEFAULT);
	UpdateWindow(dibwindow);

	modestate = modestate_t::MS_WINDOWED;

	// because we have set the background brush for the window to nullptr
	// (to avoid flickering when re-sizing the window on the desktop),
	// we clear the window to black when created, otherwise it will be
	// empty while Quake starts up.
	const auto hdc = GetDC(dibwindow);
	PatBlt(hdc, 0, 0, WindowRect.right, WindowRect.bottom,BLACKNESS);
	ReleaseDC(dibwindow, hdc);

	if (vid.conheight > modelist[modenum].height)
		vid.conheight = modelist[modenum].height;
	if (vid.conwidth > modelist[modenum].width)
		vid.conwidth = modelist[modenum].width;
	vid.width        = vid.conwidth;
	vid.height       = vid.conheight;

	vid.numpages = 2;

	mainwindow = dibwindow;

	SendMessage(mainwindow, WM_SETICON, static_cast<WPARAM>(TRUE), reinterpret_cast<LPARAM>(hIcon));
	SendMessage(mainwindow, WM_SETICON, static_cast<WPARAM>(FALSE), reinterpret_cast<LPARAM>(hIcon));

	return qtrue;
}


qboolean VID_SetFullDIBMode(int modenum)
{
	modestate = modestate_t::MS_FULLDIB;

	WindowRect.top = WindowRect.left = 0;

	WindowRect.right  = modelist[modenum].width;
	WindowRect.bottom = modelist[modenum].height;

	DIBWidth  = modelist[modenum].width;
	DIBHeight = modelist[modenum].height;

	WindowStyle   = WS_POPUP;
	ExWindowStyle = 0;

	auto rect = WindowRect;
	AdjustWindowRectEx(&rect, WindowStyle, FALSE, 0);

	const int width  = rect.right - rect.left;
	const int height = rect.bottom - rect.top;

	// Create the DIB window
	dibwindow = CreateWindowEx(
	                           ExWindowStyle,
	                           "WinQuake",
	                           "GLQuake",
	                           WindowStyle,
	                           rect.left, rect.top,
	                           width,
	                           height,
	                           nullptr,
	                           nullptr,
	                           global_hInstance,
	                           nullptr);

	if (!dibwindow)
		Sys_Error("Couldn't create DIB window");

	ShowWindow(dibwindow, SW_SHOWDEFAULT);
	UpdateWindow(dibwindow);

	// Because we have set the background brush for the window to nullptr
	// (to avoid flickering when re-sizing the window on the desktop), we
	// clear the window to black when created, otherwise it will be
	// empty while Quake starts up.
	const auto hdc = GetDC(dibwindow);
	PatBlt(hdc, 0, 0, WindowRect.right, WindowRect.bottom,BLACKNESS);
	ReleaseDC(dibwindow, hdc);

	if (vid.conheight > modelist[modenum].height)
		vid.conheight = modelist[modenum].height;
	if (vid.conwidth > modelist[modenum].width)
		vid.conwidth = modelist[modenum].width;
	vid.width        = vid.conwidth;
	vid.height       = vid.conheight;

	vid.numpages = 2;

	// needed because we're not getting WM_MOVE messages fullscreen on NT
	window_x = 0;
	window_y = 0;

	mainwindow = dibwindow;

	SendMessage(mainwindow, WM_SETICON, static_cast<WPARAM>(TRUE), reinterpret_cast<LPARAM>(hIcon));
	SendMessage(mainwindow, WM_SETICON, static_cast<WPARAM>(FALSE), reinterpret_cast<LPARAM>(hIcon));

	return qtrue;
}


int VID_SetMode(int modenum, unsigned char* palette)
{
	qboolean stat = 0;
	MSG      msg;

	if (windowed && modenum != 0 ||
		!windowed && modenum < 1 ||
		!windowed && modenum >= nummodes)
	{
		Sys_Error("Bad video mode\n");
	}

	// so Con_Printfs don't mess us up by forcing vid and snd updates
	const int temp                 = scr_disabled_for_loading;
	scr_disabled_for_loading = qtrue;

	// Set either the fullscreen or windowed mode
	if (modelist[modenum].type == modestate_t::MS_WINDOWED)
	{
		if (_windowed_mouse.value && key_dest == keydest_t::key_game)
		{
			stat = VID_SetWindowedMode(modenum);
			IN_ActivateMouse();
			IN_HideMouse();
		}
		else
		{
			IN_DeactivateMouse();
			IN_ShowMouse();
			stat = VID_SetWindowedMode(modenum);
		}
	}
	else if (modelist[modenum].type == modestate_t::MS_FULLDIB)
	{
		stat = VID_SetFullDIBMode(modenum);
		IN_ActivateMouse();
		IN_HideMouse();
	}
	else
	{
		Sys_Error("VID_SetMode: Bad mode type in modelist");
	}

	window_width  = DIBWidth;
	window_height = DIBHeight;
	VID_UpdateWindowStatus();
	scr_disabled_for_loading = temp;

	if (!stat)
	{
		Sys_Error("Couldn't set video mode");
	}

	// now we try to make sure we get the focus on the mode switch, because
	// sometimes in some systems we don't.  We grab the foreground, then
	// finish setting up, pump all our messages, and sleep for a little while
	// to let messages finish bouncing around the system, then we put
	// ourselves at the top of the z order, then grab the foreground again,
	// Who knows if it helps, but it probably doesn't hurt
	SetForegroundWindow(mainwindow);
	VID_SetPalette(palette);
	vid_modenum = modenum;
	Cvar_SetValue("vid_mode", static_cast<float>(vid_modenum));

	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	Sleep(100);

	SetWindowPos(mainwindow, HWND_TOP, 0, 0, 0, 0,
	             SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW |
	             SWP_NOCOPYBITS);

	SetForegroundWindow(mainwindow);

	// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates();

	if (!msg_suppress_1)
		Con_SafePrintf("Video mode %s initialized.\n", VID_GetModeDescription(vid_modenum));

	VID_SetPalette(palette);

	vid.recalc_refdef = 1;

	return qtrue;
}


/*
================
VID_UpdateWindowStatus
================
*/
void VID_UpdateWindowStatus()
{
	window_rect.left   = window_x;
	window_rect.top    = window_y;
	window_rect.right  = window_x + window_width;
	window_rect.bottom = window_y + window_height;
	window_center_x    = (window_rect.left + window_rect.right) / 2;
	window_center_y    = (window_rect.top + window_rect.bottom) / 2;

	IN_UpdateClipCursor();
}


//====================================


int texture_mode = GL_LINEAR;

int texture_extension_number = 1;

#ifdef _WIN32
void CheckMultiTextureExtensions()
{
	if (strstr(gl_extensions, "GL_SGIS_multitexture ") && !COM_CheckParm("-nomtex"))
	{
		Con_Printf("Multitexture extensions found.\n");
		qglMTexCoord2fSGIS   = reinterpret_cast<lpMTexFUNC>(wglGetProcAddress("glMTexCoord2fSGIS"));
		qglSelectTextureSGIS = reinterpret_cast<lpSelTexFUNC>(wglGetProcAddress("glSelectTextureSGIS"));
		gl_mtexable          = qtrue;
	}
}
#else
void CheckMultiTextureExtensions() 
{
		gl_mtexable = qtrue;
}
#endif

/*
===============
GL_Init
===============
*/
void GL_Init()
{
	gl_vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
	Con_Printf("GL_VENDOR: %s\n", gl_vendor);
	gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
	Con_Printf("GL_RENDERER: %s\n", gl_renderer);

	gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	Con_Printf("GL_VERSION: %s\n", gl_version);
	gl_extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
	//Con_Printf ("GL_EXTENSIONS: %s\n", gl_extensions);

	//	Con_Printf ("%s %s\n", gl_renderer, gl_version);

	if (_strnicmp(gl_renderer, "PowerVR", 7) == 0)
		fullsbardraw = qtrue;

	if (_strnicmp(gl_renderer, "Permedia", 8) == 0)
		isPermedia = qtrue;

	CheckMultiTextureExtensions();

	glClearColor(1, 0, 0, 0);
	glCullFace(GL_FRONT);
	glEnable(GL_TEXTURE_2D);

	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.666);

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel(GL_FLAT);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

/*
=================
GL_BeginRendering

=================
*/
void GL_BeginRendering(int* x, int* y, int* width, int* height)
{
	*x      = *y = 0;
	*width  = WindowRect.right - WindowRect.left;
	*height = WindowRect.bottom - WindowRect.top;
}


void GL_EndRendering()
{
	if (!scr_skipupdate || block_drawing)
		SwapBuffers(maindc);

	// handle the mouse state when windowed if that's changed
	if (modestate == modestate_t::MS_WINDOWED)
	{
		if (!_windowed_mouse.value)
		{
			if (windowed_mouse)
			{
				IN_DeactivateMouse();
				IN_ShowMouse();
				windowed_mouse = qfalse;
			}
		}
		else
		{
			windowed_mouse = qtrue;
			if (key_dest == keydest_t::key_game && !mouseactive && ActiveApp)
			{
				IN_ActivateMouse();
				IN_HideMouse();
			}
			else if (mouseactive && key_dest != keydest_t::key_game)
			{
				IN_DeactivateMouse();
				IN_ShowMouse();
			}
		}
	}
	if (fullsbardraw)
		Sbar_Changed();
}

void VID_SetPalette(unsigned char* palette)
{
	unsigned       r;
	unsigned       g;
	unsigned       b;
	unsigned       v;
	int            k;
	int            l;
	unsigned short i;

	//
	// 8 8 8 encoding
	//
	auto pal   = palette;
	auto table = d_8to24table;
	for (i     = 0; i < 256; i++)
	{
		r = pal[0];
		g = pal[1];
		b = pal[2];
		pal += 3;

		//		v = (255<<24) + (r<<16) + (g<<8) + (b<<0);
		//		v = (255<<0) + (r<<8) + (g<<16) + (b<<24);
		v        = (255 << 24) + (r << 0) + (g << 8) + (b << 16);
		*table++ = v;
	}
	d_8to24table[255] &= 0xffffff; // 255 is transparent

	// JACK: 3D distance calcs - k is last closest, l is the distance.
	// FIXME: Precalculate this and cache to disk.
	for (i = 0; i < 1 << 15; i++)
	{
		/* Maps
			000000000000000
			000000000011111 = Red  = 0x1F
			000001111100000 = Blue = 0x03E0
			111110000000000 = Grn  = 0x7C00
		*/
		r      = ((i & 0x1F) << 3) + 4;
		g      = ((i & 0x03E0) >> 2) + 4;
		b      = ((i & 0x7C00) >> 7) + 4;
		pal    = reinterpret_cast<unsigned char *>(d_8to24table);
		for (v = 0, k = 0, l = 10000 * 10000; v < 256; v++, pal += 4)
		{
			const int  r1 = r - pal[0];
			const int  g1 = g - pal[1];
			const int  b1 = b - pal[2];
			const auto j  = r1 * r1 + g1 * g1 + b1 * b1;
			if (j < l)
			{
				k = v;
				l = j;
			}
		}
		d_15to8table[i] = k;
	}
}

BOOL gammaworks;

void VID_ShiftPalette(unsigned char* palette)
{
	//	VID_SetPalette (palette);

	//	gammaworks = SetDeviceGammaRamp (maindc, ramps);
}


void VID_SetDefaultMode()
{
	IN_DeactivateMouse();
}


void VID_Shutdown()
{
	if (vid_initialized)
	{
		vid_canalttab = qfalse;
		const auto hRC      = wglGetCurrentContext();
		const auto hDC      = wglGetCurrentDC();

		wglMakeCurrent(nullptr, nullptr);

		if (hRC)
			wglDeleteContext(hRC);

		if (hDC && dibwindow)
			ReleaseDC(dibwindow, hDC);
		/*
		if (modestate == MS_FULLDIB)
			ChangeDisplaySettings (nullptr, 0);*/

		if (maindc && dibwindow)
			ReleaseDC(dibwindow, maindc);

		AppActivate(qfalse, qfalse);
	}
}


//==========================================================================


BOOL bSetupPixelFormat(HDC hDC)
{
	static PIXELFORMATDESCRIPTOR pfd = {
		sizeof(PIXELFORMATDESCRIPTOR), // size of this pfd
		1, // version number
		PFD_DRAW_TO_WINDOW // support window
		| PFD_SUPPORT_OPENGL // support OpenGL
		| PFD_DOUBLEBUFFER, // double buffered
		PFD_TYPE_RGBA, // RGBA type
		24, // 24-bit color depth
		0, 0, 0, 0, 0, 0, // color bits ignored
		0, // no alpha buffer
		0, // shift bit ignored
		0, // no accumulation buffer
		0, 0, 0, 0, // accum bits ignored
		32, // 32-bit z-buffer	
		0, // no stencil buffer
		0, // no auxiliary buffer
		PFD_MAIN_PLANE, // main layer
		0, // reserved
		0, 0, 0 // layer masks ignored
	};
	int pixelformat;

	if ((pixelformat = ChoosePixelFormat(hDC, &pfd)) == 0)
	{
		MessageBox(nullptr, "ChoosePixelFormat failed", "Error", MB_OK);
		return FALSE;
	}

	if (SetPixelFormat(hDC, pixelformat, &pfd) == FALSE)
	{
		MessageBox(nullptr, "SetPixelFormat failed", "Error", MB_OK);
		return FALSE;
	}

	return TRUE;
}


byte scantokey[128] =
{
	//  0           1       2       3       4       5       6       7 
	//  8           9       A       B       C       D       E       F 
	0, 27, '1', '2', '3', '4', '5', '6',
	'7', '8', '9', '0', '-', '=', K_BACKSPACE, 9, // 0 
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
	'o', 'p', '[', ']', 13, K_CTRL, 'a', 's', // 1 
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
	'\'', '`', K_SHIFT, '\\', 'z', 'x', 'c', 'v', // 2 
	'b', 'n', 'm', ',', '.', '/', K_SHIFT, '*',
	K_ALT, ' ', 0, K_F1, K_F2, K_F3, K_F4, K_F5, // 3 
	K_F6, K_F7, K_F8, K_F9, K_F10, K_PAUSE, 0, K_HOME,
	K_UPARROW,K_PGUP, '-',K_LEFTARROW, '5',K_RIGHTARROW, '+',K_END, //4 
	K_DOWNARROW,K_PGDN,K_INS,K_DEL, 0, 0, 0, K_F11,
	K_F12, 0, 0, 0, 0, 0, 0, 0, // 5 
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, // 6 
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0 // 7 
};

byte shiftscantokey[128] =
{
	//  0           1       2       3       4       5       6       7 
	//  8           9       A       B       C       D       E       F 
	0, 27, '!', '@', '#', '$', '%', '^',
	'&', '*', '(', ')', '_', '+', K_BACKSPACE, 9, // 0 
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
	'O', 'P', '{', '}', 13, K_CTRL, 'A', 'S', // 1 
	'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
	'"', '~', K_SHIFT, '|', 'Z', 'X', 'C', 'V', // 2 
	'B', 'N', 'M', '<', '>', '?', K_SHIFT, '*',
	K_ALT, ' ', 0, K_F1, K_F2, K_F3, K_F4, K_F5, // 3 
	K_F6, K_F7, K_F8, K_F9, K_F10, K_PAUSE, 0, K_HOME,
	K_UPARROW,K_PGUP, '_',K_LEFTARROW, '%',K_RIGHTARROW, '+',K_END, //4 
	K_DOWNARROW,K_PGDN,K_INS,K_DEL, 0, 0, 0, K_F11,
	K_F12, 0, 0, 0, 0, 0, 0, 0, // 5 
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, // 6 
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0 // 7 
};


/*
=======
MapKey

Map from windows to quake keynums
=======
*/
int MapKey(int key)
{
	key = key >> 16 & 255;
	if (key > 127)
		return 0;
	if (scantokey[key] == 0)
		Con_DPrintf("key 0x%02x has no translation\n", key);
	return scantokey[key];
}

/*
===================================================================

MAIN WINDOW

===================================================================
*/

/*
================
ClearAllStates
================
*/
void ClearAllStates()
{
	// send an up event for each key, to make sure the server clears them all
	for (auto i = 0; i < 256; i++)
	{
		Key_Event(i, qfalse);
	}

	Key_ClearStates();
	IN_ClearStates();
}

void AppActivate(BOOL fActive, BOOL minimize)
/****************************************************************************
*
* Function:     AppActivate
* Parameters:   fActive - True if app is activating
*
* Description:  If the application is activating, then swap the system
*               into SYSPAL_NOSTATIC mode so that our palettes will display
*               correctly.
*
****************************************************************************/
{
	static BOOL sound_active;

	ActiveApp = fActive;
	Minimized = minimize;

	// enable/disable sound on focus gain/loss
	if (!ActiveApp && sound_active)
	{
		S_BlockSound();
		sound_active = qfalse;
	}
	else if (ActiveApp && !sound_active)
	{
		S_UnblockSound();
		sound_active = qtrue;
	}

	if (fActive)
	{
		if (modestate == modestate_t::MS_FULLDIB)
		{
			IN_ActivateMouse();
			IN_HideMouse();
			if (vid_canalttab && vid_wassuspended)
			{
				vid_wassuspended = qfalse;
				//ChangeDisplaySettings (&gdevmode, CDS_FULLSCREEN);
				ShowWindow(mainwindow, SW_SHOWNORMAL);
			}
		}
		else if (modestate == modestate_t::MS_WINDOWED && _windowed_mouse.value && key_dest == keydest_t::key_game)
		{
			IN_ActivateMouse();
			IN_HideMouse();
		}
	}

	if (!fActive)
	{
		if (modestate == modestate_t::MS_FULLDIB)
		{
			IN_DeactivateMouse();
			IN_ShowMouse();
			if (vid_canalttab)
			{
				ChangeDisplaySettings(nullptr, 0);
				vid_wassuspended = qtrue;
			}
		}
		else if (modestate == modestate_t::MS_WINDOWED && _windowed_mouse.value)
		{
			IN_DeactivateMouse();
			IN_ShowMouse();
		}
	}
}


/* main window procedure */
LONG WINAPI MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LONG                lRet = 1;
	extern unsigned int uiWheelMessage;

	if (uMsg == uiWheelMessage)
		uMsg = WM_MOUSEWHEEL;

	switch (uMsg)
	{
	case WM_KILLFOCUS:
		if (modestate == modestate_t::MS_FULLDIB)
			ShowWindow(mainwindow, SW_SHOWMINNOACTIVE);
		break;

	case WM_CREATE:
		break;

	case WM_MOVE:
		window_x = static_cast<int>(LOWORD(lParam));
		window_y = static_cast<int>(HIWORD(lParam));
		VID_UpdateWindowStatus();
		break;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		Key_Event(MapKey(lParam), qtrue);
		break;

	case WM_KEYUP:
	case WM_SYSKEYUP:
		Key_Event(MapKey(lParam), qfalse);
		break;

	case WM_SYSCHAR:
		// keep Alt-Space from happening
		break;

		// this is complicated because Win32 seems to pack multiple mouse events into
		// one update sometimes, so we always check all states and look for events
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		{
			auto temp = 0;

			if (wParam & MK_LBUTTON)
				temp |= 1;

			if (wParam & MK_RBUTTON)
				temp |= 2;

			if (wParam & MK_MBUTTON)
				temp |= 4;

			IN_MouseEvent(temp);
		}
		break;

		// JACK: This is the mouse wheel with the Intellimouse
		// Its delta is either positive or neg, and we generate the proper
		// Event.
	case WM_MOUSEWHEEL:
		if (static_cast<short>(HIWORD(wParam)) > 0)
		{
			Key_Event(K_MWHEELUP, qtrue);
			Key_Event(K_MWHEELUP, qfalse);
		}
		else
		{
			Key_Event(K_MWHEELDOWN, qtrue);
			Key_Event(K_MWHEELDOWN, qfalse);
		}
		break;

	case WM_SIZE:
		break;

	case WM_CLOSE:
		if (MessageBox(mainwindow, "Are you sure you want to quit?", "Confirm Exit",
		               MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION) == IDYES)
		{
			Sys_Quit();
		}

		break;

	case WM_ACTIVATE:
		{
			const int  fActive    = LOWORD(wParam);
			const auto fMinimized = static_cast<BOOL>(HIWORD(wParam));
			AppActivate(!(fActive == WA_INACTIVE), fMinimized);

			// fix the leftover Alt from any Alt-Tab or the like that switched us away
			ClearAllStates();
		}
		break;

	case WM_DESTROY:
		{
			if (dibwindow)
				DestroyWindow(dibwindow);

			PostQuitMessage(0);
		}
		break;

	case MM_MCINOTIFY:
		break;

	default:
		/* pass all unhandled messages to DefWindowProc */
		lRet = DefWindowProc(hWnd, uMsg, wParam, lParam);
		break;
	}

	/* return 1 if handled message, 0 if not */
	return lRet;
}


/*
=================
VID_NumModes
=================
*/
int VID_NumModes()
{
	return nummodes;
}


/*
=================
VID_GetModePtr
=================
*/
vmode_t* VID_GetModePtr(int modenum)
{
	if (modenum >= 0 && modenum < nummodes)
		return &modelist[modenum];
	return &badmode;
}


/*
=================
VID_GetModeDescription
=================
*/
char* VID_GetModeDescription(int mode)
{
	char*       pinfo;
	static char temp[100];

	if (mode < 0 || mode >= nummodes)
		return nullptr;

	if (!leavecurrentmode)
	{
		auto pv = VID_GetModePtr(mode);
		pinfo   = pv->modedesc;
	}
	else
	{
		sprintf(temp, "Desktop resolution (%dx%d)",
		        modelist[MODE_FULLSCREEN_DEFAULT].width,
		        modelist[MODE_FULLSCREEN_DEFAULT].height);
		pinfo = temp;
	}

	return pinfo;
}


// KJB: Added this to return the mode driver name in description for console

char* VID_GetExtModeDescription(int mode)
{
	static char pinfo[40];

	if (mode < 0 || mode >= nummodes)
		return nullptr;

	const auto pv = VID_GetModePtr(mode);
	if (modelist[mode].type == modestate_t::MS_FULLDIB)
	{
		if (!leavecurrentmode)
		{
			sprintf(pinfo, "%s fullscreen", pv->modedesc);
		}
		else
		{
			sprintf(pinfo, "Desktop resolution (%dx%d)",
			        modelist[MODE_FULLSCREEN_DEFAULT].width,
			        modelist[MODE_FULLSCREEN_DEFAULT].height);
		}
	}
	else
	{
		if (modestate == modestate_t::MS_WINDOWED)
			sprintf(pinfo, "%s windowed", pv->modedesc);
		else
			sprintf(pinfo, "windowed");
	}

	return pinfo;
}


/*
=================
VID_DescribeCurrentMode_f
=================
*/
void VID_DescribeCurrentMode_f()
{
	Con_Printf("%s\n", VID_GetExtModeDescription(vid_modenum));
}


/*
=================
VID_NumModes_f
=================
*/
void VID_NumModes_f()
{
	if (nummodes == 1)
		Con_Printf("%d video mode is available\n", nummodes);
	else
		Con_Printf("%d video modes are available\n", nummodes);
}


/*
=================
VID_DescribeMode_f
=================
*/
void VID_DescribeMode_f()
{
	const auto modenum = Q_atoi(Cmd_Argv(1));

	const int t            = leavecurrentmode;
	leavecurrentmode = 0;

	Con_Printf("%s\n", VID_GetExtModeDescription(modenum));

	leavecurrentmode = t;
}


/*
=================
VID_DescribeModes_f
=================
*/
void VID_DescribeModes_f()
{
	const auto lnummodes = VID_NumModes();

	const int t            = leavecurrentmode;
	leavecurrentmode = 0;

	for (auto i = 1; i < lnummodes; i++)
	{
		VID_GetModePtr(i);
		const auto pinfo = VID_GetExtModeDescription(i);
		Con_Printf("%2d: %s\n", i, pinfo);
	}

	leavecurrentmode = t;
}


void VID_InitDIB(HINSTANCE hInstance)
{
	WNDCLASS wc;

	/* Register the frame class */
	wc.style         = 0;
	wc.lpfnWndProc   = static_cast<WNDPROC>(MainWndProc);
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = nullptr;
	wc.hCursor       = LoadCursor(nullptr,IDC_ARROW);
	wc.hbrBackground = nullptr;
	wc.lpszMenuName  = nullptr;
	wc.lpszClassName = "WinQuake";

	if (!RegisterClass(&wc))
		Sys_Error("Couldn't register window class");

	modelist[0].type = modestate_t::MS_WINDOWED;

	if (COM_CheckParm("-width"))
		modelist[0].width = Q_atoi(com_argv[COM_CheckParm("-width") + 1]);
	else
		modelist[0].width = 640;

	if (modelist[0].width < 320)
		modelist[0].width = 320;

	if (COM_CheckParm("-height"))
		modelist[0].height = Q_atoi(com_argv[COM_CheckParm("-height") + 1]);
	else
		modelist[0].height = modelist[0].width * 240 / 320;

	if (modelist[0].height < 240)
		modelist[0].height = 240;

	sprintf(modelist[0].modedesc, "%dx%d",
	        modelist[0].width, modelist[0].height);

	modelist[0].modenum    = MODE_WINDOWED;
	modelist[0].dib        = 1;
	modelist[0].fullscreen = 0;
	modelist[0].halfscreen = 0;
	modelist[0].bpp        = 0;

	nummodes = 1;
}


/*
=================
VID_InitFullDIB
=================
*/
void VID_InitFullDIB(HINSTANCE hInstance)
{
	DEVMODE devmode;
	int     i;
	int     existingmode;
	BOOL    stat;

	// enumerate >8 bpp modes
	const auto originalnummodes = nummodes;
	auto modenum          = 0;

	do
	{
		stat = EnumDisplaySettings(nullptr, modenum, &devmode);

		if (devmode.dmBitsPerPel >= 15 &&
			devmode.dmPelsWidth <= MAXWIDTH &&
			devmode.dmPelsHeight <= MAXHEIGHT &&
			nummodes < MAX_MODE_LIST)
		{
			devmode.dmFields = DM_BITSPERPEL |
				DM_PELSWIDTH |
				DM_PELSHEIGHT;

			if (ChangeDisplaySettings(&devmode, CDS_TEST | CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL)
			{
				modelist[nummodes].type       = modestate_t::MS_FULLDIB;
				modelist[nummodes].width      = devmode.dmPelsWidth;
				modelist[nummodes].height     = devmode.dmPelsHeight;
				modelist[nummodes].modenum    = 0;
				modelist[nummodes].halfscreen = 0;
				modelist[nummodes].dib        = 1;
				modelist[nummodes].fullscreen = 1;
				modelist[nummodes].bpp        = devmode.dmBitsPerPel;
				sprintf(modelist[nummodes].modedesc, "%dx%dx%d",
				        devmode.dmPelsWidth, devmode.dmPelsHeight,
				        devmode.dmBitsPerPel);

				// if the width is more than twice the height, reduce it by half because this
				// is probably a dual-screen monitor
				if (!COM_CheckParm("-noadjustaspect"))
				{
					if (modelist[nummodes].width > modelist[nummodes].height << 1)
					{
						modelist[nummodes].width >>= 1;
						modelist[nummodes].halfscreen = 1;
						sprintf(modelist[nummodes].modedesc, "%dx%dx%d",
						        modelist[nummodes].width,
						        modelist[nummodes].height,
						        modelist[nummodes].bpp);
					}
				}

				for (i = originalnummodes, existingmode = 0; i < nummodes; i++)
				{
					if (modelist[nummodes].width == modelist[i].width &&
						modelist[nummodes].height == modelist[i].height &&
						modelist[nummodes].bpp == modelist[i].bpp)
					{
						existingmode = 1;
						break;
					}
				}

				if (!existingmode)
				{
					nummodes++;
				}
			}
		}

		modenum++;
	}
	while (stat);


	if (nummodes == originalnummodes)
		Con_SafePrintf("No fullscreen DIB modes found\n");
}

#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FB

static void Check_Gamma(unsigned char* pal)
{
	unsigned char palette[768];
	int           i;

	if ((i = COM_CheckParm("-gamma")) == 0)
	{
		if (gl_renderer && strstr(gl_renderer, "Voodoo") ||
			gl_vendor && strstr(gl_vendor, "3Dfx"))
			vid_gamma = 1;
		else
			vid_gamma = 0.7; // default to 0.7 on non-3dfx hardware
	}
	else
		vid_gamma = Q_atof(com_argv[i + 1]);

	for (i = 0; i < 768; i++)
	{
		const float f   = pow((pal[i] + 1) / 256.0, vid_gamma);
		float inf = f * 255 + 0.5;
		if (inf < 0)
			inf = 0;
		if (inf > 255)
			inf    = 255;
		palette[i] = inf;
	}

	memcpy(pal, palette, sizeof palette);
}

/*
===================
VID_Init
===================
*/
void VID_Init(unsigned char* palette)
{
	int     i;
	int     existingmode;
	int     width;
	auto    height = 0;
	int     bpp;
	int     findbpp;
	char    gldir[MAX_OSPATH];
	DEVMODE devmode;

	memset(&devmode, 0, sizeof devmode);

	Cvar_RegisterVariable(&vid_mode);
	Cvar_RegisterVariable(&vid_wait);
	Cvar_RegisterVariable(&vid_nopageflip);
	Cvar_RegisterVariable(&_vid_wait_override);
	Cvar_RegisterVariable(&_vid_default_mode);
	Cvar_RegisterVariable(&_vid_default_mode_win);
	Cvar_RegisterVariable(&vid_config_x);
	Cvar_RegisterVariable(&vid_config_y);
	Cvar_RegisterVariable(&vid_stretch_by_2);
	Cvar_RegisterVariable(&_windowed_mouse);
	Cvar_RegisterVariable(&gl_ztrick);

	Cmd_AddCommand("vid_nummodes", VID_NumModes_f);
	Cmd_AddCommand("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand("vid_describemode", VID_DescribeMode_f);
	Cmd_AddCommand("vid_describemodes", VID_DescribeModes_f);

	hIcon = LoadIcon(global_hInstance, IDI_WINLOGO);

	InitCommonControls();

	VID_InitDIB(global_hInstance);

	VID_InitFullDIB(global_hInstance);

	if (COM_CheckParm("-window"))
	{
		const auto hdc = GetDC(nullptr);

		if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE)
		{
			Sys_Error("Can't run in non-RGB mode");
		}

		ReleaseDC(nullptr, hdc);

		windowed = qtrue;

		vid_default = MODE_WINDOWED;
	}
	else
	{
		if (nummodes == 1)
			Sys_Error("No RGB fullscreen modes available");

		windowed = qfalse;

		if (COM_CheckParm("-mode"))
		{
			vid_default = Q_atoi(com_argv[COM_CheckParm("-mode") + 1]);
		}
		else
		{
			if (COM_CheckParm("-current"))
			{
				modelist[MODE_FULLSCREEN_DEFAULT].width  = GetSystemMetrics(SM_CXSCREEN);
				modelist[MODE_FULLSCREEN_DEFAULT].height = GetSystemMetrics(SM_CYSCREEN);
				vid_default                              = MODE_FULLSCREEN_DEFAULT;
				leavecurrentmode                         = 1;
			}
			else
			{
				if (COM_CheckParm("-width"))
				{
					width = Q_atoi(com_argv[COM_CheckParm("-width") + 1]);
				}
				else
				{
					width = 640;
				}

				if (COM_CheckParm("-bpp"))
				{
					bpp     = Q_atoi(com_argv[COM_CheckParm("-bpp") + 1]);
					findbpp = 0;
				}
				else
				{
					bpp     = 15;
					findbpp = 1;
				}

				if (COM_CheckParm("-height"))
					height = Q_atoi(com_argv[COM_CheckParm("-height") + 1]);

				// if they want to force it, add the specified mode to the list
				if (COM_CheckParm("-force") && nummodes < MAX_MODE_LIST)
				{
					modelist[nummodes].type       = modestate_t::MS_FULLDIB;
					modelist[nummodes].width      = width;
					modelist[nummodes].height     = height;
					modelist[nummodes].modenum    = 0;
					modelist[nummodes].halfscreen = 0;
					modelist[nummodes].dib        = 1;
					modelist[nummodes].fullscreen = 1;
					modelist[nummodes].bpp        = bpp;
					sprintf(modelist[nummodes].modedesc, "%dx%dx%d",
					        devmode.dmPelsWidth, devmode.dmPelsHeight,
					        devmode.dmBitsPerPel);

					for (i = nummodes, existingmode = 0; i < nummodes; i++)
					{
						if (modelist[nummodes].width == modelist[i].width &&
							modelist[nummodes].height == modelist[i].height &&
							modelist[nummodes].bpp == modelist[i].bpp)
						{
							existingmode = 1;
							break;
						}
					}

					if (!existingmode)
					{
						nummodes++;
					}
				}

				auto done = 0;

				do
				{
					if (COM_CheckParm("-height"))
					{
						height = Q_atoi(com_argv[COM_CheckParm("-height") + 1]);

						for (i = 1, vid_default = 0; i < nummodes; i++)
						{
							if (modelist[i].width == width &&
								modelist[i].height == height &&
								modelist[i].bpp == bpp)
							{
								vid_default = i;
								done        = 1;
								break;
							}
						}
					}
					else
					{
						for (i = 1, vid_default = 0; i < nummodes; i++)
						{
							if (modelist[i].width == width && modelist[i].bpp == bpp)
							{
								vid_default = i;
								done        = 1;
								break;
							}
						}
					}

					if (!done)
					{
						if (findbpp)
						{
							switch (bpp)
							{
							case 15:
								bpp = 16;
								break;
							case 16:
								bpp = 32;
								break;
							case 32:
								bpp = 24;
								break;
							case 24:
								done = 1;
								break;
							default: break;
							}
						}
						else
						{
							done = 1;
						}
					}
				}
				while (!done);

				if (!vid_default)
				{
					Sys_Error("Specified video mode not available");
				}
			}
		}
	}

	vid_initialized = qtrue;

	if ((i           = COM_CheckParm("-conwidth")) != 0)
		vid.conwidth = Q_atoi(com_argv[i + 1]);
	else
		vid.conwidth = 640;

	vid.conwidth &= 0xfff8; // make it a multiple of eight

	if (vid.conwidth < 320)
		vid.conwidth = 320;

	// pick a conheight that matches with correct aspect
	vid.conheight = vid.conwidth * 3 / 4;

	if ((i            = COM_CheckParm("-conheight")) != 0)
		vid.conheight = Q_atoi(com_argv[i + 1]);
	if (vid.conheight < 200)
		vid.conheight = 200;

	vid.maxwarpwidth  = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap      = host_colormap;
	vid.fullbright    = 256 - LittleLong(*(reinterpret_cast<int *>(vid.colormap) + 2048));

	Check_Gamma(palette);
	VID_SetPalette(palette);

	VID_SetMode(vid_default, palette);

	maindc = GetDC(mainwindow);
	bSetupPixelFormat(maindc);

	baseRC = wglCreateContext(maindc);
	if (!baseRC)
		Sys_Error("Could not initialize GL (wglCreateContext failed).\n\nMake sure you in are 65535 color mode, and try running -window.");
	if (!wglMakeCurrent(maindc, baseRC))
		Sys_Error("wglMakeCurrent failed");

	GL_Init();

	sprintf(gldir, "%s/glquake", com_gamedir);
	Sys_mkdir(gldir);

	vid_realmode = vid_modenum;


	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn  = VID_MenuKey;

	strcpy(badmode.modedesc, "Bad mode");
	vid_canalttab = qtrue;

	if (COM_CheckParm("-fullsbar"))
		fullsbardraw = qtrue;
}


//========================================================
// Video menu stuff
//========================================================

extern void M_Menu_Options_f();
extern void M_Print(int         cx, int cy, char*  str);
extern void M_PrintWhite(int    cx, int cy, char*  str);
extern void M_DrawCharacter(int cx, int line, int  num);
extern void M_DrawTransPic(int  x, int  y, qpic_t* pic);
extern void M_DrawPic(int       x, int  y, qpic_t* pic);

static int vid_line, vid_wmodes;

struct modedesc_t
{
	int   modenum;
	char* desc;
	int   iscur;
};

#define MAX_COLUMN_SIZE		9
#define MODE_AREA_HEIGHT	(MAX_COLUMN_SIZE + 2)
#define MAX_MODEDESCS		(MAX_COLUMN_SIZE*3)

static modedesc_t modedescs[MAX_MODEDESCS];

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw()
{
	int i;

	const auto p = Draw_CachePic("gfx/vidmodes.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);

	vid_wmodes     = 0;
	const auto lnummodes = VID_NumModes();

	for (i = 1; i < lnummodes && vid_wmodes < MAX_MODEDESCS; i++)
	{
		const auto ptr = VID_GetModeDescription(i);
		VID_GetModePtr(i);

		const auto k = vid_wmodes;

		modedescs[k].modenum = i;
		modedescs[k].desc    = ptr;
		modedescs[k].iscur   = 0;

		if (i == vid_modenum)
			modedescs[k].iscur = 1;

		vid_wmodes++;
	}

	if (vid_wmodes > 0)
	{
		M_Print(2 * 8, 36 + 0 * 8, "Fullscreen Modes (WIDTHxHEIGHTxBPP)");

		auto column = 8;
		auto row    = 36 + 2 * 8;

		for (i = 0; i < vid_wmodes; i++)
		{
			if (modedescs[i].iscur)
				M_PrintWhite(column, row, modedescs[i].desc);
			else
				M_Print(column, row, modedescs[i].desc);

			column += 13 * 8;

			if (i % VID_ROW_SIZE == VID_ROW_SIZE - 1)
			{
				column = 8;
				row += 8;
			}
		}
	}

	M_Print(3 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 2,
	        "Video modes must be set from the");
	M_Print(3 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 3,
	        "command line with -width <width>");
	M_Print(3 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 4,
	        "and -bpp <bits-per-pixel>");
	M_Print(3 * 8, 36 + MODE_AREA_HEIGHT * 8 + 8 * 6,
	        "Select windowed mode with -window");
}


/*
================
VID_MenuKey
================
*/
void VID_MenuKey(int key)
{
	switch (key)
	{
	case K_ESCAPE:
		S_LocalSound("misc/menu1.wav");
		M_Menu_Options_f();
		break;

	default:
		break;
	}
}
