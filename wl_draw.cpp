// WL_DRAW.C
#define USE_SPRITES 1
#include "wl_def.h"
#pragma hdrstop

#include "wl_cloudsky.h"
#include "wl_atmos.h"
#include "wl_shade.h"

typedef void PARA_RTN(void *parm);

void  SPR_RunSlaveSH(PARA_RTN *routine, void *parm);
void  SPR_WaitEndSlaveSH(void);

void heapWalk();
unsigned char wall_buffer[SATURN_WIDTH*64];
//unsigned char spr_buffer[30*64*64];
typedef struct
{
	byte *postsource;
	int postx;
	int texdelta;
	word tilehit;
	short xtile,ytile;
	int xintercept,yintercept;
	short xtilestep,ytilestep;
// wall optimization variables	
	int lastside;
	int32_t lastintercept;
	int lasttilehit;
	int lasttexture;
} 
ray_struc;

/*
=============================================================================

                               LOCAL CONSTANTS

=============================================================================
*/

// the door is the last picture before the sprites
#define DOORWALL        (PMSpriteStart-8)

#define ACTORSIZE       0x4000

/*
=============================================================================

                              GLOBAL VARIABLES

=============================================================================
*/

static byte *vbuf = NULL;
//unsigned vbufPitch = 0;

int32_t    lasttimecount;
//int32_t    frameon;
boolean fpscounter=1;
//boolean fpscounter=0;

int fps_frames=0, fps_time=0, fps=0;

int *wallheight;
int min_wallheight;

//
// math tables
//
short *pixelangle;
int32_t finetangent[FINEANGLES/4];
fixed sintable[ANGLES+ANGLES/4];
fixed *costable = sintable+(ANGLES/4);

//
// refresh variables
//
fixed   viewx,viewy;                    // the focal point
//short   viewangle;
fixed   viewsin,viewcos;

void    TransformActor (objtype *ob);
void    BuildTables (void);
void    ClearScreen (void);
int     CalcRotate (objtype *ob);
void    DrawScaleds (void);
void    CalcTics (void);
void    ThreeDRefresh (void);

//
// ray tracing variables
//
short   focaltx,focalty,viewtx,viewty;
short   midangle;

word horizwall[MAXWALLTILES],vertwall[MAXWALLTILES];

/*
============================================================================

                           3 - D  DEFINITIONS

============================================================================
*/

/*
========================
=
= TransformActor
=
= Takes paramaters:
=   gx,gy               : globalx/globaly of point
=
= globals:
=   viewx,viewy         : point of view
=   viewcos,viewsin     : sin/cos of viewangle
=   scale               : conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
========================
*/


//
// transform actor
//
void TransformActor (objtype *ob)
{
    fixed gx,gy,gxt,gyt,nx,ny;

//
// translate point to view centered coordinates
//
    gx = ob->x-viewx;
    gy = ob->y-viewy;

//
// calculate newx
//
    gxt = FixedMul(gx,viewcos);
    gyt = FixedMul(gy,viewsin);
    nx = gxt-gyt-ACTORSIZE;         // fudge the shape forward a bit, because
                                    // the midpoint could put parts of the shape
                                    // into an adjacent wall

//
// calculate newy
//
    gxt = FixedMul(gx,viewsin);
    gyt = FixedMul(gy,viewcos);
    ny = gyt+gxt;

//
// calculate perspective ratio
//
    ob->transx = nx;
    ob->transy = ny;

    if (nx<MINDIST)                 // too close, don't overflow the divide
    {
        ob->viewheight = 0;
        return;
    }

    ob->viewx = (word)(centerx + ny*scale/nx);

//
// calculate height (heightnumerator/(nx>>8))
//
    ob->viewheight = (word)(heightnumerator/(nx>>8));
}

//==========================================================================

/*
========================
=
= TransformTile
=
= Takes paramaters:
=   tx,ty               : tile the object is centered in
=
= globals:
=   viewx,viewy         : point of view
=   viewcos,viewsin     : sin/cos of viewangle
=   scale               : conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
= Returns true if the tile is withing getting distance
=
========================
*/

boolean TransformTile (int tx, int ty, short shapenum, short *dispx, short *dispheight)
{
    fixed gx,gy,gxt,gyt,nx,ny;

//
// translate point to view centered coordinates
//
    gx = ((int32_t)tx<<TILESHIFT)+0x8000-viewx;
    gy = ((int32_t)ty<<TILESHIFT)+0x8000-viewy;

//
// calculate newx
//
    gxt = FixedMul(gx,viewcos);
    gyt = FixedMul(gy,viewsin);
    nx = gxt-gyt-0x2000;            // 0x2000 is size of object

//
// calculate newy
//
    gxt = FixedMul(gx,viewsin);
    gyt = FixedMul(gy,viewcos);
    ny = gyt+gxt;

//
// calculate height / perspective ratio
//
    if (nx<MINDIST)                 // too close, don't overflow the divide
        *dispheight = 0;
    else
    {
        *dispx = (short)(centerx + ny*scale/nx);
        *dispheight = (short)(heightnumerator/(nx>>8));
    }

//
// see if it should be grabbed
//
    if (nx<TILEGLOBAL && ny>-TILEGLOBAL/2 && ny<TILEGLOBAL/2)
        return true;
    else
        return false;
}
/*
====================
=
= CalcHeight
=
= Calculates the height of xintercept,yintercept from viewx,viewy
=
====================
*/

int CalcHeight(int xintercept, int yintercept)
{
    fixed z = FixedMul(xintercept - viewx, viewcos)
        - FixedMul(yintercept - viewy, viewsin);
    if(z < MINDIST) z = MINDIST;
    int height = heightnumerator / (z >> 8);
    if(height < min_wallheight) min_wallheight = height;
    return height;
}

//==========================================================================

/*
===================
=
= ScalePost
=
===================
*/

//int postx;
//byte *postsource;
//int postwidth;
void loadActorTexture(unsigned char texture);

void ScalePost(int postx,byte *postsource)
{
#if USE_SPRITES	
//--------------------------------------------------------------------------------------------
	memcpyl((void *)(wall_buffer + (postx<<6)),(void *)postsource,64);
//  a           b          c             d
// top left, top right, bottom right, bottom left
    SPRITE user_wall;
    user_wall.CTRL=FUNC_Texture | _ZmCC;
    user_wall.PMOD=CL256Bnk | ECdis | SPdis | 0x0800; // sans transparence

    user_wall.SRCA=(void *)(0x2000|(postx*8));
    user_wall.COLR=256;
    user_wall.SIZE=0x801;

	user_wall.XD=postx-(viewwidth/2);
	user_wall.YD=-(wallheight[postx] / 8);
	user_wall.XC=user_wall.XD;
	user_wall.YC=(wallheight[postx] / 8);
	user_wall.XA=user_wall.XD;
	user_wall.YA=user_wall.YD;
	user_wall.XB=user_wall.XA;
	user_wall.YB=user_wall.YC;
	
    user_wall.GRDA=0;
	// 240 pour du 320, 264 pour du 352
	slSetSprite(&user_wall, toFIXED(0+(SATURN_SORT_VALUE-wallheight[postx]/8)));	
//--------------------------------------------------------------------------------------------
#else
	
    int ywcount, yoffs, yw, yd, yendoffs;
    byte col;

#ifdef USE_SHADING
    byte *curshades = shadetable[GetShade(wallheight[postx])];
#endif

    ywcount = yd = wallheight[postx] / 8;
    if(yd <= 0) yd = 100;

    yoffs = (viewheight / 2 - ywcount) * vbufPitch;
    if(yoffs < 0) yoffs = 0;
    yoffs += postx;

    yendoffs = viewheight / 2 + ywcount - 1;
    yw=TEXTURESIZE-1;

    while(yendoffs >= viewheight)
    {
        ywcount -= TEXTURESIZE/2;
        while(ywcount <= 0)
        {
            ywcount += yd;
            yw--;
        }
        yendoffs--;
    }
    if(yw < 0) return;	

#ifdef USE_SHADING
    col = curshades[postsource[yw]];
#else
    col = postsource[yw];
#endif	

    yendoffs = yendoffs * vbufPitch + postx;
    while(yoffs <= yendoffs)
    {
        vbuf[yendoffs] = col;
        ywcount -= TEXTURESIZE/2;
        if(ywcount <= 0)
        {
            do
            {
                ywcount += yd;
                yw--;
            }
            while(ywcount <= 0);
            if(yw < 0) break;
#ifdef USE_SHADING
            col = curshades[postsource[yw]];
#else
            col = postsource[yw];
#endif
        }
        yendoffs -= vbufPitch;
    }
#endif	
}

