#include "quakedef.h"

int skytexturenum;

#ifndef GL_RGBA4
#define	GL_RGBA4	0
#endif


int lightmap_bytes; // 1, 2, or 4

int lightmap_textures;

unsigned blocklights[18 * 18];

#define	BLOCK_WIDTH		128
#define	BLOCK_HEIGHT	128

#define	MAX_LIGHTMAPS	64
int active_lightmaps;

struct glRect_t
{
	unsigned char l, t, w, h;
};

glpoly_t* lightmap_polys[MAX_LIGHTMAPS];
qboolean lightmap_modified[MAX_LIGHTMAPS];
glRect_t lightmap_rectchange[MAX_LIGHTMAPS];

int allocated[MAX_LIGHTMAPS][BLOCK_WIDTH];

// the lightmap texture data needs to be kept in
// main memory so texsubimage can update properly
byte lightmaps[4 * MAX_LIGHTMAPS * BLOCK_WIDTH * BLOCK_HEIGHT];

// For gl_texsort 0
msurface_t* skychain = nullptr;
msurface_t* waterchain = nullptr;

void R_RenderDynamicLightmaps(msurface_t* fa);

/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights(msurface_t* surf)
{
	int lnum;
	vec3_t impact, local;
	mtexinfo_t* tex;

	auto smax = (surf->extents[0] >> 4) + 1;
	auto tmax = (surf->extents[1] >> 4) + 1;
	tex = surf->texinfo;

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		if (!(surf->dlightbits & 1 << lnum))
			continue; // not lit by this light

		auto rad = cl_dlights[lnum].radius;
		auto dist = DotProduct (cl_dlights[lnum].origin, surf->plane->normal) - surf->plane->dist;
		rad -= fabs(dist);
		auto minlight = cl_dlights[lnum].minlight;
		if (rad < minlight)
			continue;
		minlight = rad - minlight;

		for (auto i = 0; i < 3; i++)
		{
			impact[i] = cl_dlights[lnum].origin[i] - surf->plane->normal[i] * dist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];

		for (auto t = 0; t < tmax; t++)
		{
			int td = local[1] - t * 16;
			if (td < 0)
				td = -td;
			for (auto s = 0; s < smax; s++)
			{
				int sd = local[0] - s * 16;
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td >> 1);
				else
					dist = td + (sd >> 1);
				if (dist < minlight)
					blocklights[t * smax + s] += (rad - dist) * 256;
			}
		}
	}
}


/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
void R_BuildLightMap(msurface_t* surf, byte* dest, int stride)
{
	int t;
	int i, j;
	unsigned* bl;

	surf->cached_dlight = surf->dlightframe == r_framecount;

	auto smax = (surf->extents[0] >> 4) + 1;
	auto tmax = (surf->extents[1] >> 4) + 1;
	auto size = smax * tmax;
	auto lightmap = surf->samples;

	// set to full bright if no light data
	if (r_fullbright.value || !cl.worldmodel->lightdata)
	{
		for (i = 0; i < size; i++)
			blocklights[i] = 255 * 256;
		goto store;
	}

	// clear to no light
	for (i = 0; i < size; i++)
		blocklights[i] = 0;

	// add all the lightmaps
	if (lightmap)
		for (auto maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255;
		     maps++)
		{
			unsigned scale = d_lightstylevalue[surf->styles[maps]];
			surf->cached_light[maps] = scale; // 8.8 fraction
			for (i = 0; i < size; i++)
				blocklights[i] += lightmap[i] * scale;
			lightmap += size; // skip to next lightmap
		}

	// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLights(surf);

	// bound, invert, and shift
store:
	switch (gl_lightmap_format)
	{
	case GL_RGBA:
		stride -= smax << 2;
		bl = blocklights;
		for (i = 0; i < tmax; i++ , dest += stride)
		{
			for (j = 0; j < smax; j++)
			{
				t = *bl++;
				t >>= 7;
				if (t > 255)
					t = 255;
				dest[3] = 255 - t;
				dest += 4;
			}
		}
		break;
	case GL_ALPHA:
	case GL_LUMINANCE:
	case GL_INTENSITY:
		bl = blocklights;
		for (i = 0; i < tmax; i++ , dest += stride)
		{
			for (j = 0; j < smax; j++)
			{
				t = *bl++;
				t >>= 7;
				if (t > 255)
					t = 255;
				dest[j] = 255 - t;
			}
		}
		break;
	default:
		Sys_Error("Bad lightmap format");
	}
}


