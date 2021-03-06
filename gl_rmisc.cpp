#include "quakedef.h"


/*
==================
R_InitTextures
==================
*/
void R_InitTextures()
{
	// create a simple checkerboard texture for the default
	r_notexture_mip             = static_cast<texture_t*>(Hunk_AllocName(sizeof(texture_t) + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2, "notexture"));
	r_notexture_mip->width      = r_notexture_mip->height = 16;
	r_notexture_mip->offsets[0] = sizeof(texture_t);
	r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16 * 16;
	r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8 * 8;
	r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4 * 4;

	for (auto m = 0; m < 4; m++)
	{
		auto          dest = reinterpret_cast<byte *>(r_notexture_mip) + r_notexture_mip->offsets[m];
		for (auto     y    = 0; y < 16 >> m; y++)
			for (auto x    = 0; x < 16 >> m; x++)
			{
				if (y < 8 >> m ^ x < 8 >> m)
					*dest++ = 0;
				else
					*dest++ = 0xff;
			}
	}
}

byte dottexture[8][8] =
{
	{0, 1, 1, 0, 0, 0, 0, 0},
	{1, 1, 1, 1, 0, 0, 0, 0},
	{1, 1, 1, 1, 0, 0, 0, 0},
	{0, 1, 1, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0},
};

