#pragma once

#if !defined BYTE_DEFINED
struct cache_user_t;
using byte = uint8_t;
#define BYTE_DEFINED 1
#endif

#undef true
#undef false

using qboolean = uint32_t;
static constexpr qboolean qfalse = 0u;
static constexpr qboolean qtrue  = 1u;

//============================================================================

struct sizebuf_t
{
	qboolean allowoverflow; // if qfalse, do a Sys_Error
	qboolean overflowed; // set to qtrue if the buffer size failed
	byte*    data;
	int      maxsize;
	int      cursize;
};

void  SZ_Alloc(sizebuf_t*    buf, int startsize);
void  SZ_Free(sizebuf_t*     buf);
void  SZ_Clear(sizebuf_t*    buf);
void* SZ_GetSpace(sizebuf_t* buf, int   length);
void  SZ_Write(sizebuf_t*    buf, void* data, int length);
void  SZ_Print(sizebuf_t*    buf, char* data); // strcats onto the sizebuf

//============================================================================

struct link_t
{
	link_t *prev, *next;
};


void ClearLink(link_t*        l);
void RemoveLink(link_t*       l);
void InsertLinkBefore(link_t* l, link_t* before);
void InsertLinkAfter(link_t*  l, link_t* after);

// (type *)STRUCT_FROM_LINK(link_t *link, type, member)
// ent = STRUCT_FROM_LINK(link,entity_t,order)
// FIXME: remove this mess!
#define	STRUCT_FROM_LINK(l,t,m) ((t *)((byte *)l - (int)&(((t *)0)->m)))

//============================================================================


#define Q_MAXCHAR ((char)0x7f)
#define Q_MAXSHORT ((short)0x7fff)
#define Q_MAXINT	((int)0x7fffffff)
#define Q_MAXLONG ((int)0x7fffffff)
#define Q_MAXFLOAT ((int)0x7fffffff)

#define Q_MINCHAR ((char)0x80)
#define Q_MINSHORT ((short)0x8000)
#define Q_MININT 	((int)0x80000000)
#define Q_MINLONG ((int)0x80000000)
#define Q_MINFLOAT ((int)0x7fffffff)

//============================================================================

extern qboolean bigendien;

extern short (*BigShort)(short    l);
extern short (*LittleShort)(short l);
extern int (*  BigLong)(int       l);
extern int (*  LittleLong)(int    l);
extern float (*BigFloat)(float    l);
extern float (*LittleFloat)(float l);

//============================================================================

void MSG_WriteChar(sizebuf_t*   sb, int   c);
void MSG_WriteByte(sizebuf_t*   sb, int   c);
void MSG_WriteShort(sizebuf_t*  sb, int   c);
void MSG_WriteLong(sizebuf_t*   sb, int   c);
void MSG_WriteFloat(sizebuf_t*  sb, float f);
void MSG_WriteString(sizebuf_t* sb, char* s);
void MSG_WriteCoord(sizebuf_t*  sb, float f);
void MSG_WriteAngle(sizebuf_t*  sb, float f);

extern int      msg_readcount;
extern qboolean msg_badread; // set if a read goes beyond end of message

void  MSG_BeginReading();
int   MSG_ReadChar();
int   MSG_ReadByte();
int   MSG_ReadShort();
int   MSG_ReadLong();
float MSG_ReadFloat();
char* MSG_ReadString();

float MSG_ReadCoord();
float MSG_ReadAngle();

//============================================================================

void  Q_memset(void*            dest, int   fill, int count);
void  Q_memcpy(void*            dest, void* src, int  count);
int   Q_memcmp(void*            m1, void*   m2, int   count);
void  Q_strcpy(char*            dest, char* src);
void  Q_strncpy(char*           dest, char* src, int count);
int   Q_strlen(char*            str);
char* Q_strrchr(char*           s, char     c);
void  Q_strcat(char*            dest, char* src);
int   Q_strcmp(char*            s1, char*   s2);
int   Q_strncmp(char*           s1, char*   s2, int count);
int   Q_strcasecmp(char*        s1, char*   s2);
int   Q_strncasecmp(const char* s1, char*   s2, int n);
int   Q_atoi(char*              str);
float Q_atof(char*              str);

//============================================================================

extern char     com_token[1024];
extern qboolean com_eof;

char* COM_Parse(char* data);


extern int    com_argc;
extern char** com_argv;

int  COM_CheckParm(char* parm);
void COM_Init(char*      path);
void COM_InitArgv(int    argc, char** argv);

char* COM_SkipPath(char*         pathname);
void  COM_StripExtension(char*   in, char*   out);
void  COM_FileBase(char*         in, char*   out);
void  COM_DefaultExtension(char* path, char* extension);

char* va(char* format, ...);
// does a varargs printf into a temp buffer


//============================================================================

extern int com_filesize;
struct cache_user_s;

extern char com_gamedir[MAX_OSPATH];

void COM_WriteFile(char* filename, void*  data, int len);
int  COM_OpenFile(char*  filename, int*   hndl);
int  COM_FOpenFile(char* filename, FILE** file);
void COM_CloseFile(int   h);

byte* COM_LoadStackFile(char* path, void* buffer, int bufsize);
byte* COM_LoadTempFile(char*  path);
byte* COM_LoadHunkFile(char*  path);
void  COM_LoadCacheFile(char* path, cache_user_t* cu);

extern qboolean standard_quake, rogue, hipnotic;

void Memory_Init(void* buf, int size);

void  Z_Free(void*    ptr);
void* Z_Malloc(int    size); // returns 0 filled memory
void* Z_TagMalloc(int size, int tag);

void Z_DumpHeap();
void Z_CheckHeap();
int  Z_FreeMemory();

void* Hunk_Alloc(int     size); // returns 0 filled memory
void* Hunk_AllocName(int size, char* name);

void* Hunk_HighAllocName(int size, char* name);

int  Hunk_LowMark();
void Hunk_FreeToLowMark(int mark);

int  Hunk_HighMark();
void Hunk_FreeToHighMark(int mark);

void* Hunk_TempAlloc(int size);

void Hunk_Check();

struct cache_user_t
{
	void* data;
};

void Cache_Flush();

void* Cache_Check(cache_user_t* c);
// returns the cached data, and moves to the head of the LRU list
// if present, otherwise returns nullptr

void Cache_Free(cache_user_t* c);

void* Cache_Alloc(cache_user_t* c, int size, char* name);
// Returns nullptr if all purgable data was tossed and there still
// wasn't enough room.

void Cache_Report();

void           CRC_Init(unsigned short*        crcvalue);
void           CRC_ProcessByte(unsigned short* crcvalue, byte data);
unsigned short CRC_Value(unsigned short        crcvalue);