/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
texture_t* R_TextureAnimation(texture_t* base)
{
	if (currententity->frame)
	{
		if (base->alternate_anims)
			base = base->alternate_anims;
	}

	if (!base->anim_total)
		return base;

	auto reletive = static_cast<int>(cl.time * 10) % base->anim_total;

	auto count = 0;
	while (base->anim_min > reletive || base->anim_max <= reletive)
	{
		base = base->anim_next;
		if (!base)
			Sys_Error("R_TextureAnimation: broken cycle");
		if (++count > 100)
			Sys_Error("R_TextureAnimation: infinite cycle");
	}

	return base;
}


/*
=============================================================

	BRUSH MODELS

=============================================================
*/


extern int solidskytexture;
extern int alphaskytexture;
extern float speedscale; // for top sky and bottom sky

void DrawGLWaterPoly(glpoly_t* p);
void DrawGLWaterPolyLightmap(glpoly_t* p);

lpMTexFUNC qglMTexCoord2fSGIS = nullptr;
lpSelTexFUNC qglSelectTextureSGIS = nullptr;

qboolean mtexenabled = qfalse;

void GL_SelectTexture(GLenum target);

void GL_DisableMultitexture()
{
	if (mtexenabled)
	{
		glDisable(GL_TEXTURE_2D);
		GL_SelectTexture(TEXTURE0_SGIS);
		mtexenabled = qfalse;
	}
}

void GL_EnableMultitexture()
{
	if (gl_mtexable)
	{
		GL_SelectTexture(TEXTURE1_SGIS);
		glEnable(GL_TEXTURE_2D);
		mtexenabled = qtrue;
	}
}


