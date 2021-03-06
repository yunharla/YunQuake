#include "quakedef.h"

int cache_full_cycle;

/*
================
ResampleSfx
================
*/
void ResampleSfx(sfx_t* sfx, const int inrate, const int inwidth, byte* data)
{
	int i;
	int sample;

	const auto sc = static_cast<sfxcache_t *>(Cache_Check(&sfx->cache));
	if (!sc)
		return;

	const auto stepscale = static_cast<float>(inrate) / shm->speed; // this is usually 0.5, 1, or 2
	const int  outcount  = sc->length / stepscale;

	sc->length = outcount;
	if (sc->loopstart != -1)
		sc->loopstart = sc->loopstart / stepscale;

	sc->speed = shm->speed;
	if (loadas8bit.value)
		sc->width = 1;
	else
		sc->width = inwidth;
	sc->stereo    = 0;

	// resample / decimate to the current source rate

	if (stepscale == 1 && inwidth == 1 && sc->width == 1)
	{
		// fast special case
		for (i                                           = 0; i < outcount; i++)
			reinterpret_cast<signed char *>(sc->data)[i] = static_cast<int>(static_cast<unsigned char>(data[i]) - 128);
	}
	else
	{
		// general case
		auto      samplefrac = 0;
		const int fracstep   = stepscale * 256;
		for (i               = 0; i < outcount; i++)
		{
			const auto srcsample = samplefrac >> 8;
			samplefrac += fracstep;
			if (inwidth == 2)
				sample = LittleShort(reinterpret_cast<short *>(data)[srcsample]);
			else
				sample = static_cast<int>(static_cast<unsigned char>(data[srcsample]) - 128) << 8;
			if (sc->width == 2)
				reinterpret_cast<short *>(sc->data)[i] = sample;
			else
				reinterpret_cast<signed char *>(sc->data)[i] = sample >> 8;
		}
	}
}

//=============================================================================

/*
==============
S_LoadSound
==============
*/
sfxcache_t* S_LoadSound(sfx_t* s)
{
	char namebuffer[256];
	byte stackbuf[1 * 1024]; // avoid dirtying the cache heap

	// see if still in memory
	auto sc = static_cast<sfxcache_t *>(Cache_Check(&s->cache));
	if (sc)
		return sc;

	//Con_Printf ("S_LoadSound: %x\n", (int)stackbuf);
	// load it in
	Q_strcpy(namebuffer, "sound/");
	Q_strcat(namebuffer, s->name);

	//	Con_Printf ("loading %s\n",namebuffer);

	const auto data = COM_LoadStackFile(namebuffer, stackbuf, sizeof stackbuf);

	if (!data)
	{
		Con_Printf("Couldn't load %s\n", namebuffer);
		return nullptr;
	}

	const auto info = GetWavinfo(s->name, data, com_filesize);
	if (info.channels != 1)
	{
		Con_Printf("%s is a stereo sample\n", s->name);
		return nullptr;
	}

	const auto stepscale = static_cast<float>(info.rate) / shm->speed;
	int        len       = info.samples / stepscale;

	len = len * info.width * info.channels;

	sc = static_cast<sfxcache_t *>(Cache_Alloc(&s->cache, len + sizeof(sfxcache_t), s->name));
	if (!sc)
		return nullptr;

	sc->length    = info.samples;
	sc->loopstart = info.loopstart;
	sc->speed     = info.rate;
	sc->width     = info.width;
	sc->stereo    = info.channels;

	ResampleSfx(s, sc->speed, sc->width, data + info.dataofs);

	return sc;
}


/*
===============================================================================

WAV loading

===============================================================================
*/


byte* data_p;
byte* iff_end;
byte* last_chunk;
byte* iff_data;
int   iff_chunk_len;


short GetLittleShort()
{
	short val = *data_p;
	val       = val + (*(data_p + 1) << 8);
	data_p += 2;
	return val;
}