/*
void GlobalScalePost(byte *vidbuf, unsigned pitch)
{
    vbuf = vidbuf;
    vbufPitch = pitch;
    ScalePost(postx,postsource);
}
*/
/*
====================
=
= HitVertWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitVertWall (int pixx, ray_struc *ray)
{
    int wallpic;
    int texture;

    texture = ((ray->yintercept+ray->texdelta)>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;
    if (ray->xtilestep == -1)
    {
        texture = TEXTUREMASK-texture;
        ray->xintercept += TILEGLOBAL;
    }

    if(ray->lastside==1 && ray->lastintercept==ray->xtile && ray->lasttilehit==ray->tilehit && !(ray->lasttilehit & 0x40))
    {
        if((pixx&3) && texture == ray->lasttexture)
        {
            ScalePost(ray->postx,ray->postsource);
            ray->postx = pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost(ray->postx,ray->postsource);
        wallheight[pixx] = CalcHeight(ray->xintercept,ray->yintercept);
        ray->postsource+=texture-ray->lasttexture;
//        postwidth=1;
        ray->postx=pixx;
        ray->lasttexture=texture;
        return;
    }

    if(ray->lastside!=-1) ScalePost(ray->postx,ray->postsource);

    ray->lastside=1;
    ray->lastintercept=ray->xtile;
    ray->lasttilehit=ray->tilehit;
    ray->lasttexture=texture;
    wallheight[pixx] = CalcHeight(ray->xintercept,ray->yintercept);
    ray->postx = pixx;
//    postwidth = 1;

    if (ray->tilehit & 0x40)
    {                                                               // check for adjacent doors
        ray->ytile = (short)(ray->yintercept>>TILESHIFT);
        if ( tilemap[ray->xtile-ray->xtilestep][ray->ytile]&0x80 )
            wallpic = DOORWALL+3;
        else
            wallpic = vertwall[ray->tilehit & ~0x40];
    }
    else
        wallpic = vertwall[ray->tilehit];

    ray->postsource = PM_GetTexture(wallpic) + texture;
}


/*
====================
=
= HitHorizWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitHorizWall (int pixx, ray_struc *ray)
{
    int wallpic;
    int texture;

    texture = ((ray->xintercept+ray->texdelta)>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;
    if (ray->ytilestep == -1)
        ray->yintercept += TILEGLOBAL;
    else
        texture = TEXTUREMASK-texture;

    if(ray->lastside==0 && ray->lastintercept==ray->ytile && ray->lasttilehit==ray->tilehit && !(ray->lasttilehit & 0x40))
    {
        if((pixx&3) && texture == ray->lasttexture)
        {
            ScalePost(ray->postx,ray->postsource);
            ray->postx=pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost(ray->postx,ray->postsource);
        wallheight[pixx] = CalcHeight(ray->xintercept,ray->yintercept);
        ray->postsource+=texture-ray->lasttexture;
//        postwidth=1;
        ray->postx=pixx;
        ray->lasttexture=texture;
        return;
    }

    if(ray->lastside!=-1) ScalePost(ray->postx,ray->postsource);

    ray->lastside=0;
    ray->lastintercept=ray->ytile;
    ray->lasttilehit=ray->tilehit;
    ray->lasttexture=texture;
    wallheight[pixx] = CalcHeight(ray->xintercept,ray->yintercept);
    ray->postx = pixx;
//    postwidth = 1;

    if (ray->tilehit & 0x40)
    {                                                               // check for adjacent doors
        ray->xtile = (short)(ray->xintercept>>TILESHIFT);
        if ( tilemap[ray->xtile][ray->ytile-ray->ytilestep]&0x80)
            wallpic = DOORWALL+2;
        else
            wallpic = horizwall[ray->tilehit & ~0x40];
    }
    else
        wallpic = horizwall[ray->tilehit];
    ray->postsource = PM_GetTexture(wallpic) + texture;
}

//==========================================================================

/*
====================
=
= HitHorizDoor
=
====================
*/