/*
================
R_DrawSequentialPoly

Systems that have fast state and texture changes can
just do everything as it passes with no need to sort
================
*/
void R_DrawSequentialPoly(msurface_t* s)
{
	glpoly_t* p;
	float* v;
	int i;
	texture_t* t;
	vec3_t nv;
	glRect_t* theRect;

	//
	// normal lightmaped poly
	//

	if (! (s->flags & (SURF_DRAWSKY | SURF_DRAWTURB | SURF_UNDERWATER)))
	{
		R_RenderDynamicLightmaps(s);
		if (gl_mtexable)
		{
			p = s->polys;

			t = R_TextureAnimation(s->texinfo->texture);
			// Binds world to texture env 0
			GL_SelectTexture(TEXTURE0_SGIS);
			GL_Bind(t->gl_texturenum);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			// Binds lightmap to texenv 1
			GL_EnableMultitexture(); // Same as SelectTexture (TEXTURE1)
			GL_Bind(lightmap_textures + s->lightmaptexturenum);
			i = s->lightmaptexturenum;
			if (lightmap_modified[i])
			{
				lightmap_modified[i] = qfalse;
				theRect = &lightmap_rectchange[i];
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t,
				                BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
				                lightmaps + (i * BLOCK_HEIGHT + theRect->t) * BLOCK_WIDTH * lightmap_bytes);
				theRect->l = BLOCK_WIDTH;
				theRect->t = BLOCK_HEIGHT;
				theRect->h = 0;
				theRect->w = 0;
			}
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
			glBegin(GL_POLYGON);
			v = p->verts[0];
			for (i = 0; i < p->numverts; i++ , v += VERTEXSIZE)
			{
				qglMTexCoord2fSGIS(TEXTURE0_SGIS, v[3], v[4]);
				qglMTexCoord2fSGIS(TEXTURE1_SGIS, v[5], v[6]);
				glVertex3fv(v);
			}
			glEnd();
			return;
		}
		p = s->polys;

		t = R_TextureAnimation(s->texinfo->texture);
		GL_Bind(t->gl_texturenum);
		glBegin(GL_POLYGON);
		v = p->verts[0];
		for (i = 0; i < p->numverts; i++ , v += VERTEXSIZE)
		{
			glTexCoord2f(v[3], v[4]);
			glVertex3fv(v);
		}
		glEnd();

		GL_Bind(lightmap_textures + s->lightmaptexturenum);
		glEnable(GL_BLEND);
		glBegin(GL_POLYGON);
		v = p->verts[0];
		for (i = 0; i < p->numverts; i++ , v += VERTEXSIZE)
		{
			glTexCoord2f(v[5], v[6]);
			glVertex3fv(v);
		}
		glEnd();

		glDisable(GL_BLEND);

		return;
	}

	//
	// subdivided water surface warp
	//

	if (s->flags & SURF_DRAWTURB)
	{
		GL_DisableMultitexture();
		GL_Bind(s->texinfo->texture->gl_texturenum);
		EmitWaterPolys(s);
		return;
	}

	//
	// subdivided sky warp
	//
	if (s->flags & SURF_DRAWSKY)
	{
		GL_DisableMultitexture();
		GL_Bind(solidskytexture);
		speedscale = realtime * 8;
		speedscale -= static_cast<int>(speedscale) & ~127;

		EmitSkyPolys(s);

		glEnable(GL_BLEND);
		GL_Bind(alphaskytexture);
		speedscale = realtime * 16;
		speedscale -= static_cast<int>(speedscale) & ~127;
		EmitSkyPolys(s);

		glDisable(GL_BLEND);
		return;
	}

	//
	// underwater warped with lightmap
	//
	R_RenderDynamicLightmaps(s);
	if (gl_mtexable)
	{
		p = s->polys;

		t = R_TextureAnimation(s->texinfo->texture);
		GL_SelectTexture(TEXTURE0_SGIS);
		GL_Bind(t->gl_texturenum);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		GL_EnableMultitexture();
		GL_Bind(lightmap_textures + s->lightmaptexturenum);
		i = s->lightmaptexturenum;
		if (lightmap_modified[i])
		{
			lightmap_modified[i] = qfalse;
			theRect = &lightmap_rectchange[i];
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t,
			                BLOCK_WIDTH, theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
			                lightmaps + (i * BLOCK_HEIGHT + theRect->t) * BLOCK_WIDTH * lightmap_bytes);
			theRect->l = BLOCK_WIDTH;
			theRect->t = BLOCK_HEIGHT;
			theRect->h = 0;
			theRect->w = 0;
		}
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
		glBegin(GL_TRIANGLE_FAN);
		v = p->verts[0];
		for (i = 0; i < p->numverts; i++ , v += VERTEXSIZE)
		{
			qglMTexCoord2fSGIS(TEXTURE0_SGIS, v[3], v[4]);
			qglMTexCoord2fSGIS(TEXTURE1_SGIS, v[5], v[6]);

			nv[0] = v[0] + 8 * sin(v[1] * 0.05 + realtime) * sin(v[2] * 0.05 + realtime);
			nv[1] = v[1] + 8 * sin(v[0] * 0.05 + realtime) * sin(v[2] * 0.05 + realtime);
			nv[2] = v[2];

			glVertex3fv(nv);
		}
		glEnd();
	}
	else
	{
		p = s->polys;

		t = R_TextureAnimation(s->texinfo->texture);
		GL_Bind(t->gl_texturenum);
		DrawGLWaterPoly(p);

		GL_Bind(lightmap_textures + s->lightmaptexturenum);
		glEnable(GL_BLEND);
		DrawGLWaterPolyLightmap(p);
		glDisable(GL_BLEND);
	}
}


/*
================
DrawGLWaterPoly

Warp the vertex coordinates
================
*/
void DrawGLWaterPoly(glpoly_t* p)
{
	vec3_t nv;

	GL_DisableMultitexture();

	glBegin(GL_TRIANGLE_FAN);
	auto v = p->verts[0];
	for (auto i = 0; i < p->numverts; i++ , v += VERTEXSIZE)
	{
		glTexCoord2f(v[3], v[4]);

		nv[0] = v[0] + 8 * sin(v[1] * 0.05 + realtime) * sin(v[2] * 0.05 + realtime);
		nv[1] = v[1] + 8 * sin(v[0] * 0.05 + realtime) * sin(v[2] * 0.05 + realtime);
		nv[2] = v[2];

		glVertex3fv(nv);
	}
	glEnd();
}

void DrawGLWaterPolyLightmap(glpoly_t* p)
{
	vec3_t nv;

	GL_DisableMultitexture();

	glBegin(GL_TRIANGLE_FAN);
	auto v = p->verts[0];
	for (auto i = 0; i < p->numverts; i++ , v += VERTEXSIZE)
	{
		glTexCoord2f(v[5], v[6]);

		nv[0] = v[0] + 8 * sin(v[1] * 0.05 + realtime) * sin(v[2] * 0.05 + realtime);
		nv[1] = v[1] + 8 * sin(v[0] * 0.05 + realtime) * sin(v[2] * 0.05 + realtime);
		nv[2] = v[2];

		glVertex3fv(nv);
	}
	glEnd();
}