void R_InitParticleTexture()
{
	byte data[8][8][4];

	//
	// particle texture
	//
	particletexture = texture_extension_number++;
	GL_Bind(particletexture);

	for (auto x = 0; x < 8; x++)
	{
		for (auto y = 0; y < 8; y++)
		{
			data[y][x][0] = 255;
			data[y][x][1] = 255;
			data[y][x][2] = 255;
			data[y][x][3] = dottexture[x][y] * 255;
		}
	}
	glTexImage2D(GL_TEXTURE_2D, 0, gl_alpha_format, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

/*
===============
R_Envmap_f

Grab six views for environment mapping tests
===============
*/
void R_Envmap_f()
{
	byte buffer[256 * 256 * 4];

	glDrawBuffer(GL_FRONT);
	glReadBuffer(GL_FRONT);
	envmap = qtrue;

	r_refdef.vrect.x      = 0;
	r_refdef.vrect.y      = 0;
	r_refdef.vrect.width  = 256;
	r_refdef.vrect.height = 256;

	r_refdef.viewangles[0] = 0;
	r_refdef.viewangles[1] = 0;
	r_refdef.viewangles[2] = 0;
	GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
	R_RenderView();
	glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile("env0.rgb", buffer, sizeof buffer);

	r_refdef.viewangles[1] = 90;
	GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
	R_RenderView();
	glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile("env1.rgb", buffer, sizeof buffer);

	r_refdef.viewangles[1] = 180;
	GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
	R_RenderView();
	glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile("env2.rgb", buffer, sizeof buffer);

	r_refdef.viewangles[1] = 270;
	GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
	R_RenderView();
	glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile("env3.rgb", buffer, sizeof buffer);

	r_refdef.viewangles[0] = -90;
	r_refdef.viewangles[1] = 0;
	GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
	R_RenderView();
	glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile("env4.rgb", buffer, sizeof buffer);

	r_refdef.viewangles[0] = 90;
	r_refdef.viewangles[1] = 0;
	GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
	R_RenderView();
	glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile("env5.rgb", buffer, sizeof buffer);

	envmap = qfalse;
	glDrawBuffer(GL_BACK);
	glReadBuffer(GL_BACK);
	GL_EndRendering();
}

/*
===============
R_Init
===============
*/
void R_Init()
{
	Cmd_AddCommand("timerefresh", R_TimeRefresh_f);
	Cmd_AddCommand("envmap", R_Envmap_f);

	Cvar_RegisterVariable(&r_norefresh);
	Cvar_RegisterVariable(&r_lightmap);
	Cvar_RegisterVariable(&r_fullbright);
	Cvar_RegisterVariable(&r_drawentities);
	Cvar_RegisterVariable(&r_drawviewmodel);
	Cvar_RegisterVariable(&r_shadows);
	Cvar_RegisterVariable(&r_mirroralpha);
	Cvar_RegisterVariable(&r_wateralpha);
	Cvar_RegisterVariable(&r_dynamic);
	Cvar_RegisterVariable(&r_novis);
	Cvar_RegisterVariable(&r_speeds);

	Cvar_RegisterVariable(&gl_finish);
	Cvar_RegisterVariable(&gl_clear);
	Cvar_RegisterVariable(&gl_texsort);

	if (gl_mtexable)
		Cvar_SetValue("gl_texsort", 0.0);

	Cvar_RegisterVariable(&gl_cull);
	Cvar_RegisterVariable(&gl_smoothmodels);
	Cvar_RegisterVariable(&gl_affinemodels);
	Cvar_RegisterVariable(&gl_polyblend);
	Cvar_RegisterVariable(&gl_flashblend);
	Cvar_RegisterVariable(&gl_playermip);
	Cvar_RegisterVariable(&gl_nocolors);

	Cvar_RegisterVariable(&gl_keeptjunctions);
	Cvar_RegisterVariable(&gl_reporttjunctions);

	Cvar_RegisterVariable(&gl_doubleeyes);

	R_InitParticles();
	R_InitParticleTexture();

#ifdef GLTEST
	Test_Init ();
#endif

	playertextures = texture_extension_number;
	texture_extension_number += 16;
}

/*
===============
R_TranslatePlayerSkin

Translates a skin texture by the per-player color lookup
===============
*/
void R_TranslatePlayerSkin(int playernum)
{
	byte     translate[256];
	unsigned translate32[256];
	int      i;
	byte*    original;
	unsigned pixels[512 * 256];

	GL_DisableMultitexture();

	const auto top    = cl.scores[playernum].colors & 0xf0;
	const auto bottom = (cl.scores[playernum].colors & 15) << 4;

	for (i           = 0; i < 256; i++)
		translate[i] = i;

	for (i = 0; i < 16; i++)
	{
		if (top < 128) // the artists made some backwards ranges.  sigh.
			translate[TOP_RANGE + i] = top + i;
		else
			translate[TOP_RANGE + i] = top + 15 - i;

		if (bottom < 128)
			translate[BOTTOM_RANGE + i] = bottom + i;
		else
			translate[BOTTOM_RANGE + i] = bottom + 15 - i;
	}

	//
	// locate the original skin pixels
	//
	currententity = &cl_entities[1 + playernum];
	const auto model    = currententity->model;
	if (!model)
		return; // player doesn't have a model yet
	if (model->type != modtype_t::mod_alias)
		return; // only translate skins on alias models

	auto paliashdr = static_cast<aliashdr_t *>(Mod_Extradata(model));
	const auto s         = paliashdr->skinwidth * paliashdr->skinheight;
	if (currententity->skinnum < 0 || currententity->skinnum >= paliashdr->numskins)
	{
		Con_Printf("(%d): Invalid player skin #%d\n", playernum, currententity->skinnum);
		original = reinterpret_cast<byte *>(paliashdr) + paliashdr->texels[0];
	}
	else
		original = reinterpret_cast<byte *>(paliashdr) + paliashdr->texels[currententity->skinnum];
	if (s & 3)
		Sys_Error("R_TranslateSkin: s&3");

	const auto inwidth  = paliashdr->skinwidth;
	const auto inheight = paliashdr->skinheight;

	// because this happens during gameplay, do it fast
	// instead of sending it through gl_upload 8
	GL_Bind(playertextures + playernum);

	unsigned scaled_width  = gl_max_size.value < 512 ? gl_max_size.value : 512;
	unsigned scaled_height = gl_max_size.value < 256 ? gl_max_size.value : 256;

	// allow users to crunch sizes down even more if they want
	scaled_width >>= static_cast<int>(gl_playermip.value);
	scaled_height >>= static_cast<int>(gl_playermip.value);

	for (i             = 0; i < 256; i++)
		translate32[i] = d_8to24table[translate[i]];

	auto out      = pixels;
	const auto fracstep = inwidth * 0x10000 / scaled_width;
	for (i        = 0; i < scaled_height; i++, out += scaled_width)
	{
		const auto      inrow = original + inwidth * (i * inheight / scaled_height);
		auto      frac  = fracstep >> 1;
		for (auto j     = 0; j < scaled_width; j += 4)
		{
			out[j] = translate32[inrow[frac >> 16]];
			frac += fracstep;
			out[j + 1] = translate32[inrow[frac >> 16]];
			frac += fracstep;
			out[j + 2] = translate32[inrow[frac >> 16]];
			frac += fracstep;
			out[j + 3] = translate32[inrow[frac >> 16]];
			frac += fracstep;
		}
	}

	glTexImage2D(GL_TEXTURE_2D, 0, gl_solid_format, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}


/*
===============
R_NewMap
===============
*/
void R_NewMap()
{
	int i;

	for (i                   = 0; i < 256; i++)
		d_lightstylevalue[i] = 264; // normal light value

	memset(&r_worldentity, 0, sizeof r_worldentity);
	r_worldentity.model = cl.worldmodel;

	// clear out efrags in case the level hasn't been reloaded
	// FIXME: is this one short?
	for (i                             = 0; i < cl.worldmodel->numleafs; i++)
		cl.worldmodel->leafs[i].efrags = nullptr;

	r_viewleaf = nullptr;
	R_ClearParticles();

	GL_BuildLightmaps();

	// identify sky texture
	skytexturenum    = -1;
	mirrortexturenum = -1;
	for (i           = 0; i < cl.worldmodel->numtextures; i++)
	{
		if (!cl.worldmodel->textures[i])
			continue;
		if (!Q_strncmp(cl.worldmodel->textures[i]->name, "sky", 3))
			skytexturenum = i;
		if (!Q_strncmp(cl.worldmodel->textures[i]->name, "window02_1", 10))
			mirrortexturenum                     = i;
		cl.worldmodel->textures[i]->texturechain = nullptr;
	}
}


/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f()
{
	glDrawBuffer(GL_FRONT);
	glFinish();

	const float     start = Sys_FloatTime();
	for (auto i     = 0; i < 128; i++)
	{
		r_refdef.viewangles[1] = i / 128.0 * 360.0;
		R_RenderView();
	}

	glFinish();
	const float stop = Sys_FloatTime();
	const auto  time = stop - start;
	Con_Printf("%f seconds (%f fps)\n", time, 128 / time);

	glDrawBuffer(GL_BACK);
	GL_EndRendering();
}