int GetLittleLong()
{
	int val = *data_p;
	val     = val + (*(data_p + 1) << 8);
	val     = val + (*(data_p + 2) << 16);
	val     = val + (*(data_p + 3) << 24);
	data_p += 4;
	return val;
}

void FindNextChunk(char* name)
{
	while (true)
	{
		data_p = last_chunk;

		if (data_p >= iff_end)
		{
			// didn't find the chunk
			data_p = nullptr;
			return;
		}

		data_p += 4;
		iff_chunk_len = GetLittleLong();
		if (iff_chunk_len < 0)
		{
			data_p = nullptr;
			return;
		}
		//		if (iff_chunk_len > 1024*1024)
		//			Sys_Error ("FindNextChunk: %i length is past the 1 meg sanity limit", iff_chunk_len);
		data_p -= 8;
		last_chunk = data_p + 8 + (iff_chunk_len + 1 & ~1);
		if (!Q_strncmp(reinterpret_cast<char*>(data_p), name, 4))
			return;
	}
}

void FindChunk(char* name)
{
	last_chunk = iff_data;
	FindNextChunk(name);
}


void DumpChunks()
{
	char str[5];

	str[4] = 0;
	data_p = iff_data;
	do
	{
		memcpy(str, data_p, 4);
		data_p += 4;
		iff_chunk_len = GetLittleLong();
		Con_Printf("0x%x : %s (%d)\n", reinterpret_cast<int>(data_p - 4), str, iff_chunk_len);
		data_p += iff_chunk_len + 1 & ~1;
	}
	while (data_p < iff_end);
}

/*
============
GetWavinfo
============
*/
wavinfo_t GetWavinfo(char* name, byte* wav, const int wavlength)
{
	wavinfo_t info;

	memset(&info, 0, sizeof info);

	if (!wav)
		return info;

	iff_data = wav;
	iff_end  = wav + wavlength;

	// find "RIFF" chunk
	FindChunk("RIFF");
	if (!(data_p && !Q_strncmp(reinterpret_cast<char*>(data_p + 8), "WAVE", 4)))
	{
		Con_Printf("Missing RIFF/WAVE chunks\n");
		return info;
	}

	// get "fmt " chunk
	iff_data = data_p + 12;
	// DumpChunks ();

	FindChunk("fmt ");
	if (!data_p)
	{
		Con_Printf("Missing fmt chunk\n");
		return info;
	}
	data_p += 8;
	const int format = GetLittleShort();
	if (format != 1)
	{
		Con_Printf("Microsoft PCM format only\n");
		return info;
	}

	info.channels = GetLittleShort();
	info.rate     = GetLittleLong();
	data_p += 4 + 2;
	info.width = GetLittleShort() / 8;

	// get cue chunk
	FindChunk("cue ");
	if (data_p)
	{
		data_p += 32;
		info.loopstart = GetLittleLong();
		//		Con_Printf("loopstart=%d\n", sfx->loopstart);

		// if the next chunk is a LIST chunk, look for a cue length marker
		FindNextChunk("LIST");
		if (data_p)
		{
			if (!strncmp(reinterpret_cast<char*>(data_p + 28), "mark", 4))
			{
				// this is not a proper parse, but it works with cooledit...
				data_p += 24;
				const auto i = GetLittleLong(); // samples in loop
				info.samples = info.loopstart + i;
				//				Con_Printf("looped length: %i\n", i);
			}
		}
	}
	else
		info.loopstart = -1;

	// find data chunk
	FindChunk("data");
	if (!data_p)
	{
		Con_Printf("Missing data chunk\n");
		return info;
	}

	data_p += 4;
	const auto samples = GetLittleLong() / info.width;

	if (info.samples)
	{
		if (samples < info.samples)
			Sys_Error("Sound %s has a bad loop length", name);
	}
	else
		info.samples = samples;

	info.dataofs = data_p - wav;

	return info;
}