/*
================
DrawGLPoly
================
*/
void DrawGLPoly(glpoly_t* p)
{
	glBegin(GL_POLYGON);
	auto v = p->verts[0];
	for (auto i = 0; i < p->numverts; i++ , v += VERTEXSIZE)
	{
		glTexCoord2f(v[3], v[4]);
		glVertex3fv(v);
	}
	glEnd();
}


/*
================
R_BlendLightmaps
================
*/
void R_BlendLightmaps()
{
	if (r_fullbright.value)
		return;
	if (!gl_texsort.value)
		return;

	glDepthMask(0); // don't bother writing Z

	if (gl_lightmap_format == GL_LUMINANCE)
		glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
	else if (gl_lightmap_format == GL_INTENSITY)
	{
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f(0, 0, 0, 1);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	if (!r_lightmap.value)
	{
		glEnable(GL_BLEND);
	}

	for (auto i = 0; i < MAX_LIGHTMAPS; i++)
	{
		auto p = lightmap_polys[i];
		if (!p)
			continue;
		GL_Bind(lightmap_textures + i);
		if (lightmap_modified[i])
		{
			lightmap_modified[i] = qfalse;
			auto theRect = &lightmap_rectchange[i];

			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, theRect->t, BLOCK_WIDTH,
			                theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
			                lightmaps + (i * BLOCK_HEIGHT + theRect->t) * BLOCK_WIDTH * lightmap_bytes);

			theRect->l = BLOCK_WIDTH;
			theRect->t = BLOCK_HEIGHT;
			theRect->h = 0;
			theRect->w = 0;
		}
		for (; p; p = p->chain)
		{
			if (p->flags & SURF_UNDERWATER)
				DrawGLWaterPolyLightmap(p);
			else
			{
				glBegin(GL_POLYGON);
				auto v = p->verts[0];
				for (auto j = 0; j < p->numverts; j++ , v += VERTEXSIZE)
				{
					glTexCoord2f(v[5], v[6]);
					glVertex3fv(v);
				}
				glEnd();
			}
		}
	}

	glDisable(GL_BLEND);
	if (gl_lightmap_format == GL_LUMINANCE)
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	else if (gl_lightmap_format == GL_INTENSITY)
	{
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glColor4f(1, 1, 1, 1);
	}

	glDepthMask(1); // back to normal Z buffering
}

/*
================
R_RenderBrushPoly
================
*/
void R_RenderBrushPoly(msurface_t* fa)
{
	c_brush_polys++;

	if (fa->flags & SURF_DRAWSKY)
	{ // warp texture, no lightmaps
		EmitBothSkyLayers(fa);
		return;
	}

	auto t = R_TextureAnimation(fa->texinfo->texture);
	GL_Bind(t->gl_texturenum);

	if (fa->flags & SURF_DRAWTURB)
	{ // warp texture, no lightmaps
		EmitWaterPolys(fa);
		return;
	}

	if (fa->flags & SURF_UNDERWATER)
		DrawGLWaterPoly(fa->polys);
	else
		DrawGLPoly(fa->polys);

	// add the poly to the proper lightmap chain

	fa->polys->chain = lightmap_polys[fa->lightmaptexturenum];
	lightmap_polys[fa->lightmaptexturenum] = fa->polys;

	// check for lightmap modification
	for (auto maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255;
	     maps++)
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;

	if (fa->dlightframe == r_framecount // dynamic this frame
		|| fa->cached_dlight) // dynamic previously
	{
	dynamic:
		if (r_dynamic.value)
		{
			lightmap_modified[fa->lightmaptexturenum] = qtrue;
			auto theRect = &lightmap_rectchange[fa->lightmaptexturenum];
			if (fa->light_t < theRect->t)
			{
				if (theRect->h)
					theRect->h += theRect->t - fa->light_t;
				theRect->t = fa->light_t;
			}
			if (fa->light_s < theRect->l)
			{
				if (theRect->w)
					theRect->w += theRect->l - fa->light_s;
				theRect->l = fa->light_s;
			}
			auto smax = (fa->extents[0] >> 4) + 1;
			auto tmax = (fa->extents[1] >> 4) + 1;
			if (theRect->w + theRect->l < fa->light_s + smax)
				theRect->w = fa->light_s - theRect->l + smax;
			if (theRect->h + theRect->t < fa->light_t + tmax)
				theRect->h = fa->light_t - theRect->t + tmax;
			auto base = lightmaps + fa->lightmaptexturenum * lightmap_bytes * BLOCK_WIDTH * BLOCK_HEIGHT;
			base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
			R_BuildLightMap(fa, base, BLOCK_WIDTH * lightmap_bytes);
		}
	}
}