void HitHorizDoor2 (int pixx,ray_struc *ray)
{
    int doorpage;
    int doornum;
    int texture;

    doornum = ray->tilehit&0x7f;
    texture = ((ray->xintercept-doorposition[doornum])>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;

    if(ray->lasttilehit==ray->tilehit)
    {
        if((pixx&3) && texture == ray->lasttexture)
        {
            ScalePost(ray->postx,ray->postsource);
            ray->postx=pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost(ray->postx,ray->postsource);
        wallheight[pixx] = CalcHeight(ray->xintercept,ray->yintercept);
        ray->postsource+=texture-ray->lasttexture;
//        postwidth=1;
        ray->postx=pixx;
        ray->lasttexture=texture;
        return;
    }

    if(ray->lastside!=-1) ScalePost(ray->postx,ray->postsource);

    ray->lastside=2;
    ray->lasttilehit=ray->tilehit;
    ray->lasttexture=texture;
    wallheight[pixx] = CalcHeight(ray->xintercept,ray->yintercept);
    ray->postx = pixx;
//    postwidth = 1;

    switch(doorobjlist[doornum].lock)
    {
        case dr_normal:
            doorpage = DOORWALL;
            break;
        case dr_lock1:
        case dr_lock2:
        case dr_lock3:
        case dr_lock4:
            doorpage = DOORWALL+6;
            break;
        case dr_elevator:
            doorpage = DOORWALL+4;
            break;
    }
    ray->postsource = PM_GetTexture(doorpage) + texture;
}

//==========================================================================

/*
====================
=
= HitVertDoor
=
====================
*/

void HitVertDoor2 (int pixx,ray_struc *ray)
{
    int doorpage;
    int doornum;
    int texture;

    doornum = ray->tilehit&0x7f;
    texture = ((ray->yintercept-doorposition[doornum])>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;

    if(ray->lasttilehit==ray->tilehit)
    {
        if((pixx&3) && texture == ray->lasttexture)
        {
            ScalePost(ray->postx,ray->postsource);
            ray->postx=pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost(ray->postx,ray->postsource);
        wallheight[pixx] = CalcHeight(ray->xintercept,ray->yintercept);
        ray->postsource+=texture-ray->lasttexture;
//        postwidth=1;
        ray->postx=pixx;
        ray->lasttexture=texture;
        return;
    }

    if(ray->lastside!=-1) ScalePost(ray->postx,ray->postsource);

    ray->lastside=2;
    ray->lasttilehit=ray->tilehit;
    ray->lasttexture=texture;
    wallheight[pixx] = CalcHeight(ray->xintercept,ray->yintercept);
    ray->postx = pixx;
//    postwidth = 1;

    switch(doorobjlist[doornum].lock)
    {
        case dr_normal:
            doorpage = DOORWALL+1;
            break;
        case dr_lock1:
        case dr_lock2:
        case dr_lock3:
        case dr_lock4:
            doorpage = DOORWALL+7;
            break;
        case dr_elevator:
            doorpage = DOORWALL+5;
            break;
    }
    ray->postsource = PM_GetTexture(doorpage) + texture;
}

//==========================================================================

#define HitHorizBorder HitHorizWall
#define HitVertBorder HitVertWall

//==========================================================================

byte vgaCeiling[]=
{
#ifndef SPEAR
 0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0xbf,
 0x4e,0x4e,0x4e,0x1d,0x8d,0x4e,0x1d,0x2d,0x1d,0x8d,
 0x1d,0x1d,0x1d,0x1d,0x1d,0x2d,0xdd,0x1d,0x1d,0x98,

 0x1d,0x9d,0x2d,0xdd,0xdd,0x9d,0x2d,0x4d,0x1d,0xdd,
 0x7d,0x1d,0x2d,0x2d,0xdd,0xd7,0x1d,0x1d,0x1d,0x2d,
 0x1d,0x1d,0x1d,0x1d,0xdd,0xdd,0x7d,0xdd,0xdd,0xdd
#else
 0x6f,0x4f,0x1d,0xde,0xdf,0x2e,0x7f,0x9e,0xae,0x7f,
 0x1d,0xde,0xdf,0xde,0xdf,0xde,0xe1,0xdc,0x2e,0x1d,0xdc
#endif
};

/*
=====================
=
= VGAClearScreen
=
=====================
*/

void VGAClearScreen (void) // vbt : fond d'écran 2 barres grises
{
	/*
    byte ceiling=vgaCeiling[gamestate.episode*10+mapon];

    int y;
    byte *ptr = vbuf;
#ifdef USE_SHADING
    for(y = 0; y < viewheight / 2; y++, ptr += vbufPitch)
        memset(ptr, shadetable[GetShade((viewheight / 2 - y) << 3)][ceiling], viewwidth);
    for(; y < viewheight; y++, ptr += vbufPitch)
        memset(ptr, shadetable[GetShade((y - viewheight / 2) << 3)][0x19], viewwidth);
#else
    for(y = 0; y < viewheight / 2; y++, ptr += vbufPitch)
        memset(ptr, ceiling, viewwidth);
    for(; y < viewheight; y++, ptr += vbufPitch)
        memset(ptr, 0x19, viewwidth);
#endif
*/
}

//==========================================================================

/*
=====================
=
= CalcRotate
=
=====================
*/

int CalcRotate (objtype *ob)
{
    int angle, viewangle;

    // this isn't exactly correct, as it should vary by a trig value,
    // but it is close enough with only eight rotations

    viewangle = player->angle + (centerx - ob->viewx)/8;

    if (ob->obclass == rocketobj || ob->obclass == hrocketobj)
        angle = (viewangle-180) - ob->angle;
    else
        angle = (viewangle-180) - dirangle[ob->dir];

    angle+=ANGLES/16;
    while (angle>=ANGLES)
        angle-=ANGLES;
    while (angle<0)
        angle+=ANGLES;

    if (ob->state->rotate == 2)             // 2 rotation pain frame
        return 0;               // pain with shooting frame bugfix

    return angle/(ANGLES/8);
}

void ScaleShape (int xcenter, int shapenum, unsigned height, uint32_t flags)
{
    unsigned scale,pixheight;

#ifdef USE_SHADING
    byte *curshades;
    if(flags & FL_FULLBRIGHT)
        curshades = shadetable[0];
    else
        curshades = shadetable[GetShade(height)];
#endif
    scale=height/8;                 // low three bits are fractional
    if(!scale) return;   // too close or far away
    pixheight=scale*SPRITESCALEFACTOR;

#if USE_SPRITES
//xxxxxxxxxxxxxxxxx
	if(shapenum>SPR_STAT_47)
	loadActorTexture(shapenum);
//--------------------------------------------------------------------------------------------
	extern 	TEXTURE tex_spr[];		
	TEXTURE *txptr = &tex_spr[SATURN_WIDTH+shapenum]; 
// correct on touche pas		
    SPRITE user_sprite;
    user_sprite.CTRL = FUNC_Sprite | _ZmCC;
    user_sprite.PMOD=CL256Bnk| ECdis;// | ECenb | SPdis;  // pas besoin pour les sprites
    user_sprite.SRCA=((txptr->CGadr));
    user_sprite.COLR=256;
    user_sprite.SIZE=0x840;
	user_sprite.XA=(xcenter-centerx);
	user_sprite.YA=0;
	user_sprite.XB=pixheight;
	user_sprite.YB=user_sprite.XB;
    user_sprite.GRDA=0;
	slSetSprite(&user_sprite, toFIXED(0+(SATURN_SORT_VALUE-pixheight/2)));	
//--------------------------------------------------------------------------------------------	
#else
    t_compshape *shape;

    unsigned starty,endy;
    word *cmdptr;
    byte *cline;
    byte *line;
    byte *vmem;
    unsigned j;
    byte col;
    int actx,i,upperedge;
    short newstart;
    int scrstarty,screndy,lpix,rpix,pixcnt,ycnt;
	
    actx=xcenter-scale;
    upperedge=viewheight/2-scale;

    shape = (t_compshape *) PM_GetSprite(shapenum);
	
    cmdptr=(word *) shape->dataofs;
	
    for(i=shape->leftpix,pixcnt=i*pixheight,rpix=(pixcnt>>6)+actx;i<=shape->rightpix;i++,cmdptr++)
    {
        lpix=rpix;
        if(lpix>=viewwidth) break;
        pixcnt+=pixheight;
        rpix=(pixcnt>>6)+actx;
        if(lpix!=rpix && rpix>0)
        {
            if(lpix<0) lpix=0;
            if(rpix>viewwidth) rpix=viewwidth,i=shape->rightpix+1;
            cline=(byte *)shape + *cmdptr;
            while(lpix<rpix)
            {
                if(wallheight[lpix]<=(int)height)
                {
                    line=cline;
                    while((endy = READWORD(line)) != 0)
                    {
                        endy >>= 1;
                        newstart = READWORD(line);
                        starty = READWORD(line) >> 1;
                        j=starty;
                        ycnt=j*pixheight;
                        screndy=(ycnt>>6)+upperedge;
                        if(screndy<0) vmem=vbuf+lpix;
                        else vmem=vbuf+screndy*vbufPitch+lpix;
                        for(;j<endy;j++)
                        {
                            scrstarty=screndy;
                            ycnt+=pixheight;
                            screndy=(ycnt>>6)+upperedge;
                            if(scrstarty!=screndy && screndy>0)
                            {
#ifdef USE_SHADING
                                col=curshades[((byte *)shape)[newstart+j]];
#else
                                col=((byte *)shape)[newstart+j];
#endif
                                if(scrstarty<0) scrstarty=0;
                                if(screndy>viewheight) screndy=viewheight,j=endy;

                                while(scrstarty<screndy)
                                {
                                    *vmem=col;
                                    vmem+=vbufPitch;
                                    scrstarty++;
                                }
                            }
                        }
                    }
                }
                lpix++;
            }
        }
    }
#endif	
}

void SimpleScaleShape (int xcenter, int shapenum, unsigned height,unsigned vbufPitch)
{
    t_compshape   *shape;
    unsigned scale,pixheight;
    unsigned starty,endy;
    word *cmdptr;
    byte *cline;
    byte *line;
    int actx,i,upperedge;
    short newstart;
    int scrstarty,screndy,lpix,rpix,pixcnt,ycnt;
    unsigned j;
    byte col;
    byte *vmem;

    shape = (t_compshape *) PM_GetSprite(shapenum);

    scale=height>>1;
    pixheight=scale*SPRITESCALEFACTOR;
    actx=xcenter-scale;
    upperedge=viewheight/2-scale;

    cmdptr=shape->dataofs;

#if 0

#else // vbt on affiche l'arme en bitmap
// vbt : ajout nettoyage bitmap vdp2
	for( Sint16 i=0;i<height;i++)
	{
		Uint8*d = (Uint8*)vbuf + ((pixheight-i-1) * vbufPitch) + (xcenter-scale); 
		memset(d,0x0,height);
	}

    for(i=shape->leftpix,pixcnt=i*pixheight,rpix=(pixcnt>>6)+actx;i<=shape->rightpix;i++,cmdptr++)
    {
        lpix=rpix;
        if(lpix>=viewwidth) break;
        pixcnt+=pixheight;
        rpix=(pixcnt>>6)+actx;
        if(lpix!=rpix && rpix>0)
        {
            if(lpix<0) lpix=0;
            if(rpix>viewwidth) rpix=viewwidth,i=shape->rightpix+1;
            cline = (byte *)shape + *cmdptr;
            while(lpix<rpix)
            {
                line=cline;
                while((endy = READWORD(line)) != 0)
                {
                    endy >>= 1;
                    newstart = READWORD(line);
                    starty = READWORD(line) >> 1;
                    j=starty;
                    ycnt=j*pixheight;
                    screndy=(ycnt>>6)+upperedge;
                    if(screndy<0) vmem=vbuf+lpix;
                    else vmem=vbuf+screndy*vbufPitch+lpix;
                    for(;j<endy;j++)
                    {
                        scrstarty=screndy;
                        ycnt+=pixheight;
                        screndy=(ycnt>>6)+upperedge;
                        if(scrstarty!=screndy && screndy>0)
                        {
                            col=((byte *)shape)[newstart+j];
                            if(scrstarty<0) scrstarty=0;
                            if(screndy>viewheight) screndy=viewheight,j=endy;

                            while(scrstarty<screndy)
                            {
                                *vmem=col;
                                vmem+=vbufPitch;
                                scrstarty++;
                            }
                        }
                    }
                }
                lpix++;
            }
        }
    }
#endif	
}

/*
=====================
=
= DrawScaleds
=
= Draws all objects that are visable
=
=====================
*/

#define MAXVISABLE 250

typedef struct
{
    short      viewx,
               viewheight,
               shapenum;
    short      flags;          // this must be changed to uint32_t, when you
                               // you need more than 16-flags for drawing
#ifdef USE_DIR3DSPR
    statobj_t *transsprite;
#endif
} visobj_t;


visobj_t vislist[MAXVISABLE];
visobj_t *visptr,*visstep,*farthest;

void DrawScaleds (void)
{
    int      i,least,numvisable,height;
    byte     *tilespot,*visspot;
    unsigned spotloc;

    statobj_t *statptr;
    objtype   *obj;

    visptr = &vislist[0];



//
// place static objects
//
    for (statptr = &statobjlist[0] ; statptr !=laststatobj ; statptr++)
    {
        if ((visptr->shapenum = statptr->shapenum) == -1)
            continue;                                               // object has been deleted

        if (!*statptr->visspot)
            continue;                                               // not visable

        if (TransformTile (statptr->tilex,statptr->tiley,statptr->shapenum,
            &visptr->viewx,&visptr->viewheight) && statptr->flags & FL_BONUS)
        {
            GetBonus (statptr);
            if(statptr->shapenum == -1)
                continue;                                           // object has been taken
        }

        if (!visptr->viewheight)
            continue;                                               // to close to the object

#ifdef USE_DIR3DSPR
        if(statptr->flags & FL_DIR_MASK)
            visptr->transsprite=statptr;
        else
            visptr->transsprite=NULL;
#endif

        if (visptr < &vislist[MAXVISABLE-1])    // don't let it overflow
        {
            visptr->flags = (short) statptr->flags;
            visptr++;
        }
    }
//
// place active objects
//
    for (obj = player->next;obj;obj=obj->next)
    {
        if ((visptr->shapenum = obj->state->shapenum)==0)
            continue;                                               // no shape

        spotloc = (obj->tilex<<mapshift)+obj->tiley;   // optimize: keep in struct?
        visspot = &spotvis[0][0]+spotloc;
        tilespot = &tilemap[0][0]+spotloc;

        //
        // could be in any of the nine surrounding tiles
        //
        if (*visspot
            || ( *(visspot-1) && !*(tilespot-1) )
            || ( *(visspot+1) && !*(tilespot+1) )
            || ( *(visspot-65) && !*(tilespot-65) )
            || ( *(visspot-64) && !*(tilespot-64) )
            || ( *(visspot-63) && !*(tilespot-63) )
            || ( *(visspot+65) && !*(tilespot+65) )
            || ( *(visspot+64) && !*(tilespot+64) )
            || ( *(visspot+63) && !*(tilespot+63) ) )
        {
            obj->active = ac_yes;
            TransformActor (obj);
            if (!obj->viewheight)
                continue;                                               // too close or far away

            visptr->viewx = obj->viewx;
            visptr->viewheight = obj->viewheight;
            if (visptr->shapenum == -1)
                visptr->shapenum = obj->temp1;  // special shape

            if (obj->state->rotate)
                visptr->shapenum += CalcRotate (obj);

            if (visptr < &vislist[MAXVISABLE-1])    // don't let it overflow
            {
                visptr->flags = (short) obj->flags;
#ifdef USE_DIR3DSPR
                visptr->transsprite = NULL;
#endif
                visptr++;
            }
            obj->flags |= FL_VISABLE;
        }
        else
            obj->flags &= ~FL_VISABLE;
    }

//
// draw from back to front
//
    numvisable = (int) (visptr-&vislist[0]);

    if (!numvisable)
        return;                                                                 // no visable objects

    for (i = 0; i<numvisable; i++)
    {
        least = 32000;
        for (visstep=&vislist[0] ; visstep<visptr ; visstep++)
        {
            height = visstep->viewheight;
            if (height < least)
            {
                least = height;
                farthest = visstep;
            }
        }
        //
        // draw farthest
        //
#ifdef USE_DIR3DSPR
        if(farthest->transsprite)
            Scale3DShape(vbuf, vbufPitch, farthest->transsprite);
        else
#endif
// affiche la version bitmap
		ScaleShape(farthest->viewx, farthest->shapenum, farthest->viewheight, farthest->flags);
        farthest->viewheight = 32000;
    }


}

//==========================================================================

/*
==============
=
= DrawPlayerWeapon
=
= Draw the player's hands
=
==============
*/

int weaponscale[NUMWEAPONS] = {SPR_KNIFEREADY, SPR_PISTOLREADY,
    SPR_MACHINEGUNREADY, SPR_CHAINREADY};

void DrawPlayerWeapon ()
{
    int shapenum;

#ifndef SPEAR
    if (gamestate.victoryflag)
    {
#ifndef APOGEE_1_0
        if (player->state == &s_deathcam && (GetTimeCount()&32) )
            SimpleScaleShape(viewwidth/2,SPR_DEATHCAM,viewheight+1,bufferPitch);
#endif
        return;
    }
#endif

    if (gamestate.weapon != -1)
    {
        shapenum = weaponscale[gamestate.weapon]+gamestate.weaponframe;
        SimpleScaleShape(viewwidth/2,shapenum,viewheight+1,bufferPitch);
    }

    if (demorecord || demoplayback)
        SimpleScaleShape(viewwidth/2,SPR_DEMO,viewheight+1,bufferPitch);
}


//==========================================================================


/*
=====================
=
= CalcTics
=
=====================
*/

void CalcTics (void)
{
//
// calculate tics since last refresh for adaptive timing
//
    if (lasttimecount > (int32_t) GetTimeCount())
        lasttimecount = GetTimeCount();    // if the game was paused a LONG time

    uint32_t curtime = SDL_GetTicks();
    tics = (curtime * 7) / 100 - lasttimecount;
    if(!tics)
    {
        // wait until end of current tic
        SDL_Delay(((lasttimecount + 1) * 100) / 7 - curtime);
        tics = 1;
    }

    lasttimecount += tics;

    if (tics>MAXTICS)
        tics = MAXTICS;
}


//==========================================================================

void AsmRefresh()
{
// vbt :moins de variable globale
	longword xpartialup,xpartialdown,ypartialup,ypartialdown;
//	word tilehit;
	word xspot,yspot;
	
    xpartialdown = viewx&(TILEGLOBAL-1);
    xpartialup = TILEGLOBAL-xpartialdown;
    ypartialdown = viewy&(TILEGLOBAL-1);
    ypartialup = TILEGLOBAL-ypartialdown;	
	
    int32_t xstep=0,ystep=0;
    longword xpartial=0,ypartial=0;
	ray_struc my_ray;
	
    my_ray.lastside = -1;                  // the first pixel is on a new wall	
    boolean playerInPushwallBackTile = tilemap[focaltx][focalty] == 64;

    for(int pixx=0;pixx<viewwidth;pixx++)
    {
        short angl=midangle+pixelangle[pixx];
        if(angl<0) angl+=FINEANGLES;
        if(angl>=3600) angl-=FINEANGLES;
        if(angl<900)
        {
            my_ray.xtilestep=1;
            my_ray.ytilestep=-1;
            xstep=finetangent[900-1-angl];
            ystep=-finetangent[angl];
            xpartial=xpartialup;
            ypartial=ypartialdown;
        }
        else if(angl<1800)
        {
            my_ray.xtilestep=-1;
            my_ray.ytilestep=-1;
            xstep=-finetangent[angl-900];
            ystep=-finetangent[1800-1-angl];
            xpartial=xpartialdown;
            ypartial=ypartialdown;
        }
        else if(angl<2700)
        {
            my_ray.xtilestep=-1;
            my_ray.ytilestep=1;
            xstep=-finetangent[2700-1-angl];
            ystep=finetangent[angl-1800];
            xpartial=xpartialdown;
            ypartial=ypartialup;
        }
        else if(angl<3600)
        {
            my_ray.xtilestep=1;
            my_ray.ytilestep=1;
            xstep=finetangent[angl-2700];
            ystep=finetangent[3600-1-angl];
            xpartial=xpartialup;
            ypartial=ypartialup;
        }
        my_ray.yintercept=FixedMul(ystep,xpartial)+viewy;
        my_ray.xtile=focaltx+my_ray.xtilestep;
        xspot=(word)((my_ray.xtile<<mapshift)+((uint32_t)my_ray.yintercept>>16));
        my_ray.xintercept=FixedMul(xstep,ypartial)+viewx;
        my_ray.ytile=focalty+my_ray.ytilestep;
        yspot=(word)((((uint32_t)my_ray.xintercept>>16)<<mapshift)+my_ray.ytile);
        my_ray.texdelta=0;

        // Special treatment when player is in back tile of pushwall
        if(playerInPushwallBackTile)
        {
            if(    pwalldir == di_east && my_ray.xtilestep ==  1
                || pwalldir == di_west && my_ray.xtilestep == -1)
            {
                int32_t yintbuf = my_ray.yintercept - ((ystep * (64 - pwallpos)) >> 6);
                if((yintbuf >> 16) == focalty)   // ray hits pushwall back?
                {
                    if(pwalldir == di_east)
                        my_ray.xintercept = (focaltx << TILESHIFT) + (pwallpos << 10);
                    else
                        my_ray.xintercept = (focaltx << TILESHIFT) - TILEGLOBAL + ((64 - pwallpos) << 10);
                    my_ray.yintercept = yintbuf;
                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                    my_ray.tilehit = pwalltile;
                    HitVertWall(pixx,&my_ray);
                    continue;
                }
            }
            else if(pwalldir == di_south && my_ray.ytilestep ==  1
                ||  pwalldir == di_north && my_ray.ytilestep == -1)
            {
                int32_t xintbuf = my_ray.xintercept - ((xstep * (64 - pwallpos)) >> 6);
                if((xintbuf >> 16) == focaltx)   // ray hits pushwall back?
                {
                    my_ray.xintercept = xintbuf;
                    if(pwalldir == di_south)
                        my_ray.yintercept = (focalty << TILESHIFT) + (pwallpos << 10);
                    else
                        my_ray.yintercept = (focalty << TILESHIFT) - TILEGLOBAL + ((64 - pwallpos) << 10);
// vbt new
                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                    my_ray.tilehit = pwalltile;
                    HitHorizWall(pixx,&my_ray);
                    continue;
                }
            }
        }

        do
        {
            if(my_ray.ytilestep==-1 && (my_ray.yintercept>>16)<=my_ray.ytile) goto horizentry;
            if(my_ray.ytilestep==1 && (my_ray.yintercept>>16)>=my_ray.ytile) goto horizentry;
vertentry:
            if((uint32_t)my_ray.yintercept>mapheight*65536-1 || (word)my_ray.xtile>=mapwidth)
            {
                if(my_ray.xtile<0) my_ray.xintercept=0, my_ray.xtile=0;
                else if(my_ray.xtile>=mapwidth) my_ray.xintercept=mapwidth<<TILESHIFT, my_ray.xtile=mapwidth-1;
                else my_ray.xtile=(short) (my_ray.xintercept >> TILESHIFT);
                if(my_ray.yintercept<0) my_ray.yintercept=0, my_ray.ytile=0;
                else if(my_ray.yintercept>=(mapheight<<TILESHIFT)) my_ray.yintercept=mapheight<<TILESHIFT, my_ray.ytile=mapheight-1;
                yspot=0xffff;
                my_ray.tilehit=0;
                HitHorizBorder(pixx,&my_ray);
                break;
            }
            if(xspot>=maparea) break;
            my_ray.tilehit=((byte *)tilemap)[xspot];
            if(my_ray.tilehit)
            {
                if(my_ray.tilehit&0x80)
                {
                    int32_t yintbuf=my_ray.yintercept+(ystep>>1);
                    if((yintbuf>>16)!=(my_ray.yintercept>>16))
                        goto passvert;
                    if((word)yintbuf<doorposition[my_ray.tilehit&0x7f])
                        goto passvert;
                    my_ray.yintercept=yintbuf;
                    my_ray.xintercept=(my_ray.xtile<<TILESHIFT)|0x8000;
// vbt new
                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                    HitVertDoor2(pixx,&my_ray);
                }
                else
                {
                    if(my_ray.tilehit==64)
                    {
                        if(pwalldir==di_west || pwalldir==di_east)
                        {
	                        int32_t yintbuf;
                            int pwallposnorm;
                            int pwallposinv;
                            if(pwalldir==di_west)
                            {
                                pwallposnorm = 64-pwallpos;
                                pwallposinv = pwallpos;
                            }
                            else
                            {
                                pwallposnorm = pwallpos;
                                pwallposinv = 64-pwallpos;
                            }
                            if(pwalldir == di_east && my_ray.xtile==pwallx && ((uint32_t)my_ray.yintercept>>16)==pwally
                                || pwalldir == di_west && !(my_ray.xtile==pwallx && ((uint32_t)my_ray.yintercept>>16)==pwally))
                            {
                                yintbuf=my_ray.yintercept+((ystep*pwallposnorm)>>6);
                                if((yintbuf>>16)!=(my_ray.yintercept>>16))
                                    goto passvert;

                                my_ray.xintercept=(my_ray.xtile<<TILESHIFT)+TILEGLOBAL-(pwallposinv<<10);
                                my_ray.yintercept=yintbuf;
// vbt new
                                my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                my_ray.tilehit=pwalltile;
                                HitVertWall(pixx,&my_ray);
                            }
                            else
                            {
                                yintbuf=my_ray.yintercept+((ystep*pwallposinv)>>6);
                                if((yintbuf>>16)!=(my_ray.yintercept>>16))
                                    goto passvert;

                                my_ray.xintercept=(my_ray.xtile<<TILESHIFT)-(pwallposinv<<10);
                                my_ray.yintercept=yintbuf;
// vbt new
                                my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                my_ray.tilehit=pwalltile;
                                HitVertWall(pixx,&my_ray);
                            }
                        }
                        else
                        {
                            int pwallposi = pwallpos;
                            if(pwalldir==di_north) pwallposi = 64-pwallpos;
                            if(pwalldir==di_south && (word)my_ray.yintercept<(pwallposi<<10)
                                || pwalldir==di_north && (word)my_ray.yintercept>(pwallposi<<10))
                            {
                                if(((uint32_t)my_ray.yintercept>>16)==pwally && my_ray.xtile==pwallx)
                                {
                                    if(pwalldir==di_south && (int32_t)((word)my_ray.yintercept)+ystep<(pwallposi<<10)
                                            || pwalldir==di_north && (int32_t)((word)my_ray.yintercept)+ystep>(pwallposi<<10))
                                        goto passvert;

                                    if(pwalldir==di_south)
                                        my_ray.yintercept=(my_ray.yintercept&0xffff0000)+(pwallposi<<10);
                                    else
                                        my_ray.yintercept=(my_ray.yintercept&0xffff0000)-TILEGLOBAL+(pwallposi<<10);
                                    my_ray.xintercept=my_ray.xintercept-((xstep*(64-pwallpos))>>6);
// vbt new
                                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
									HitHorizWall(pixx,&my_ray);
                                }
                                else
                                {
                                    my_ray.texdelta = -(pwallposi<<10);
                                    my_ray.xintercept=my_ray.xtile<<TILESHIFT;
// vbt new
                                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitVertWall(pixx,&my_ray);
                                }
                            }
                            else
                            {
                                if(((uint32_t)my_ray.yintercept>>16)==pwally && my_ray.xtile==pwallx)
                                {
                                    my_ray.texdelta = -(pwallposi<<10);
                                    my_ray.xintercept=my_ray.xtile<<TILESHIFT;
// vbt new
                                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitVertWall(pixx,&my_ray);
                                }
                                else
                                {
                                    if(pwalldir==di_south && (int32_t)((word)my_ray.yintercept)+ystep>(pwallposi<<10)
                                            || pwalldir==di_north && (int32_t)((word)my_ray.yintercept)+ystep<(pwallposi<<10))
                                        goto passvert;

                                    if(pwalldir==di_south)
                                        my_ray.yintercept=(my_ray.yintercept&0xffff0000)-((64-pwallpos)<<10);
                                    else
                                        my_ray.yintercept=(my_ray.yintercept&0xffff0000)+((64-pwallpos)<<10);
                                    my_ray.xintercept=my_ray.xintercept-((xstep*pwallpos)>>6);
// vbt new
                                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
									HitHorizWall(pixx,&my_ray);
                                }
                            }
                        }
                    }
                    else
                    {
                        my_ray.xintercept=my_ray.xtile<<TILESHIFT;
// vbt new
                        my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                        HitVertWall(pixx,&my_ray);
                    }
                }
                break;
            }
passvert:
            *((byte *)spotvis+xspot)=1;
            my_ray.xtile+=my_ray.xtilestep;
            my_ray.yintercept+=ystep;
            xspot=(word)((my_ray.xtile<<mapshift)+((uint32_t)my_ray.yintercept>>16));
        }
        while(1);
        continue;

        do
        {
            if(my_ray.xtilestep==-1 && (my_ray.xintercept>>16)<=my_ray.xtile) goto vertentry;
            if(my_ray.xtilestep==1 && (my_ray.xintercept>>16)>=my_ray.xtile) goto vertentry;
horizentry:
            if((uint32_t)my_ray.xintercept>mapwidth*65536-1 || (word)my_ray.ytile>=mapheight)
            {
                if(my_ray.ytile<0) my_ray.yintercept=0, my_ray.ytile=0;
                else if(my_ray.ytile>=mapheight) my_ray.yintercept=mapheight<<TILESHIFT, my_ray.ytile=mapheight-1;
                else my_ray.ytile=(short) (my_ray.yintercept >> TILESHIFT);
                if(my_ray.xintercept<0) my_ray.xintercept=0, my_ray.xtile=0;
                else if(my_ray.xintercept>=(mapwidth<<TILESHIFT)) my_ray.xintercept=mapwidth<<TILESHIFT, my_ray.xtile=mapwidth-1;
                xspot=0xffff;
                my_ray.tilehit=0;
                HitVertBorder(pixx,&my_ray);
                break;
            }
            if(yspot>=maparea) break;
            my_ray.tilehit=((byte *)tilemap)[yspot];
            if(my_ray.tilehit)
            {
                if(my_ray.tilehit&0x80)
                {
                    int32_t xintbuf=my_ray.xintercept+(xstep>>1);
                    if((xintbuf>>16)!=(my_ray.xintercept>>16))
                        goto passhoriz;
                    if((word)xintbuf<doorposition[my_ray.tilehit&0x7f])
                        goto passhoriz;
                    my_ray.xintercept=xintbuf;
                    my_ray.yintercept=(my_ray.ytile<<TILESHIFT)+0x8000;
// vbt new
                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                    HitHorizDoor2(pixx,&my_ray);
                }
                else
                {
                    if(my_ray.tilehit==64)
                    {
                        if(pwalldir==di_north || pwalldir==di_south)
                        {
                            int32_t xintbuf;
                            int pwallposnorm;
                            int pwallposinv;
                            if(pwalldir==di_north)
                            {
                                pwallposnorm = 64-pwallpos;
                                pwallposinv = pwallpos;
                            }
                            else
                            {
                                pwallposnorm = pwallpos;
                                pwallposinv = 64-pwallpos;
                            }
                            if(pwalldir == di_south && my_ray.ytile==pwally && ((uint32_t)my_ray.xintercept>>16)==pwallx
                                || pwalldir == di_north && !(my_ray.ytile==pwally && ((uint32_t)my_ray.xintercept>>16)==pwallx))
                            {
                                xintbuf=my_ray.xintercept+((xstep*pwallposnorm)>>6);
                                if((xintbuf>>16)!=(my_ray.xintercept>>16))
                                    goto passhoriz;

                                my_ray.yintercept=(my_ray.ytile<<TILESHIFT)+TILEGLOBAL-(pwallposinv<<10);
                                my_ray.xintercept=xintbuf;
// vbt new
                                my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                my_ray.tilehit=pwalltile;
                                HitHorizWall(pixx,&my_ray);
                            }
                            else
                            {
                                xintbuf=my_ray.xintercept+((xstep*pwallposinv)>>6);
                                if((xintbuf>>16)!=(my_ray.xintercept>>16))
                                    goto passhoriz;

                                my_ray.yintercept=(my_ray.ytile<<TILESHIFT)-(pwallposinv<<10);
                                my_ray.xintercept=xintbuf;
// vbt new
                                my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                my_ray.tilehit=pwalltile;
                                HitHorizWall(pixx,&my_ray);
                            }
                        }
                        else
                        {
                            int pwallposi = pwallpos;
                            if(pwalldir==di_west) pwallposi = 64-pwallpos;
                            if(pwalldir==di_east && (word)my_ray.xintercept<(pwallposi<<10)
                                    || pwalldir==di_west && (word)my_ray.xintercept>(pwallposi<<10))
                            {
                                if(((uint32_t)my_ray.xintercept>>16)==pwallx && my_ray.ytile==pwally)
                                {
                                    if(pwalldir==di_east && (int32_t)((word)my_ray.xintercept)+xstep<(pwallposi<<10)
                                            || pwalldir==di_west && (int32_t)((word)my_ray.xintercept)+xstep>(pwallposi<<10))
                                        goto passhoriz;

                                    if(pwalldir==di_east)
                                        my_ray.xintercept=(my_ray.xintercept&0xffff0000)+(pwallposi<<10);
                                    else
                                        my_ray.xintercept=(my_ray.xintercept&0xffff0000)-TILEGLOBAL+(pwallposi<<10);
                                    my_ray.yintercept=my_ray.yintercept-((ystep*(64-pwallpos))>>6);
// vbt new
                                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitVertWall(pixx,&my_ray);
                                }
                                else
                                {
                                    my_ray.texdelta = -(pwallposi<<10);
                                    my_ray.yintercept=my_ray.ytile<<TILESHIFT;
// vbt new
                                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitHorizWall(pixx,&my_ray);
                                }
                            }
                            else
                            {
                                if(((uint32_t)my_ray.xintercept>>16)==pwallx && my_ray.ytile==pwally)
                                {
                                    my_ray.texdelta = -(pwallposi<<10);
                                    my_ray.yintercept=my_ray.ytile<<TILESHIFT;
// vbt new
                                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitHorizWall(pixx,&my_ray);
                                }
                                else
                                {
                                    if(pwalldir==di_east && (int32_t)((word)my_ray.xintercept)+xstep>(pwallposi<<10)
                                            || pwalldir==di_west && (int32_t)((word)my_ray.xintercept)+xstep<(pwallposi<<10))
                                        goto passhoriz;

                                    if(pwalldir==di_east)
                                        my_ray.xintercept=(my_ray.xintercept&0xffff0000)-((64-pwallpos)<<10);
                                    else
                                        my_ray.xintercept=(my_ray.xintercept&0xffff0000)+((64-pwallpos)<<10);
                                    my_ray.yintercept=my_ray.yintercept-((ystep*pwallpos)>>6);
// vbt new
                                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitVertWall(pixx,&my_ray);
                                }
                            }
                        }
                    }
                    else
                    {
                        my_ray.yintercept=my_ray.ytile<<TILESHIFT;
// vbt new
                        my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                        HitHorizWall(pixx,&my_ray);
                    }
                }
                break;
            }
passhoriz:
            *((byte *)spotvis+yspot)=1;
            my_ray.ytile+=my_ray.ytilestep;
            my_ray.xintercept+=xstep;
            yspot=(word)((((uint32_t)my_ray.xintercept>>16)<<mapshift)+my_ray.ytile);
        }
        while(1);
    }
// vbt : uniquement à la fin de tous les strips	
	    ScalePost(my_ray.postx,my_ray.postsource);                   // no more optimization on last post
}


//==========================================================================

void AsmRefreshSlave()
{
// vbt :moins de variable globale
	longword xpartialup,xpartialdown,ypartialup,ypartialdown;
//	word tilehit;
	word xspot,yspot;
	
    xpartialdown = viewx&(TILEGLOBAL-1);
    xpartialup = TILEGLOBAL-xpartialdown;
    ypartialdown = viewy&(TILEGLOBAL-1);
    ypartialup = TILEGLOBAL-ypartialdown;	
	
    int32_t xstep=0,ystep=0;
    longword xpartial=0,ypartial=0;
	ray_struc my_ray;
	
//    my_ray.lastside = -1;                  // the first pixel is on a new wall	
    boolean playerInPushwallBackTile = tilemap[focaltx][focalty] == 64;

    for(int pixx=viewwidth/2;pixx<viewwidth;pixx++)
    {
        short angl=midangle+pixelangle[pixx];
        if(angl<0) angl+=FINEANGLES;
        if(angl>=3600) angl-=FINEANGLES;
        if(angl<900)
        {
            my_ray.xtilestep=1;
            my_ray.ytilestep=-1;
            xstep=finetangent[900-1-angl];
            ystep=-finetangent[angl];
            xpartial=xpartialup;
            ypartial=ypartialdown;
        }
        else if(angl<1800)
        {
            my_ray.xtilestep=-1;
            my_ray.ytilestep=-1;
            xstep=-finetangent[angl-900];
            ystep=-finetangent[1800-1-angl];
            xpartial=xpartialdown;
            ypartial=ypartialdown;
        }
        else if(angl<2700)
        {
            my_ray.xtilestep=-1;
            my_ray.ytilestep=1;
            xstep=-finetangent[2700-1-angl];
            ystep=finetangent[angl-1800];
            xpartial=xpartialdown;
            ypartial=ypartialup;
        }
        else if(angl<3600)
        {
            my_ray.xtilestep=1;
            my_ray.ytilestep=1;
            xstep=finetangent[angl-2700];
            ystep=finetangent[3600-1-angl];
            xpartial=xpartialup;
            ypartial=ypartialup;
        }
        my_ray.yintercept=FixedMul(ystep,xpartial)+viewy;
        my_ray.xtile=focaltx+my_ray.xtilestep;
        xspot=(word)((my_ray.xtile<<mapshift)+((uint32_t)my_ray.yintercept>>16));
        my_ray.xintercept=FixedMul(xstep,ypartial)+viewx;
        my_ray.ytile=focalty+my_ray.ytilestep;
        yspot=(word)((((uint32_t)my_ray.xintercept>>16)<<mapshift)+my_ray.ytile);
        my_ray.texdelta=0;

        // Special treatment when player is in back tile of pushwall
        if(playerInPushwallBackTile)
        {
            if(    pwalldir == di_east && my_ray.xtilestep ==  1
                || pwalldir == di_west && my_ray.xtilestep == -1)
            {
                int32_t yintbuf = my_ray.yintercept - ((ystep * (64 - pwallpos)) >> 6);
                if((yintbuf >> 16) == focalty)   // ray hits pushwall back?
                {
                    if(pwalldir == di_east)
                        my_ray.xintercept = (focaltx << TILESHIFT) + (pwallpos << 10);
                    else
                        my_ray.xintercept = (focaltx << TILESHIFT) - TILEGLOBAL + ((64 - pwallpos) << 10);
                    my_ray.yintercept = yintbuf;
                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                    my_ray.tilehit = pwalltile;
                    HitVertWall(pixx,&my_ray);
                    continue;
                }
            }
            else if(pwalldir == di_south && my_ray.ytilestep ==  1
                ||  pwalldir == di_north && my_ray.ytilestep == -1)
            {
                int32_t xintbuf = my_ray.xintercept - ((xstep * (64 - pwallpos)) >> 6);
                if((xintbuf >> 16) == focaltx)   // ray hits pushwall back?
                {
                    my_ray.xintercept = xintbuf;
                    if(pwalldir == di_south)
                        my_ray.yintercept = (focalty << TILESHIFT) + (pwallpos << 10);
                    else
                        my_ray.yintercept = (focalty << TILESHIFT) - TILEGLOBAL + ((64 - pwallpos) << 10);
// vbt new
                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                    my_ray.tilehit = pwalltile;
                    HitHorizWall(pixx,&my_ray);
                    continue;
                }
            }
        }

        do
        {
            if(my_ray.ytilestep==-1 && (my_ray.yintercept>>16)<=my_ray.ytile) goto horizentry;
            if(my_ray.ytilestep==1 && (my_ray.yintercept>>16)>=my_ray.ytile) goto horizentry;
vertentry:
            if((uint32_t)my_ray.yintercept>mapheight*65536-1 || (word)my_ray.xtile>=mapwidth)
            {
                if(my_ray.xtile<0) my_ray.xintercept=0, my_ray.xtile=0;
                else if(my_ray.xtile>=mapwidth) my_ray.xintercept=mapwidth<<TILESHIFT, my_ray.xtile=mapwidth-1;
                else my_ray.xtile=(short) (my_ray.xintercept >> TILESHIFT);
                if(my_ray.yintercept<0) my_ray.yintercept=0, my_ray.ytile=0;
                else if(my_ray.yintercept>=(mapheight<<TILESHIFT)) my_ray.yintercept=mapheight<<TILESHIFT, my_ray.ytile=mapheight-1;
                yspot=0xffff;
                my_ray.tilehit=0;
                HitHorizBorder(pixx,&my_ray);
                break;
            }
            if(xspot>=maparea) break;
            my_ray.tilehit=((byte *)tilemap)[xspot];
            if(my_ray.tilehit)
            {
                if(my_ray.tilehit&0x80)
                {
                    int32_t yintbuf=my_ray.yintercept+(ystep>>1);
                    if((yintbuf>>16)!=(my_ray.yintercept>>16))
                        goto passvert;
                    if((word)yintbuf<doorposition[my_ray.tilehit&0x7f])
                        goto passvert;
                    my_ray.yintercept=yintbuf;
                    my_ray.xintercept=(my_ray.xtile<<TILESHIFT)|0x8000;
// vbt new
                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                    HitVertDoor2(pixx,&my_ray);
                }
                else
                {
                    if(my_ray.tilehit==64)
                    {
                        if(pwalldir==di_west || pwalldir==di_east)
                        {
	                        int32_t yintbuf;
                            int pwallposnorm;
                            int pwallposinv;
                            if(pwalldir==di_west)
                            {
                                pwallposnorm = 64-pwallpos;
                                pwallposinv = pwallpos;
                            }
                            else
                            {
                                pwallposnorm = pwallpos;
                                pwallposinv = 64-pwallpos;
                            }
                            if(pwalldir == di_east && my_ray.xtile==pwallx && ((uint32_t)my_ray.yintercept>>16)==pwally
                                || pwalldir == di_west && !(my_ray.xtile==pwallx && ((uint32_t)my_ray.yintercept>>16)==pwally))
                            {
                                yintbuf=my_ray.yintercept+((ystep*pwallposnorm)>>6);
                                if((yintbuf>>16)!=(my_ray.yintercept>>16))
                                    goto passvert;

                                my_ray.xintercept=(my_ray.xtile<<TILESHIFT)+TILEGLOBAL-(pwallposinv<<10);
                                my_ray.yintercept=yintbuf;
// vbt new
                                my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                my_ray.tilehit=pwalltile;
                                HitVertWall(pixx,&my_ray);
                            }
                            else
                            {
                                yintbuf=my_ray.yintercept+((ystep*pwallposinv)>>6);
                                if((yintbuf>>16)!=(my_ray.yintercept>>16))
                                    goto passvert;

                                my_ray.xintercept=(my_ray.xtile<<TILESHIFT)-(pwallposinv<<10);
                                my_ray.yintercept=yintbuf;
// vbt new
                                my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                my_ray.tilehit=pwalltile;
                                HitVertWall(pixx,&my_ray);
                            }
                        }
                        else
                        {
                            int pwallposi = pwallpos;
                            if(pwalldir==di_north) pwallposi = 64-pwallpos;
                            if(pwalldir==di_south && (word)my_ray.yintercept<(pwallposi<<10)
                                || pwalldir==di_north && (word)my_ray.yintercept>(pwallposi<<10))
                            {
                                if(((uint32_t)my_ray.yintercept>>16)==pwally && my_ray.xtile==pwallx)
                                {
                                    if(pwalldir==di_south && (int32_t)((word)my_ray.yintercept)+ystep<(pwallposi<<10)
                                            || pwalldir==di_north && (int32_t)((word)my_ray.yintercept)+ystep>(pwallposi<<10))
                                        goto passvert;

                                    if(pwalldir==di_south)
                                        my_ray.yintercept=(my_ray.yintercept&0xffff0000)+(pwallposi<<10);
                                    else
                                        my_ray.yintercept=(my_ray.yintercept&0xffff0000)-TILEGLOBAL+(pwallposi<<10);
                                    my_ray.xintercept=my_ray.xintercept-((xstep*(64-pwallpos))>>6);
// vbt new
                                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
									HitHorizWall(pixx,&my_ray);
                                }
                                else
                                {
                                    my_ray.texdelta = -(pwallposi<<10);
                                    my_ray.xintercept=my_ray.xtile<<TILESHIFT;
// vbt new
                                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitVertWall(pixx,&my_ray);
                                }
                            }
                            else
                            {
                                if(((uint32_t)my_ray.yintercept>>16)==pwally && my_ray.xtile==pwallx)
                                {
                                    my_ray.texdelta = -(pwallposi<<10);
                                    my_ray.xintercept=my_ray.xtile<<TILESHIFT;
// vbt new
                                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitVertWall(pixx,&my_ray);
                                }
                                else
                                {
                                    if(pwalldir==di_south && (int32_t)((word)my_ray.yintercept)+ystep>(pwallposi<<10)
                                            || pwalldir==di_north && (int32_t)((word)my_ray.yintercept)+ystep<(pwallposi<<10))
                                        goto passvert;

                                    if(pwalldir==di_south)
                                        my_ray.yintercept=(my_ray.yintercept&0xffff0000)-((64-pwallpos)<<10);
                                    else
                                        my_ray.yintercept=(my_ray.yintercept&0xffff0000)+((64-pwallpos)<<10);
                                    my_ray.xintercept=my_ray.xintercept-((xstep*pwallpos)>>6);
// vbt new
                                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
									HitHorizWall(pixx,&my_ray);
                                }
                            }
                        }
                    }
                    else
                    {
                        my_ray.xintercept=my_ray.xtile<<TILESHIFT;
// vbt new
                        my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                        HitVertWall(pixx,&my_ray);
                    }
                }
                break;
            }
passvert:
            *((byte *)spotvis+xspot)=1;
            my_ray.xtile+=my_ray.xtilestep;
            my_ray.yintercept+=ystep;
            xspot=(word)((my_ray.xtile<<mapshift)+((uint32_t)my_ray.yintercept>>16));
        }
        while(1);
        continue;

        do
        {
            if(my_ray.xtilestep==-1 && (my_ray.xintercept>>16)<=my_ray.xtile) goto vertentry;
            if(my_ray.xtilestep==1 && (my_ray.xintercept>>16)>=my_ray.xtile) goto vertentry;
horizentry:
            if((uint32_t)my_ray.xintercept>mapwidth*65536-1 || (word)my_ray.ytile>=mapheight)
            {
                if(my_ray.ytile<0) my_ray.yintercept=0, my_ray.ytile=0;
                else if(my_ray.ytile>=mapheight) my_ray.yintercept=mapheight<<TILESHIFT, my_ray.ytile=mapheight-1;
                else my_ray.ytile=(short) (my_ray.yintercept >> TILESHIFT);
                if(my_ray.xintercept<0) my_ray.xintercept=0, my_ray.xtile=0;
                else if(my_ray.xintercept>=(mapwidth<<TILESHIFT)) my_ray.xintercept=mapwidth<<TILESHIFT, my_ray.xtile=mapwidth-1;
                xspot=0xffff;
                my_ray.tilehit=0;
                HitVertBorder(pixx,&my_ray);
                break;
            }
            if(yspot>=maparea) break;
            my_ray.tilehit=((byte *)tilemap)[yspot];
            if(my_ray.tilehit)
            {
                if(my_ray.tilehit&0x80)
                {
                    int32_t xintbuf=my_ray.xintercept+(xstep>>1);
                    if((xintbuf>>16)!=(my_ray.xintercept>>16))
                        goto passhoriz;
                    if((word)xintbuf<doorposition[my_ray.tilehit&0x7f])
                        goto passhoriz;
                    my_ray.xintercept=xintbuf;
                    my_ray.yintercept=(my_ray.ytile<<TILESHIFT)+0x8000;
// vbt new
                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                    HitHorizDoor2(pixx,&my_ray);
                }
                else
                {
                    if(my_ray.tilehit==64)
                    {
                        if(pwalldir==di_north || pwalldir==di_south)
                        {
                            int32_t xintbuf;
                            int pwallposnorm;
                            int pwallposinv;
                            if(pwalldir==di_north)
                            {
                                pwallposnorm = 64-pwallpos;
                                pwallposinv = pwallpos;
                            }
                            else
                            {
                                pwallposnorm = pwallpos;
                                pwallposinv = 64-pwallpos;
                            }
                            if(pwalldir == di_south && my_ray.ytile==pwally && ((uint32_t)my_ray.xintercept>>16)==pwallx
                                || pwalldir == di_north && !(my_ray.ytile==pwally && ((uint32_t)my_ray.xintercept>>16)==pwallx))
                            {
                                xintbuf=my_ray.xintercept+((xstep*pwallposnorm)>>6);
                                if((xintbuf>>16)!=(my_ray.xintercept>>16))
                                    goto passhoriz;

                                my_ray.yintercept=(my_ray.ytile<<TILESHIFT)+TILEGLOBAL-(pwallposinv<<10);
                                my_ray.xintercept=xintbuf;
// vbt new
                                my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                my_ray.tilehit=pwalltile;
                                HitHorizWall(pixx,&my_ray);
                            }
                            else
                            {
                                xintbuf=my_ray.xintercept+((xstep*pwallposinv)>>6);
                                if((xintbuf>>16)!=(my_ray.xintercept>>16))
                                    goto passhoriz;

                                my_ray.yintercept=(my_ray.ytile<<TILESHIFT)-(pwallposinv<<10);
                                my_ray.xintercept=xintbuf;
// vbt new
                                my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                my_ray.tilehit=pwalltile;
                                HitHorizWall(pixx,&my_ray);
                            }
                        }
                        else
                        {
                            int pwallposi = pwallpos;
                            if(pwalldir==di_west) pwallposi = 64-pwallpos;
                            if(pwalldir==di_east && (word)my_ray.xintercept<(pwallposi<<10)
                                    || pwalldir==di_west && (word)my_ray.xintercept>(pwallposi<<10))
                            {
                                if(((uint32_t)my_ray.xintercept>>16)==pwallx && my_ray.ytile==pwally)
                                {
                                    if(pwalldir==di_east && (int32_t)((word)my_ray.xintercept)+xstep<(pwallposi<<10)
                                            || pwalldir==di_west && (int32_t)((word)my_ray.xintercept)+xstep>(pwallposi<<10))
                                        goto passhoriz;

                                    if(pwalldir==di_east)
                                        my_ray.xintercept=(my_ray.xintercept&0xffff0000)+(pwallposi<<10);
                                    else
                                        my_ray.xintercept=(my_ray.xintercept&0xffff0000)-TILEGLOBAL+(pwallposi<<10);
                                    my_ray.yintercept=my_ray.yintercept-((ystep*(64-pwallpos))>>6);
// vbt new
                                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitVertWall(pixx,&my_ray);
                                }
                                else
                                {
                                    my_ray.texdelta = -(pwallposi<<10);
                                    my_ray.yintercept=my_ray.ytile<<TILESHIFT;
// vbt new
                                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitHorizWall(pixx,&my_ray);
                                }
                            }
                            else
                            {
                                if(((uint32_t)my_ray.xintercept>>16)==pwallx && my_ray.ytile==pwally)
                                {
                                    my_ray.texdelta = -(pwallposi<<10);
                                    my_ray.yintercept=my_ray.ytile<<TILESHIFT;
// vbt new
                                    my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitHorizWall(pixx,&my_ray);
                                }
                                else
                                {
                                    if(pwalldir==di_east && (int32_t)((word)my_ray.xintercept)+xstep>(pwallposi<<10)
                                            || pwalldir==di_west && (int32_t)((word)my_ray.xintercept)+xstep<(pwallposi<<10))
                                        goto passhoriz;

                                    if(pwalldir==di_east)
                                        my_ray.xintercept=(my_ray.xintercept&0xffff0000)-((64-pwallpos)<<10);
                                    else
                                        my_ray.xintercept=(my_ray.xintercept&0xffff0000)+((64-pwallpos)<<10);
                                    my_ray.yintercept=my_ray.yintercept-((ystep*pwallpos)>>6);
// vbt new
                                    my_ray.ytile = (short) (my_ray.yintercept >> TILESHIFT);
                                    my_ray.tilehit=pwalltile;
                                    HitVertWall(pixx,&my_ray);
                                }
                            }
                        }
                    }
                    else
                    {
                        my_ray.yintercept=my_ray.ytile<<TILESHIFT;
// vbt new
                        my_ray.xtile = (short) (my_ray.xintercept >> TILESHIFT);
                        HitHorizWall(pixx,&my_ray);
                    }
                }
                break;
            }
passhoriz:
            *((byte *)spotvis+yspot)=1;
            my_ray.ytile+=my_ray.ytilestep;
            my_ray.xintercept+=xstep;
            yspot=(word)((((uint32_t)my_ray.xintercept>>16)<<mapshift)+my_ray.ytile);
        }
        while(1);
    }
	
	ScalePost(my_ray.postx,my_ray.postsource);                   // no more optimization on last post
}

/*
====================
=
= WallRefresh
=
====================
*/

inline void WallRefresh (void)
{
	slCashPurge();
    min_wallheight = viewheight;
//	slSlaveFunc(AsmRefreshSlave,(void*)NULL);
//	SPR_RunSlaveSH(AsmRefreshSlave,NULL);

    AsmRefresh ();
/*		
	if((*(volatile Uint8 *)0xfffffe11 & 0x80) != 0x80)
	{
		SPR_WaitEndSlaveSH();
	}	*/	
}

void CalcViewVariables()
{
    int viewangle = player->angle;
    midangle = viewangle*(FINEANGLES/ANGLES);
    viewsin = sintable[viewangle];
    viewcos = costable[viewangle];
    viewx = player->x - FixedMul(focallength,viewcos);
    viewy = player->y + FixedMul(focallength,viewsin);

    focaltx = (short)(viewx>>TILESHIFT);
    focalty = (short)(viewy>>TILESHIFT);

    viewtx = (short)(player->x >> TILESHIFT);
    viewty = (short)(player->y >> TILESHIFT);
}

//==========================================================================

/*
========================
=
= ThreeDRefresh
=
========================
*/

void    ThreeDRefresh (void)
{
//
// clear out the traced array
//
    memset4_fast(spotvis,0,maparea);
    spotvis[player->tilex][player->tiley] = 1;       // Detect all sprites over player fix

    vbuf = VL_LockSurface(screenBuffer);
    if(vbuf == NULL) return;

    vbuf += screenofs;

    CalcViewVariables();

//
// follow the walls from there to the right, drawing as we go
//
    VGAClearScreen ();
#if defined(USE_FEATUREFLAGS) && defined(USE_STARSKY)
    if(GetFeatureFlags() & FF_STARSKY)
        DrawStarSky(vbuf, vbufPitch);
#endif
    WallRefresh ();
#if defined(USE_FEATUREFLAGS) && defined(USE_PARALLAX)
    if(GetFeatureFlags() & FF_PARALLAXSKY)
        DrawParallax(vbuf, vbufPitch);
#endif
#if defined(USE_FEATUREFLAGS) && defined(USE_CLOUDSKY)
    if(GetFeatureFlags() & FF_CLOUDSKY)
        DrawClouds(vbuf, vbufPitch, min_wallheight);
#endif
#ifdef USE_FLOORCEILINGTEX
    DrawFloorAndCeiling(vbuf, vbufPitch, min_wallheight);
#endif
//
// draw all the scaled images
//
    DrawScaleds();                  // draw scaled stuff
#if defined(USE_FEATUREFLAGS) && defined(USE_RAIN)
    if(GetFeatureFlags() & FF_RAIN)
        DrawRain(vbuf, vbufPitch);
#endif
#if defined(USE_FEATUREFLAGS) && defined(USE_SNOW)
    if(GetFeatureFlags() & FF_SNOW)
        DrawSnow(vbuf, vbufPitch);
#endif

    DrawPlayerWeapon ();    // draw player's hands

    if(Keyboard[sc_Tab] && viewsize == 21 && gamestate.weapon != -1)
        ShowActStatus();

    VL_UnlockSurface(screenBuffer);
    vbuf = NULL;

//
// show screen and time last cycle
//

    if (fizzlein)
    {
        FizzleFade(screenBuffer, screen, 0, 0,
            screenWidth, screenHeight, 20, false);
        fizzlein = false;

        lasttimecount = GetTimeCount();          // don't make a big tic count
    }
    else
    {
#ifndef REMDEBUG
        if (fpscounter)
        {
			char buffer[8];
			slPrint((char*)"fps   ",slLocate(1,0));
			slPrint((char*)ltoa(fps,buffer,8),slLocate(4,0));
			
            //fontnumber = 0;
            //SETFONTCOLOR(7,127);
            //PrintX=4; PrintY=1;
            //VWB_Bar(0,0,50,10,bordercol);
            //US_PrintSigned(fps);
            //US_Print(" fps");
        }
#endif
        SDL_BlitSurface(screenBuffer, NULL, screen, NULL);
        SDL_UpdateRect(screen, 0, 0, 0, 0);
    }
#ifndef REMDEBUG
    if (fpscounter)
    {
        fps_frames++;
        fps_time+=tics;

        if(fps_time>35)
        {
            fps_time-=35;
            fps=fps_frames<<1;
            fps_frames=0;
//			slSynch();			// vbt test
        }
    }
#endif
}
