#pragma once

struct mplane_t;
using vec_t = float;
using vec3_t = vec_t[3];
using vec5_t = vec_t [5];

using fixed4_t = int;
using fixed8_t = int;
using fixed16_t = int;

#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

struct mplane_s;

extern vec3_t vec3_origin;
extern int    nanmask;

#define	IS_NAN(x) (((*(int *)&x)&nanmask)==nanmask)

#define DotProduct(x,y) (x[0]*y[0]+x[1]*y[1]+x[2]*y[2])
#define VectorSubtract(a,b,c) {c[0]=a[0]-b[0];c[1]=a[1]-b[1];c[2]=a[2]-b[2];}
#define VectorAdd(a,b,c) {c[0]=a[0]+b[0];c[1]=a[1]+b[1];c[2]=a[2]+b[2];}
#define VectorCopy(a,b) {b[0]=a[0];b[1]=a[1];b[2]=a[2];}

void VectorMA(vec3_t veca, float scale, vec3_t vecb, vec3_t vecc);

vec_t _DotProduct(vec3_t     v1, vec3_t   v2);
void  _VectorSubtract(vec3_t veca, vec3_t vecb, vec3_t out);
void  _VectorAdd(vec3_t      veca, vec3_t vecb, vec3_t out);
void  _VectorCopy(vec3_t     in, vec3_t   out);

int   VectorCompare(vec3_t   v1, vec3_t v2);
vec_t Length(vec3_t          v);
void  CrossProduct(vec3_t    v1, vec3_t v2, vec3_t cross);
float VectorNormalize(vec3_t v); // returns vector length
void  VectorInverse(vec3_t   v);
void  VectorScale(vec3_t     in, vec_t scale, vec3_t out);
int   Q_log2(int             val);

void R_ConcatRotations(float  in1[3][3], float in2[3][3], float out[3][3]);
void R_ConcatTransforms(float in1[3][4], float in2[3][4], float out[3][4]);

void FloorDivMod(double             numer, double denom, int* quotient,
                 int*               rem);
fixed16_t Invert24To16(fixed16_t    val);
int       GreatestCommonDivisor(int i1, int i2);

void  AngleVectors(vec3_t            angles, vec3_t forward, vec3_t  right, vec3_t up);
int   BoxOnPlaneSide(vec3_t          emins, vec3_t  emaxs, mplane_t* plane);
float anglemod(float                 a);
void  RotatePointAroundVector(vec3_t dst, const vec3_t dir, const vec3_t point, float degrees);

#define BOX_ON_PLANE_SIDE(emins, emaxs, p)	\
	(((p)->type < 3)?						\
	(										\
		((p)->dist <= (emins)[(p)->type])?	\
			1								\
		:									\
		(									\
			((p)->dist >= (emaxs)[(p)->type])?\
				2							\
			:								\
				3							\
		)									\
	)										\
	:										\
		BoxOnPlaneSide( (emins), (emaxs), (p)))