/*
================
R_RenderDynamicLightmaps
Multitexture
================
*/
void R_RenderDynamicLightmaps(msurface_t* fa)
{
	c_brush_polys++;

	if (fa->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
		return;

	fa->polys->chain = lightmap_polys[fa->lightmaptexturenum];
	lightmap_polys[fa->lightmaptexturenum] = fa->polys;

	// check for lightmap modification
	for (auto maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255;
	     maps++)
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;

	if (fa->dlightframe == r_framecount // dynamic this frame
		|| fa->cached_dlight) // dynamic previously
	{
	dynamic:
		if (r_dynamic.value)
		{
			lightmap_modified[fa->lightmaptexturenum] = qtrue;
			auto theRect = &lightmap_rectchange[fa->lightmaptexturenum];
			if (fa->light_t < theRect->t)
			{
				if (theRect->h)
					theRect->h += theRect->t - fa->light_t;
				theRect->t = fa->light_t;
			}
			if (fa->light_s < theRect->l)
			{
				if (theRect->w)
					theRect->w += theRect->l - fa->light_s;
				theRect->l = fa->light_s;
			}
			auto smax = (fa->extents[0] >> 4) + 1;
			auto tmax = (fa->extents[1] >> 4) + 1;
			if (theRect->w + theRect->l < fa->light_s + smax)
				theRect->w = fa->light_s - theRect->l + smax;
			if (theRect->h + theRect->t < fa->light_t + tmax)
				theRect->h = fa->light_t - theRect->t + tmax;
			auto base = lightmaps + fa->lightmaptexturenum * lightmap_bytes * BLOCK_WIDTH * BLOCK_HEIGHT;
			base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
			R_BuildLightMap(fa, base, BLOCK_WIDTH * lightmap_bytes);
		}
	}
}

/*
================
R_MirrorChain
================
*/
void R_MirrorChain(msurface_t* s)
{
	if (mirror)
		return;
	mirror = qtrue;
	mirror_plane = s->plane;
}

/*
================
R_DrawWaterSurfaces
================
*/
void R_DrawWaterSurfaces()
{
	msurface_t* s;

	if (r_wateralpha.value == 1.0 && gl_texsort.value)
		return;

	//
	// go back to the world matrix
	//

	glLoadMatrixf(r_world_matrix);

	if (r_wateralpha.value < 1.0)
	{
		glEnable(GL_BLEND);
		glColor4f(1, 1, 1, r_wateralpha.value);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	}

	if (!gl_texsort.value)
	{
		if (!waterchain)
			return;

		for (s = waterchain; s; s = s->texturechain)
		{
			GL_Bind(s->texinfo->texture->gl_texturenum);
			EmitWaterPolys(s);
		}

		waterchain = nullptr;
	}
	else
	{
		for (auto i = 0; i < cl.worldmodel->numtextures; i++)
		{
			auto t = cl.worldmodel->textures[i];
			if (!t)
				continue;
			s = t->texturechain;
			if (!s)
				continue;
			if (!(s->flags & SURF_DRAWTURB))
				continue;

			// set modulate mode explicitly

			GL_Bind(t->gl_texturenum);

			for (; s; s = s->texturechain)
				EmitWaterPolys(s);

			t->texturechain = nullptr;
		}
	}

	if (r_wateralpha.value < 1.0)
	{
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

		glColor4f(1, 1, 1, 1);
		glDisable(GL_BLEND);
	}
}

/*
================
DrawTextureChains
================
*/
void DrawTextureChains()
{
	if (!gl_texsort.value)
	{
		GL_DisableMultitexture();

		if (skychain)
		{
			R_DrawSkyChain(skychain);
			skychain = nullptr;
		}

		return;
	}

	for (auto i = 0; i < cl.worldmodel->numtextures; i++)
	{
		auto t = cl.worldmodel->textures[i];
		if (!t)
			continue;
		auto s = t->texturechain;
		if (!s)
			continue;
		if (i == skytexturenum)
			R_DrawSkyChain(s);
		else if (i == mirrortexturenum && r_mirroralpha.value != 1.0)
		{
			R_MirrorChain(s);
			continue;
		}
		else
		{
			if (s->flags & SURF_DRAWTURB && r_wateralpha.value != 1.0)
				continue; // draw translucent water later
			for (; s; s = s->texturechain)
				R_RenderBrushPoly(s);
		}

		t->texturechain = nullptr;
	}
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel(entity_t* e)
{
	vec3_t mins;
	vec3_t maxs;
	int i;
	mplane_t* pplane;
	model_t* clmodel;
	qboolean rotated;

	currententity = e;
	currenttexture = -1;

	clmodel = e->model;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = qtrue;
		for (i = 0; i < 3; i++)
		{
			mins[i] = e->origin[i] - clmodel->radius;
			maxs[i] = e->origin[i] + clmodel->radius;
		}
	}
	else
	{
		rotated = qfalse;
		VectorAdd (e->origin, clmodel->mins, mins);
		VectorAdd (e->origin, clmodel->maxs, maxs);
	}

	if (R_CullBox(mins, maxs))
		return;

	glColor3f(1, 1, 1);
	memset(lightmap_polys, 0, sizeof lightmap_polys);

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (rotated)
	{
		vec3_t temp;
		vec3_t forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors(e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	auto psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	// calculate dynamic lighting for bmodel if it's not an
	// instanced model
	if (clmodel->firstmodelsurface != 0 && !gl_flashblend.value)
	{
		for (auto k = 0; k < MAX_DLIGHTS; k++)
		{
			if (cl_dlights[k].die < cl.time ||
				!cl_dlights[k].radius)
				continue;

			R_MarkLights(&cl_dlights[k], 1 << k,
			             clmodel->nodes + clmodel->hulls[0].firstclipnode);
		}
	}

	glPushMatrix();
	e->angles[0] = -e->angles[0]; // stupid quake bug
	R_RotateForEntity(e);
	e->angles[0] = -e->angles[0]; // stupid quake bug

	//
	// draw texture
	//
	for (i = 0; i < clmodel->nummodelsurfaces; i++ , psurf++)
	{
		// find which side of the node we are on
		pplane = psurf->plane;

		auto dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

		// draw the polygon
		if (psurf->flags & SURF_PLANEBACK && dot < -BACKFACE_EPSILON ||
			!(psurf->flags & SURF_PLANEBACK) && dot > BACKFACE_EPSILON)
		{
			if (gl_texsort.value)
				R_RenderBrushPoly(psurf);
			else
				R_DrawSequentialPoly(psurf);
		}
	}

	R_BlendLightmaps();

	glPopMatrix();
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode(mnode_t* node)
{
	int c;
	int side;
	mplane_t* plane;
	double dot;

	if (node->contents == CONTENTS_SOLID)
		return; // solid

	if (node->visframe != r_visframecount)
		return;
	if (R_CullBox(node->minmaxs, node->minmaxs + 3))
		return;

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		auto pleaf = reinterpret_cast<mleaf_t *>(node);

		auto mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			}
			while (--c);
		}

		// deal with model fragments in this leaf
		if (pleaf->efrags)
			R_StoreEfrags(&pleaf->efrags);

		return;
	}

	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
	plane = node->plane;

	switch (plane->type)
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (dot >= 0)
		side = 0;
	else
		side = 1;

	// recurse down the children, front side first
	R_RecursiveWorldNode(node->children[side]);

	// draw stuff
	c = node->numsurfaces;

	if (c)
	{
		auto surf = cl.worldmodel->surfaces + node->firstsurface;

		if (dot < 0 - BACKFACE_EPSILON)
			side = SURF_PLANEBACK;
		else if (dot > BACKFACE_EPSILON)
			side = 0;
		{
			for (; c; c-- , surf++)
			{
				if (surf->visframe != r_framecount)
					continue;

				// don't backface underwater surfaces, because they warp
				if (!(surf->flags & SURF_UNDERWATER) && dot < 0 ^ !!(surf->flags & SURF_PLANEBACK))
					continue; // wrong side

				// if sorting by texture, just store it out
				if (gl_texsort.value)
				{
					if (!mirror
						|| surf->texinfo->texture != cl.worldmodel->textures[mirrortexturenum])
					{
						surf->texturechain = surf->texinfo->texture->texturechain;
						surf->texinfo->texture->texturechain = surf;
					}
				}
				else if (surf->flags & SURF_DRAWSKY)
				{
					surf->texturechain = skychain;
					skychain = surf;
				}
				else if (surf->flags & SURF_DRAWTURB)
				{
					surf->texturechain = waterchain;
					waterchain = surf;
				}
				else
					R_DrawSequentialPoly(surf);
			}
		}
	}

	// recurse down the back side
	R_RecursiveWorldNode(node->children[!side]);
}


/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld()
{
	entity_t ent;

	memset(&ent, 0, sizeof ent);
	ent.model = cl.worldmodel;

	VectorCopy (r_refdef.vieworg, modelorg);

	currententity = &ent;
	currenttexture = -1;

	glColor3f(1, 1, 1);
	memset(lightmap_polys, 0, sizeof lightmap_polys);

	R_RecursiveWorldNode(cl.worldmodel->nodes);

	DrawTextureChains();

	R_BlendLightmaps();
}


/*
===============
R_MarkLeaves
===============
*/
void R_MarkLeaves()
{
	byte* vis;
	byte solid[4096];

	if (r_oldviewleaf == r_viewleaf && !r_novis.value)
		return;

	if (mirror)
		return;

	r_visframecount++;
	r_oldviewleaf = r_viewleaf;

	if (r_novis.value)
	{
		vis = solid;
		memset(solid, 0xff, cl.worldmodel->numleafs + 7 >> 3);
	}
	else
		vis = Mod_LeafPVS(r_viewleaf, cl.worldmodel);

	for (auto i = 0; i < cl.worldmodel->numleafs; i++)
	{
		if (vis[i >> 3] & 1 << (i & 7))
		{
			auto node = reinterpret_cast<mnode_t *>(&cl.worldmodel->leafs[i + 1]);
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			}
			while (node);
		}
	}
}


/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/

// returns a texture number and the position inside it
int AllocBlock(int w, int h, int* x, int* y)
{
	int i;
	int j;

	for (auto texnum = 0; texnum < MAX_LIGHTMAPS; texnum++)
	{
		auto best = BLOCK_HEIGHT;

		for (i = 0; i < BLOCK_WIDTH - w; i++)
		{
			auto best2 = 0;

			for (j = 0; j < w; j++)
			{
				if (allocated[texnum][i + j] >= best)
					break;
				if (allocated[texnum][i + j] > best2)
					best2 = allocated[texnum][i + j];
			}
			if (j == w)
			{ // this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i = 0; i < w; i++)
			allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	Sys_Error("AllocBlock: full");
	return 0;
}


mvertex_t* r_pcurrentvertbase;
model_t* currentmodel;

int nColinElim;

/*
================
BuildSurfaceDisplayList
================
*/
void BuildSurfaceDisplayList(msurface_t* fa)
{
	int i;
	medge_t* r_pedge;
	float* vec;

	auto pedges = currentmodel->edges;
	auto lnumverts = fa->numedges;
	auto poly = static_cast<glpoly_t *>(Hunk_Alloc(sizeof(glpoly_t) + (lnumverts - 4) * VERTEXSIZE * sizeof(float)));

	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (i = 0; i < lnumverts; i++)
	{
		auto lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = r_pcurrentvertbase[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = r_pcurrentvertbase[r_pedge->v[1]].position;
		}

		auto s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->texture->width;

		auto t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->texture->height;

		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		//
		// lightmap texture coordinates
		//
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s * 16;
		s += 8;
		s /= BLOCK_WIDTH * 16; //fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t * 16;
		t += 8;
		t /= BLOCK_HEIGHT * 16; //fa->texinfo->texture->height;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}

	//
	// remove co-linear points - Ed
	//
	if (!gl_keeptjunctions.value && !(fa->flags & SURF_UNDERWATER))
	{
		for (i = 0; i < lnumverts; ++i)
		{
			vec3_t v1;
			vec3_t v2;
			float *prev, *self, *next;

			prev = poly->verts[(i + lnumverts - 1) % lnumverts];
			self = poly->verts[i];
			next = poly->verts[(i + 1) % lnumverts];

			VectorSubtract(self, prev, v1 );
			VectorNormalize(v1);
			VectorSubtract( next, prev, v2 );
			VectorNormalize(v2);

			// skip co-linear points
#define COLINEAR_EPSILON 0.001
			if (fabs(v1[0] - v2[0]) <= COLINEAR_EPSILON &&
				fabs(v1[1] - v2[1]) <= COLINEAR_EPSILON &&
				fabs(v1[2] - v2[2]) <= COLINEAR_EPSILON)
			{
				for (auto j = i + 1; j < lnumverts; ++j)
				{
					for (auto k = 0; k < VERTEXSIZE; ++k)
					{
						poly->verts[j - 1][k] = poly->verts[j][k];
					}
				}
				--lnumverts;
				++nColinElim;
				// retry next vertex next time, which is now current vertex
				--i;
			}
		}
	}
	poly->numverts = lnumverts;
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
void GL_CreateSurfaceLightmap(msurface_t* surf)
{
	if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
		return;

	auto smax = (surf->extents[0] >> 4) + 1;
	auto tmax = (surf->extents[1] >> 4) + 1;

	surf->lightmaptexturenum = AllocBlock(smax, tmax, &surf->light_s, &surf->light_t);
	auto base = lightmaps + surf->lightmaptexturenum * lightmap_bytes * BLOCK_WIDTH * BLOCK_HEIGHT;
	base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * lightmap_bytes;
	R_BuildLightMap(surf, base, BLOCK_WIDTH * lightmap_bytes);
}


/*
==================
GL_BuildLightmaps

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void GL_BuildLightmaps()
{
	int i;
	extern qboolean isPermedia;

	memset(allocated, 0, sizeof allocated);

	r_framecount = 1; // no dlightcache

	if (!lightmap_textures)
	{
		lightmap_textures = texture_extension_number;
		texture_extension_number += MAX_LIGHTMAPS;
	}

	gl_lightmap_format = GL_LUMINANCE;
	// default differently on the Permedia
	if (isPermedia)
		gl_lightmap_format = GL_RGBA;

	if (COM_CheckParm("-lm_1"))
		gl_lightmap_format = GL_LUMINANCE;
	if (COM_CheckParm("-lm_a"))
		gl_lightmap_format = GL_ALPHA;
	if (COM_CheckParm("-lm_i"))
		gl_lightmap_format = GL_INTENSITY;
	if (COM_CheckParm("-lm_2"))
		gl_lightmap_format = GL_RGBA4;
	if (COM_CheckParm("-lm_4"))
		gl_lightmap_format = GL_RGBA;

	switch (gl_lightmap_format)
	{
	case GL_RGBA:
		lightmap_bytes = 4;
		break;
	case GL_RGBA4:
		lightmap_bytes = 2;
		break;
	case GL_LUMINANCE:
	case GL_INTENSITY:
	case GL_ALPHA:
		lightmap_bytes = 1;
		break;
	default:
		break;
	}

	for (auto j = 1; j < MAX_MODELS; j++)
	{
		auto m = cl.model_precache[j];
		if (!m)
			break;
		if (m->name[0] == '*')
			continue;
		r_pcurrentvertbase = m->vertexes;
		currentmodel = m;
		for (i = 0; i < m->numsurfaces; i++)
		{
			GL_CreateSurfaceLightmap(m->surfaces + i);
			if (m->surfaces[i].flags & SURF_DRAWTURB)
				continue;
			BuildSurfaceDisplayList(m->surfaces + i);
		}
	}

	if (!gl_texsort.value)
		GL_SelectTexture(TEXTURE1_SGIS);

	//
	// upload all lightmaps that were filled
	//
	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (!allocated[i][0])
			break; // no more used
		lightmap_modified[i] = qfalse;
		lightmap_rectchange[i].l = BLOCK_WIDTH;
		lightmap_rectchange[i].t = BLOCK_HEIGHT;
		lightmap_rectchange[i].w = 0;
		lightmap_rectchange[i].h = 0;
		GL_Bind(lightmap_textures + i);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, lightmap_bytes
		             , BLOCK_WIDTH, BLOCK_HEIGHT, 0,
		             gl_lightmap_format, GL_UNSIGNED_BYTE, lightmaps + i * BLOCK_WIDTH * BLOCK_HEIGHT * lightmap_bytes);
	}

	if (!gl_texsort.value)
		GL_SelectTexture(TEXTURE0_SGIS);
}
