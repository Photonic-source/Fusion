//------------------------------------- MD2/MD3 LIBRARY BEGINS -------------------------------------

#ifdef POLYMOST

#include "compat.h"
#include "build.h"
#include "glbuild.h"
#include "pragmas.h"
#include "baselayer.h"
#include "engine_priv.h"
#include "hightile.h"
#include "polymost.h"
#include "mdsprite.h"
#include "cache1d.h"
#include "kplib.h"
#include "md4.h"

voxmodel *voxmodels[MAXVOXELS];
int curextra=MAXTILES;

int addtileP(int model,int tile,int pallet)
{
    UNREFERENCED_PARAMETER(model);
    if (curextra==MAXTILES+EXTRATILES-2)return curextra;
    if (tile2model[tile].modelid==-1) {tile2model[tile].pal=pallet;return tile;}
    if (tile2model[tile].pal==pallet)return tile;
    while (tile2model[tile].next!=-1)
    {
        tile=tile2model[tile].next;
        if (tile2model[tile].pal==pallet)return tile;
    }
    tile2model[tile].next=curextra;
    tile2model[curextra].pal=pallet;
    return curextra++;
}
int Ptile2tile(int tile,int pallet)
{
    int t=tile;
//  if(tile>=1550&&tile<=1589){initprintf("(%d, %d)\n",tile,pallet);pallet=0;}
    while ((tile=tile2model[tile].next)!=-1)
        if (tile2model[tile].pal==pallet)
            return tile;
    return t;
}

int mdinited=0;
int mdpause=0;

#define MODELALLOCGROUP 256
int nummodelsalloced = 0, nextmodelid = 0;

static int maxmodelverts = 0, allocmodelverts = 0;
static int maxmodeltris = 0, allocmodeltris = 0;
static point3d *vertlist = NULL; //temp array to store interpolated vertices for drawing

static int allocvbos = 0, curvbo = 0;
static GLuint* vertvbos = NULL;
static GLuint* indexvbos = NULL;

mdmodel *mdload(const char *);
int mddraw(spritetype *);
void mdfree(mdmodel *);
int globalnoeffect=0;

extern int timerticspersec;

void freeallmodels()
{
    int i;

    if (models)
    {
        for (i=0;i<nextmodelid;i++) mdfree(models[i]);
        free(models); models = NULL;
        nummodelsalloced = 0;
        nextmodelid = 0;
    }

    memset(tile2model,-1,sizeof(tile2model));
    curextra=MAXTILES;

    if (vertlist)
    {
        free(vertlist);
        vertlist = NULL;
        allocmodelverts = maxmodelverts = 0;
        allocmodeltris = maxmodeltris = 0;
    }

    if (allocvbos)
    {
        bglDeleteBuffersARB(allocvbos, indexvbos);
        bglDeleteBuffersARB(allocvbos, vertvbos);
        free(indexvbos);
        free(vertvbos);
        allocvbos = 0;
    }
}

void clearskins()
{
    mdmodel *m;
    int i, j;

    for (i=0;i<nextmodelid;i++)
    {
        m = models[i];
        if (m->mdnum == 1)
        {
            voxmodel *v = (voxmodel*)m;
            for (j=0;j<MAXPALOOKUPS;j++)
            {
                if (v->texid[j]) bglDeleteTextures(1,(GLuint*)&v->texid[j]);
                v->texid[j] = 0;
            }
        }
        else if (m->mdnum == 2 || m->mdnum == 3)
        {
            md2model *m2 = (md2model*)m;
            mdskinmap_t *sk;
            for (j=0;j<m2->numskins*(HICEFFECTMASK+1);j++)
            {
                if (m2->texid[j]) bglDeleteTextures(1,(GLuint*)&m2->texid[j]);
                m2->texid[j] = 0;
            }

            for (sk=m2->skinmap;sk;sk=sk->next)
                for (j=0;j<(HICEFFECTMASK+1);j++)
                {
                    if (sk->texid[j]) bglDeleteTextures(1,(GLuint*)&sk->texid[j]);
                    sk->texid[j] = 0;
                }
        }
    }

    for (i=0;i<MAXVOXELS;i++)
    {
        voxmodel *v = (voxmodel*)voxmodels[i]; if (!v) continue;
        for (j=0;j<MAXPALOOKUPS;j++)
        {
            if (v->texid[j]) bglDeleteTextures(1,(GLuint*)&v->texid[j]);
            v->texid[j] = 0;
        }
    }
}

void mdinit()
{
    memset(hudmem,0,sizeof(hudmem));
    freeallmodels();
    mdinited = 1;
}

int md_loadmodel(const char *fn)
{
    mdmodel *vm, **ml;

    if (!mdinited) mdinit();

    if (nextmodelid >= nummodelsalloced)
    {
        ml = (mdmodel **)realloc(models,(nummodelsalloced+MODELALLOCGROUP)*sizeof(void*)); if (!ml) return(-1);
        models = ml; nummodelsalloced += MODELALLOCGROUP;
    }

    vm = mdload(fn); if (!vm) return(-1);
    models[nextmodelid++] = vm;
    return(nextmodelid-1);
}

int md_setmisc(int modelid, float scale, int shadeoff, float zadd, int flags)
{
    mdmodel *m;

    if (!mdinited) mdinit();

    if ((unsigned int)modelid >= (unsigned int)nextmodelid) return -1;
    m = models[modelid];
    m->bscale = scale;
    m->shadeoff = shadeoff;
    m->zadd = zadd;
    m->flags = flags;

    return 0;
}

int md_tilehasmodel(int tilenume,int pal)
{
    if (!mdinited) return -1;
    return tile2model[Ptile2tile(tilenume,pal)].modelid;
}

static int framename2index(mdmodel *vm, const char *nam)
{
    int i = 0;

    switch (vm->mdnum)
    {
    case 2:
    {
        md2model *m = (md2model *)vm;
        md2frame_t *fr;
        for (i=0;i<m->numframes;i++)
        {
            fr = (md2frame_t *)&m->frames[i*m->framebytes];
            if (!Bstrcmp(fr->name, nam)) break;
        }
    }
    break;
    case 3:
    {
        md3model *m = (md3model *)vm;
        for (i=0;i<m->numframes;i++)
            if (!Bstrcmp(m->head.frames[i].nam,nam)) break;
    }
    break;
    }
    return(i);
}

int md_defineframe(int modelid, const char *framename, int tilenume, int skinnum, float smoothduration, int pal)
{
    md2model *m;
    int i;

    if (!mdinited) mdinit();

    if ((unsigned int)modelid >= (unsigned int)nextmodelid) return(-1);
    if ((unsigned int)tilenume >= (unsigned int)MAXTILES) return(-2);
    if (!framename) return(-3);

    tilenume=addtileP(modelid,tilenume,pal);
    m = (md2model *)models[modelid];
    if (m->mdnum == 1)
    {
        tile2model[tilenume].modelid = modelid;
        tile2model[tilenume].framenum = tile2model[tilenume].skinnum = 0;
        return 0;
    }

    i = framename2index((mdmodel*)m,framename);
    if (i == m->numframes) return(-3);   // frame name invalid

    tile2model[tilenume].modelid = modelid;
    tile2model[tilenume].framenum = i;
    tile2model[tilenume].skinnum = skinnum;
    tile2model[tilenume].smoothduration = smoothduration;

    return 0;
}

int md_defineanimation(int modelid, const char *framestart, const char *frameend, int fpssc, int flags)
{
    md2model *m;
    mdanim_t ma, *map;
    int i;

    if (!mdinited) mdinit();

    if ((unsigned int)modelid >= (unsigned int)nextmodelid) return(-1);

    memset(&ma, 0, sizeof(ma));
    m = (md2model *)models[modelid];
    if (m->mdnum < 2) return 0;

    //find index of start frame
    i = framename2index((mdmodel*)m,framestart);
    if (i == m->numframes) return -2;
    ma.startframe = i;

    //find index of finish frame which must trail start frame
    i = framename2index((mdmodel*)m,frameend);
    if (i == m->numframes) return -3;
    ma.endframe = i;

    ma.fpssc = fpssc;
    ma.flags = flags;

    map = (mdanim_t*)calloc(1,sizeof(mdanim_t));
    if (!map) return(-4);
    memcpy(map, &ma, sizeof(ma));

    map->next = m->animations;
    m->animations = map;

    return(0);
}

int md_defineskin(int modelid, const char *skinfn, int palnum, int skinnum, int surfnum, float param)
{
    mdskinmap_t *sk, *skl;
    md2model *m;

    if (!mdinited) mdinit();

    if ((unsigned int)modelid >= (unsigned int)nextmodelid) return -1;
    if (!skinfn) return -2;
    if ((unsigned)palnum >= (unsigned)MAXPALOOKUPS) return -3;

    m = (md2model *)models[modelid];
    if (m->mdnum < 2) return 0;
    if (m->mdnum == 2) surfnum = 0;

    skl = NULL;
    for (sk = m->skinmap; sk; skl = sk, sk = sk->next)
        if (sk->palette == (unsigned char)palnum && skinnum == sk->skinnum && surfnum == sk->surfnum) break;
    if (!sk)
    {
        sk = (mdskinmap_t *)calloc(1,sizeof(mdskinmap_t));
        if (!sk) return -4;

        if (!skl) m->skinmap = sk;
        else skl->next = sk;
    }
    else if (sk->fn) free(sk->fn);

    sk->palette = (unsigned char)palnum;
    sk->skinnum = skinnum;
    sk->surfnum = surfnum;
    sk->param = param;
    sk->fn = (char *)malloc(strlen(skinfn)+1);
    if (!sk->fn) return(-4);
    strcpy(sk->fn, skinfn);
    sk->palmap=0;

    return 0;
}

int md_definehud(int modelid, int tilex, double xadd, double yadd, double zadd, double angadd, int flags)
{
    if (!mdinited) mdinit();

    if ((unsigned int)modelid >= (unsigned int)nextmodelid) return -1;
    if ((unsigned int)tilex >= (unsigned int)MAXTILES) return -2;

    hudmem[(flags>>2)&1][tilex].xadd = xadd;
    hudmem[(flags>>2)&1][tilex].yadd = yadd;
    hudmem[(flags>>2)&1][tilex].zadd = zadd;
    hudmem[(flags>>2)&1][tilex].angadd = ((short)angadd)|2048;
    hudmem[(flags>>2)&1][tilex].flags = (short)flags;

    return 0;
}

int md_undefinetile(int tile)
{
    if (!mdinited) return 0;
    if ((unsigned)tile >= (unsigned)MAXTILES) return -1;

    tile2model[tile].modelid = -1;
    tile2model[tile].next=-1;
    return 0;
}

int md_undefinemodel(int modelid)
{
    int i;
    if (!mdinited) return 0;
    if ((unsigned int)modelid >= (unsigned int)nextmodelid) return -1;

    for (i=MAXTILES+EXTRATILES-1; i>=0; i--)
        if (tile2model[i].modelid == modelid)
            tile2model[i].modelid = -1;

    if (models)
    {
        mdfree(models[modelid]);
        models[modelid] = NULL;
    }

    return 0;
}

md2model *modelhead;
mdskinmap_t *skhead;

typedef struct
{
    int pal,pal1,pal2;
} palmaptr;
palmaptr palconv[MAXPALCONV];

void clearconv()
{
    Bmemset(palconv,0,sizeof(palconv));
}
void setpalconv(int pal,int pal1,int pal2)
{
    int i;
    for (i=0;i<MAXPALCONV;i++)
        if (!palconv[i].pal)
        {
            palconv[i].pal =pal;
            palconv[i].pal1=pal1;
            palconv[i].pal2=pal2;return;
        }
        else
            if (palconv[i].pal==pal&&palconv[i].pal1==pal1)
            {
                palconv[i].pal2=pal2;return;
            }
}


void getpalmap(int *i,int *pal1,int *pal2)
{
    for (;*i<MAXPALCONV&&palconv[*i].pal1;(*i)++)
        if (palconv[*i].pal==*pal2)
        {
            *pal1=palconv[*i].pal1;
            *pal2=palconv[*i].pal2;
            return;
        }
}

int checkpalmaps(int pal)
{
    int stage,val=0;

    for (stage=0;stage<MAXPALCONV;stage++)
    {
        int pal1=0,pal2=pal;
        getpalmap(&stage,&pal1,&pal2);
        if (!pal)break;
        if (pal1)val|=1<<(pal1-SPECPAL);
    }
    return val;
}

void applypalmap(char *pic, char *palmap, int size, int pal)
{
    int r=0,g=1,b=2;
    pal+=200;

    //_initprintf("  %d #%d\n",pal,palmap);
    while (size--)
    {
        char a=palmap[b+1];
        if (glinfo.bgra)swapchar(&pic[r], &pic[b]);
        pic[r]=((pic[r]*(255-a)+hictinting[pal].r*a)*palmap[r])/255/255;
        pic[g]=((pic[g]*(255-a)+hictinting[pal].g*a)*palmap[g])/255/255;
        pic[b]=((pic[b]*(255-a)+hictinting[pal].b*a)*palmap[b])/255/255;

        /*
        		pic[r]=((255*(255-a)+hictinting[pal].r*a)*palmap[r])/255/255;
        		pic[g]=((255*(255-a)+hictinting[pal].g*a)*palmap[g])/255/255;
        		pic[b]=((255*(255-a)+hictinting[pal].b*a)*palmap[b])/255/255;
        */
        if (glinfo.bgra)swapchar(&pic[r], &pic[b]);
        r+=4;g+=4;b+=4;
    }
}

static void applypalmapSkin(char *pic, int sizx, int sizy, md2model *m, int number, int pal, int surf)
{
    int stage;

    //_initprintf("%d(%dx%d)\n",pal,sizx,sizy);
    for (stage=0;stage<MAXPALCONV;stage++)
    {
        int pal1=0,pal2=pal;
        mdskinmap_t *sk=modelhead->skinmap;
        getpalmap(&stage,&pal1,&pal2);
        if (!pal1)return;

        mdloadskin((md2model *)m,number,pal1,surf);
        for (; sk; sk = sk->next)
            if ((int)sk->palette == pal1&&sk->palmap)break;
        if (!sk||sk->size!=sizx*sizy)continue;

        applypalmap(pic,sk->palmap,sk->size,pal2);
    }
}

static int daskinloader(int filh, intptr_t *fptr, int *bpl, int *sizx, int *sizy, int *osizx, int *osizy, char *hasalpha, int pal, char effect, md2model *m, int number, int surf)
{
    int picfillen, j,y,x;
    char *picfil,*cptr,al=255;
    coltype *pic;
    int xsiz, ysiz, tsizx, tsizy;
    int r, g, b;

    picfillen = kfilelength(filh);
    picfil = (char *)malloc(picfillen+1); if (!picfil) { return -1; }
    kread(filh, picfil, picfillen);

    // tsizx/y = replacement texture's natural size
    // xsiz/y = 2^x size of replacement

    kpgetdim(picfil,picfillen,&tsizx,&tsizy);
    if (tsizx == 0 || tsizy == 0) { free(picfil); return -1; }

    if (!glinfo.texnpot)
    {
        for (xsiz=1;xsiz<tsizx;xsiz+=xsiz);
        for (ysiz=1;ysiz<tsizy;ysiz+=ysiz);
    }
    else
    {
        xsiz = tsizx;
        ysiz = tsizy;
    }
    *osizx = tsizx; *osizy = tsizy;
    pic = (coltype *)malloc(xsiz*ysiz*sizeof(coltype));
    if (!pic) { free(picfil); return -1; }
    memset(pic,0,xsiz*ysiz*sizeof(coltype));

    if (kprender(picfil,picfillen,(intptr_t)pic,xsiz*sizeof(coltype),xsiz,ysiz,0,0))
        { free(picfil); free(pic); return -1; }
    free(picfil);

    applypalmapSkin((char *)pic,tsizx,tsizy,m,number,pal,surf);
    cptr = &britable[gammabrightness ? 0 : curbrightness][0];
    r=(glinfo.bgra)?hictinting[pal].b:hictinting[pal].r;
    g=hictinting[pal].g;
    b=(glinfo.bgra)?hictinting[pal].r:hictinting[pal].b;
    for (y=0,j=0;y<tsizy;y++,j+=xsiz)
    {
        coltype *rpptr = &pic[j], tcol;

        for (x=0;x<tsizx;x++)
        {
            tcol.b = cptr[rpptr[x].b];
            tcol.g = cptr[rpptr[x].g];
            tcol.r = cptr[rpptr[x].r];

            if (effect & 1)
            {
                // greyscale
                tcol.b = max(tcol.b, max(tcol.g, tcol.r));
                tcol.g = tcol.r = tcol.b;
            }
            if (effect & 2)
            {
                // invert
                tcol.b = 255-tcol.b;
                tcol.g = 255-tcol.g;
                tcol.r = 255-tcol.r;
            }
            if (effect & 4)
            {
                // colorize
                tcol.b = min((int)(tcol.b)*b/64,255);
                tcol.g = min((int)(tcol.g)*g/64,255);
                tcol.r = min((int)(tcol.r)*r/64,255);
            }

            rpptr[x].b = tcol.b;
            rpptr[x].g = tcol.g;
            rpptr[x].r = tcol.r;
            al &= rpptr[x].a;
        }
    }
    if (!glinfo.bgra)
    {
        for (j=xsiz*ysiz-1;j>=0;j--)
        {
            swapchar(&pic[j].r, &pic[j].b);
        }
    }

    *sizx = xsiz;
    *sizy = ysiz;
    *bpl = xsiz;
    *fptr = (intptr_t)pic;
    *hasalpha = (al != 255);
    return 0;
}

// JONOF'S COMPRESSED TEXTURE CACHE STUFF ---------------------------------------------------
int mdloadskin_trytexcache(char *fn, int len, int pal, char effect, texcacheheader *head)
{
    int fp;
    char cachefn[BMAX_PATH], *cp;
    unsigned char mdsum[16];

    if (!glinfo.texcompr || !glusetexcompr || !glusetexcache || !cacheindexptr || cachefilehandle < 0) return -1;
    if (!bglCompressedTexImage2DARB || !bglGetCompressedTexImageARB)
    {
        // lacking the necessary extensions to do this
        OSD_Printf("Warning: the GL driver lacks necessary functions to use caching\n");
        glusetexcache = 0;
        return -1;
    }

    md4once((unsigned char *)fn, strlen(fn), mdsum);
//    for (cp = cachefn, fp = 0; (*cp = TEXCACHEFILE[fp]); cp++,fp++);
//    *(cp++) = '/';
    cp = cachefn;
    for (fp = 0; fp < 16; phex(mdsum[fp++], cp), cp+=2);
    sprintf(cp, "-%x-%x%x", len, pal, effect);

//    fil = kopen4load(cachefn, 0);
//    if (fil < 0) return -1;

    {
        int offset = 0;
        int len = 0;
        int i;
/*
        texcacheindex *cacheindexptr = &firstcacheindex;

        do
        {
//            initprintf("checking %s against %s\n",cachefn,cacheindexptr->name);
            if (!Bstrcmp(cachefn,cacheindexptr->name))
            {
                offset = cacheindexptr->offset;
                len = cacheindexptr->len;
//                initprintf("got a match for %s offset %d\n",cachefn,offset);
                break;
            }
            cacheindexptr = cacheindexptr->next;
        }
        while (cacheindexptr->next);
        */
        i = HASH_find(&cacheH,cachefn);
        if (i != -1)
        {
            texcacheindex *cacheindexptr = cacheptrs[i];
            len = cacheindexptr->len;
            offset = cacheindexptr->offset;
//            initprintf("got a match for %s offset %d\n",cachefn,offset);
        }
        if (len == 0) return -1; // didn't find it
        Blseek(cachefilehandle, offset, BSEEK_SET);
    }

//    initprintf("Loading cached skin: %s\n", cachefn);

    if (Bread(cachefilehandle, head, sizeof(texcacheheader)) < (int)sizeof(texcacheheader)) goto failure;
    if (memcmp(head->magic, "PMST", 4)) goto failure;

    head->xdim = B_LITTLE32(head->xdim);
    head->ydim = B_LITTLE32(head->ydim);
    head->flags = B_LITTLE32(head->flags);
    head->quality = B_LITTLE32(head->quality);

    if (head->quality != r_downsize) goto failure;
    if ((head->flags & 4) && !glusetexcachecompression) goto failure;
    if (!(head->flags & 4) && glusetexcachecompression) goto failure;
    if (gltexmaxsize && (head->xdim > (1<<gltexmaxsize) || head->ydim > (1<<gltexmaxsize))) goto failure;
    if (!glinfo.texnpot && (head->flags & 1)) goto failure;

    return cachefilehandle;
failure:
//    kclose(fil);
    initprintf("cache miss\n");
    return -1;
}

static int mdloadskin_cached(int fil, texcacheheader *head, int *doalloc, GLuint *glpic, int *xsiz, int *ysiz, int pal)
{
    int level, r;
    texcachepicture pict;
    void *pic = NULL, *packbuf = NULL;
    void *midbuf = NULL;
    int alloclen=0;

    UNREFERENCED_PARAMETER(pal);

    if (*doalloc&1)
    {
        bglGenTextures(1,glpic);  //# of textures (make OpenGL allocate structure)
        *doalloc |= 2;	// prevents bglGenTextures being called again if we fail in here
    }
    bglBindTexture(GL_TEXTURE_2D,*glpic);

    bglGetError();

    // load the mipmaps
    for (level = 0; level==0 || (pict.xdim > 1 || pict.ydim > 1); level++)
    {
        r = Bread(fil, &pict, sizeof(texcachepicture));
        if (r < (int)sizeof(texcachepicture)) goto failure;

        pict.size = B_LITTLE32(pict.size);
        pict.format = B_LITTLE32(pict.format);
        pict.xdim = B_LITTLE32(pict.xdim);
        pict.ydim = B_LITTLE32(pict.ydim);
        pict.border = B_LITTLE32(pict.border);
        pict.depth = B_LITTLE32(pict.depth);

        if (level == 0) { *xsiz = pict.xdim; *ysiz = pict.ydim; }

        if (alloclen < pict.size)
        {
            void *picc = realloc(pic, pict.size);
            if (!picc) goto failure; else pic = picc;
            alloclen = pict.size;

            picc = realloc(packbuf, alloclen+16);
            if (!picc) goto failure; else packbuf = picc;

            picc = realloc(midbuf, pict.size);
            if (!picc) goto failure; else midbuf = picc;
        }

        if (dedxtfilter(fil, &pict, pic, midbuf, packbuf, (head->flags&4)==4)) goto failure;

        bglCompressedTexImage2DARB(GL_TEXTURE_2D,level,pict.format,pict.xdim,pict.ydim,pict.border,
                                   pict.size,pic);
        if (bglGetError() != GL_NO_ERROR) goto failure;
    }

    if (midbuf) free(midbuf);
    if (pic) free(pic);
    if (packbuf) free(packbuf);
    return 0;
failure:
    if (midbuf) free(midbuf);
    if (pic) free(pic);
    if (packbuf) free(packbuf);
    return -1;
}
// --------------------------------------------------- JONOF'S COMPRESSED TEXTURE CACHE STUFF

//Note: even though it says md2model, it works for both md2model&md3model
int mdloadskin(md2model *m, int number, int pal, int surf)
{
    int i,j, bpl, xsiz=0, ysiz=0, osizx, osizy, texfmt = GL_RGBA, intexfmt = GL_RGBA;
    intptr_t fptr=0;
    char *skinfile, hasalpha, fn[BMAX_PATH+65];
    GLuint *texidx = NULL;
    mdskinmap_t *sk, *skzero = NULL;
    int doalloc = 1, filh;
    int cachefil = -1, picfillen;
    texcacheheader cachead;

    modelhead=m; // for palmaps

    if (m->mdnum == 2) surf = 0;

    if ((unsigned)pal >= (unsigned)MAXPALOOKUPS) return 0;
    i = -1;
    for (sk = m->skinmap; sk; sk = sk->next)
    {
        if ((int)sk->palette == pal && sk->skinnum == number && sk->surfnum == surf)
        {
            skinfile = sk->fn;
            texidx = &sk->texid[(globalnoeffect)?0:(hictinting[pal].f&HICEFFECTMASK)];
            strcpy(fn,skinfile);
            //OSD_Printf("Using exact match skin (pal=%d,skinnum=%d,surfnum=%d) %s\n",pal,number,surf,skinfile);
            break;
        }
        //If no match, give highest priority to number, then pal.. (Parkar's request, 02/27/2005)
        else if (((int)sk->palette ==   0) && (sk->skinnum == number) && (sk->surfnum == surf) && (i < 5)) { i = 5; skzero = sk; }
        else if (((int)sk->palette == pal) && (sk->skinnum ==      0) && (sk->surfnum == surf) && (i < 4)) { i = 4; skzero = sk; }
        else if (((int)sk->palette ==   0) && (sk->skinnum ==      0) && (sk->surfnum == surf) && (i < 3)) { i = 3; skzero = sk; }
        else if (((int)sk->palette ==   0) && (sk->skinnum == number) && (i < 2)) { i = 2; skzero = sk; }
        else if (((int)sk->palette == pal) && (sk->skinnum ==      0) && (i < 1)) { i = 1; skzero = sk; }
        else if (((int)sk->palette ==   0) && (sk->skinnum ==      0) && (i < 0)) { i = 0; skzero = sk; }
    }
    if (!sk)
    {
        if (pal >= (MAXPALOOKUPS - RESERVEDPALS))
            return (0);
        if (skzero)
        {
            skinfile = skzero->fn;
            texidx = &skzero->texid[(globalnoeffect)?0:(hictinting[pal].f&HICEFFECTMASK)];
            strcpy(fn,skinfile);
            //OSD_Printf("Using def skin 0,0 as fallback, pal=%d\n", pal);
        }
        else
            return 0;
    }
    skhead=sk; // for palmaps
    if (!skinfile[0]) return 0;

    if (*texidx) return *texidx;

    // possibly fetch an already loaded multitexture :_)
    if (pal >= (MAXPALOOKUPS - RESERVEDPALS))
        for (i=0;i<nextmodelid;i++)
            for (skzero = ((md2model *)models[i])->skinmap; skzero; skzero = skzero->next)
                if (!Bstrcasecmp(skzero->fn, sk->fn) && skzero->texid[(globalnoeffect)?0:(hictinting[pal].f&HICEFFECTMASK)])
                {
                    sk->texid[(globalnoeffect)?0:(hictinting[pal].f&HICEFFECTMASK)] = skzero->texid[(globalnoeffect)?0:(hictinting[pal].f&HICEFFECTMASK)];
                    return sk->texid[(globalnoeffect)?0:(hictinting[pal].f&HICEFFECTMASK)];
                }

    *texidx = 0;

    if ((filh = kopen4load(fn, 0)) < 0)
    {
        OSD_Printf("Skin %s not found.\n",fn);
        skinfile[0] = 0;
        return 0;
    }

    picfillen = kfilelength(filh);
    kclose(filh);	// FIXME: shouldn't have to do this. bug in cache1d.c

    cachefil = mdloadskin_trytexcache(fn, picfillen, pal<<8, (globalnoeffect)?0:(hictinting[pal].f&HICEFFECTMASK), &cachead);
    if (cachefil >= 0 && !mdloadskin_cached(cachefil, &cachead, &doalloc, texidx, &xsiz, &ysiz, pal))
    {
        osizx = cachead.xdim;
        osizy = cachead.ydim;
        hasalpha = (cachead.flags & 2) ? 1 : 0;
        if (pal < (MAXPALOOKUPS - RESERVEDPALS))m->usesalpha = hasalpha;
//        kclose(cachefil);
        //kclose(filh);	// FIXME: uncomment when cache1d.c is fixed
        // cachefil >= 0, so it won't be rewritten
    }
    else
    {
//        if (cachefil >= 0) kclose(cachefil);
        cachefil = -1;	// the compressed version will be saved to disk

        if ((filh = kopen4load(fn, 0)) < 0) return -1;
        if (daskinloader(filh,&fptr,&bpl,&xsiz,&ysiz,&osizx,&osizy,&hasalpha,pal,(globalnoeffect)?0:(hictinting[pal].f&HICEFFECTMASK),m,number,surf))
        {
            kclose(filh);
            OSD_Printf("Failed loading skin file \"%s\"\n", fn);
            skinfile[0] = 0;
            return(0);
        }
        else kclose(filh);
        if (pal < (MAXPALOOKUPS - RESERVEDPALS))m->usesalpha = hasalpha;
        if (pal>=SPECPAL&&pal<=REDPAL)
        {
            //_initprintf("%cLoaded palmap %d(%dx%d)",sk->palmap?'+':'-',pal,xsiz,ysiz);
            if (!sk->palmap)
            {
                sk->size=xsiz*ysiz;
                sk->palmap=malloc(sk->size*4);
                memcpy(sk->palmap,(char *)fptr,sk->size*4);
            }
            cachefil=0;
            //_initprintf("#%d\n",sk->palmap);
        }

        if ((doalloc&3)==1) bglGenTextures(1,(GLuint*)texidx);
        bglBindTexture(GL_TEXTURE_2D,*texidx);

        //gluBuild2DMipmaps(GL_TEXTURE_2D,GL_RGBA,xsiz,ysiz,GL_BGRA_EXT,GL_UNSIGNED_BYTE,(char *)fptr);
        if (glinfo.texcompr && glusetexcompr) intexfmt = hasalpha ? GL_COMPRESSED_RGBA_ARB : GL_COMPRESSED_RGB_ARB;
        else if (!hasalpha) intexfmt = GL_RGB;
        if (glinfo.bgra) texfmt = GL_BGRA;
        uploadtexture((doalloc&1), xsiz, ysiz, intexfmt, texfmt, (coltype*)fptr, xsiz, ysiz, 0|8192);
        free((void*)fptr);
    }

    if (!m->skinloaded)
    {
        if (xsiz != osizx || ysiz != osizy)
        {
            float fx, fy;
            fx = ((float)osizx)/((float)xsiz);
            fy = ((float)osizy)/((float)ysiz);
            if (m->mdnum == 2)
            {
                int *lptr;
                for (lptr=m->glcmds;(i=*lptr++);)
                    for (i=labs(i);i>0;i--,lptr+=3)
                    {
                        ((float *)lptr)[0] *= fx;
                        ((float *)lptr)[1] *= fy;
                    }
            }
            else if (m->mdnum == 3)
            {
                md3model *m3 = (md3model *)m;
                md3surf_t *s;
                int surfi;
                for (surfi=0;surfi<m3->head.numsurfs;surfi++)
                {
                    s = &m3->head.surfs[surfi];
                    for (i=s->numverts-1;i>=0;i--)
                    {
                        s->uv[i].u *= fx;
                        s->uv[i].v *= fy;
                    }
                }
            }
        }
        m->skinloaded = 1+number;
    }

    bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,glfiltermodes[gltexfiltermode].mag);
    bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,glfiltermodes[gltexfiltermode].min);
    if (glinfo.maxanisotropy > 1.0)
        bglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,glanisotropy);
    bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);

    if (glinfo.texcompr && glusetexcompr && glusetexcache)
        if (cachefil < 0)
        {
            // save off the compressed version
            cachead.quality = r_downsize;
            cachead.xdim = osizx>>cachead.quality;
            cachead.ydim = osizy>>cachead.quality;

            i = 0;
            for (j=0;j<31;j++)
            {
                if (xsiz == pow2long[j]) { i |= 1; }
                if (ysiz == pow2long[j]) { i |= 2; }
            }
            cachead.flags = (i!=3) | (hasalpha ? 2 : 0);
            OSD_Printf("No cached tex for %s.\n",fn);
            writexcache(fn, picfillen, pal<<8, (globalnoeffect)?0:(hictinting[pal].f&HICEFFECTMASK), &cachead);
        }

    return(*texidx);
}

//Note: even though it says md2model, it works for both md2model&md3model
void updateanimation(md2model *m, spritetype *tspr)
{
    mdanim_t *anim;
    int i, j, k;
    int fps;
    char lpal = (tspr->owner >= MAXSPRITES) ? tspr->pal : sprite[tspr->owner].pal;

    if (m->numframes < 2) 
    {
        m->interpol = 0;
        return;
    }

    m->cframe = m->nframe = tile2model[Ptile2tile(tspr->picnum,lpal)].framenum;

    for (anim = m->animations;
            anim && anim->startframe != m->cframe;
            anim = anim->next) ;
    if (!anim)
    {
        if (r_animsmoothing && (tile2model[Ptile2tile(tspr->picnum,lpal)].smoothduration != 0) && (spritesmooth[tspr->owner].mdoldframe != m->cframe))
        {
            if (spritesmooth[tspr->owner].mdsmooth == 0)
            {
                spriteext[tspr->owner].mdanimtims = mdtims;
                m->interpol = 0;
                spritesmooth[tspr->owner].mdsmooth = 1;
                spritesmooth[tspr->owner].mdcurframe = m->cframe;
            }
            if (r_animsmoothing && (tile2model[Ptile2tile(tspr->picnum,lpal)].smoothduration != 0) && (spritesmooth[tspr->owner].mdcurframe != m->cframe))
            {
                spriteext[tspr->owner].mdanimtims = mdtims;
                m->interpol = 0;
                spritesmooth[tspr->owner].mdsmooth = 1;
                spritesmooth[tspr->owner].mdoldframe = spritesmooth[tspr->owner].mdcurframe;
                spritesmooth[tspr->owner].mdcurframe = m->cframe;
            }
        }
        else if (r_animsmoothing && (tile2model[Ptile2tile(tspr->picnum,lpal)].smoothduration != 0) && (spritesmooth[tspr->owner].mdcurframe != m->cframe))
        {
            spriteext[tspr->owner].mdanimtims = mdtims;
            m->interpol = 0;
            spritesmooth[tspr->owner].mdsmooth = 1;
            spritesmooth[tspr->owner].mdoldframe = spritesmooth[tspr->owner].mdcurframe;
            spritesmooth[tspr->owner].mdcurframe = m->cframe;
        }
        else
        {
            m->interpol = 0;
            return;
        }
    }

    if (anim && (((int)spriteext[tspr->owner].mdanimcur) != anim->startframe))
    {
        //if (spriteext[tspr->owner].flags & SPREXT_NOMDANIM) OSD_Printf("SPREXT_NOMDANIM\n");
        //OSD_Printf("smooth launched ! oldanim %i new anim %i\n", spriteext[tspr->owner].mdanimcur, anim->startframe);
        spriteext[tspr->owner].mdanimcur = (short)anim->startframe;
        spriteext[tspr->owner].mdanimtims = mdtims;
        m->interpol = 0;
        if (!r_animsmoothing || (tile2model[Ptile2tile(tspr->picnum,lpal)].smoothduration == 0))
        {
            m->cframe = m->nframe = anim->startframe;
            return;
        }
        m->nframe = anim->startframe;
        m->cframe = spritesmooth[tspr->owner].mdoldframe;
        spritesmooth[tspr->owner].mdsmooth = 1;
        return;
    }

    if (spritesmooth[tspr->owner].mdsmooth)
        fps = (1.0f / (float)(tile2model[Ptile2tile(tspr->picnum,lpal)].smoothduration)) * 66;
    else
        fps = anim->fpssc;

    i = (mdtims-spriteext[tspr->owner].mdanimtims)*((fps*timerticspersec)/120);

    if (spritesmooth[tspr->owner].mdsmooth)
        j = 65536;
    else
        j = ((anim->endframe+1-anim->startframe)<<16);
    //Just in case you play the game for a VERY int time...
    if (i < 0) { i = 0;spriteext[tspr->owner].mdanimtims = mdtims; }
    //compare with j*2 instead of j to ensure i stays > j-65536 for MDANIM_ONESHOT
    if ((anim) && (i >= j+j) && (fps) && !mdpause) //Keep mdanimtims close to mdtims to avoid the use of MOD
        spriteext[tspr->owner].mdanimtims += j/((fps*timerticspersec)/120);

    k = i;

    if (anim && (anim->flags&MDANIM_ONESHOT))
        { if (i > j-65536) i = j-65536; }
    else { if (i >= j) { i -= j; if (i >= j) i %= j; } }

    if (r_animsmoothing && spritesmooth[tspr->owner].mdsmooth)
    {
        m->nframe = (anim) ? anim->startframe : spritesmooth[tspr->owner].mdcurframe;
        m->cframe = spritesmooth[tspr->owner].mdoldframe;
        //OSD_Printf("smoothing... cframe %i nframe %i\n", m->cframe, m->nframe);
        if (k > 65535)
        {
            spriteext[tspr->owner].mdanimtims = mdtims;
            m->interpol = 0;
            spritesmooth[tspr->owner].mdsmooth = 0;
            m->cframe = m->nframe = (anim) ? anim->startframe : spritesmooth[tspr->owner].mdcurframe;
            spritesmooth[tspr->owner].mdoldframe = m->cframe;
            //OSD_Printf("smooth stopped !\n");
            return;
        }
    }
    else
    {
        m->cframe = (i>>16)+anim->startframe;
        m->nframe = m->cframe+1; if (m->nframe > anim->endframe) m->nframe = anim->startframe;
        spritesmooth[tspr->owner].mdoldframe = m->cframe;
        //OSD_Printf("not smoothing... cframe %i nframe %i\n", m->cframe, m->nframe);
    }
    m->interpol = ((float)(i&65535))/65536.f;
    //OSD_Printf("interpol %f\n", m->interpol);
}

// VBO generation and allocation
static void mdloadvbos(md3model *m)
{
    int     i;

    m->vbos = malloc(m->head.numsurfs * sizeof(GLuint));
    bglGenBuffersARB(m->head.numsurfs, m->vbos);

    i = 0;
    while (i < m->head.numsurfs)
    {
        bglBindBufferARB(GL_ARRAY_BUFFER_ARB, m->vbos[i]);
        bglBufferDataARB(GL_ARRAY_BUFFER_ARB, m->head.surfs[i].numverts * sizeof(md3uv_t), m->head.surfs[i].uv, GL_STATIC_DRAW_ARB);
        i++;
    }
    bglBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
}

//--------------------------------------- MD2 LIBRARY BEGINS ---------------------------------------
static md2model *md2load(int fil, const char *filnam)
{
    md2model *m;
    md3model *m3;
    md3surf_t *s;
    md2frame_t *f;
    md2head_t head;
    char st[BMAX_PATH];
    int i, j, k;

    m = (md2model *)calloc(1,sizeof(md2model)); if (!m) return(0);
    m->mdnum = 2; m->scale = .01f;

    kread(fil,(char *)&head,sizeof(md2head_t));
    head.id = B_LITTLE32(head.id);                 head.vers = B_LITTLE32(head.vers);
    head.skinxsiz = B_LITTLE32(head.skinxsiz);     head.skinysiz = B_LITTLE32(head.skinysiz);
    head.framebytes = B_LITTLE32(head.framebytes); head.numskins = B_LITTLE32(head.numskins);
    head.numverts = B_LITTLE32(head.numverts);     head.numuv = B_LITTLE32(head.numuv);
    head.numtris = B_LITTLE32(head.numtris);       head.numglcmds = B_LITTLE32(head.numglcmds);
    head.numframes = B_LITTLE32(head.numframes);   head.ofsskins = B_LITTLE32(head.ofsskins);
    head.ofsuv = B_LITTLE32(head.ofsuv);           head.ofstris = B_LITTLE32(head.ofstris);
    head.ofsframes = B_LITTLE32(head.ofsframes);   head.ofsglcmds = B_LITTLE32(head.ofsglcmds);
    head.ofseof = B_LITTLE32(head.ofseof);

    if ((head.id != 0x32504449) || (head.vers != 8)) { free(m); return(0); } //"IDP2"

    m->numskins = head.numskins;
    m->numframes = head.numframes;
    m->numverts = head.numverts;
    m->numglcmds = head.numglcmds;
    m->framebytes = head.framebytes;

    m->frames = (char *)calloc(m->numframes,m->framebytes); if (!m->frames) { free(m); return(0); }
    m->glcmds = (int *)calloc(m->numglcmds,sizeof(int)); if (!m->glcmds) { free(m->frames); free(m); return(0); }
    m->tris = (md2tri_t *)calloc(head.numtris, sizeof(md2tri_t)); if (!m->tris) { free(m->glcmds); free(m->frames); free(m); return(0); }
    m->uv = (md2uv_t *)calloc(head.numuv, sizeof(md2uv_t)); if (!m->uv) { free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }

    klseek(fil,head.ofsframes,SEEK_SET);
    if (kread(fil,(char *)m->frames,m->numframes*m->framebytes) != m->numframes*m->framebytes)
        { free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }

    klseek(fil,head.ofsglcmds,SEEK_SET);
    if (kread(fil,(char *)m->glcmds,m->numglcmds*sizeof(int)) != (int)(m->numglcmds*sizeof(int)))
        { free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }

    klseek(fil,head.ofstris,SEEK_SET);
    if (kread(fil,(char *)m->tris,head.numtris*sizeof(md2tri_t)) != (int)(head.numtris*sizeof(md2tri_t)))
        { free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }

    klseek(fil,head.ofsuv,SEEK_SET);
    if (kread(fil,(char *)m->uv,head.numuv*sizeof(md2uv_t)) != (int)(head.numuv*sizeof(md2uv_t)))
        { free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }

#if B_BIG_ENDIAN != 0
    {
        char *f = (char *)m->frames;
        int *l,j;
        md2frame_t *fr;

        for (i = m->numframes-1; i>=0; i--)
        {
            fr = (md2frame_t *)f;
            l = (int *)&fr->mul;
            for (j=5;j>=0;j--) l[j] = B_LITTLE32(l[j]);
            f += m->framebytes;
        }

        for (i = m->numglcmds-1; i>=0; i--)
        {
            m->glcmds[i] = B_LITTLE32(m->glcmds[i]);
        }
    }
#endif

    strcpy(st,filnam);
    for (i=strlen(st)-1;i>0;i--)
        if ((st[i] == '/') || (st[i] == '\\')) { i++; break; }
    if (i<0) i=0;
    st[i] = 0;
    m->basepath = (char *)malloc(i+1); if (!m->basepath) { free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }
    strcpy(m->basepath, st);

    m->skinfn = (char *)calloc(m->numskins,64); if (!m->skinfn) { free(m->basepath); free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }
    klseek(fil,head.ofsskins,SEEK_SET);
    if (kread(fil,m->skinfn,64*m->numskins) != 64*m->numskins)
        { free(m->glcmds); free(m->frames); free(m); return(0); }

    m->texid = (GLuint *)calloc(m->numskins, sizeof(GLuint) * (HICEFFECTMASK+1));
    if (!m->texid) { free(m->skinfn); free(m->basepath); free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }

    maxmodelverts = max(maxmodelverts, m->numverts);
    maxmodeltris = max(maxmodeltris, head.numtris);

    //return(m);

    // the MD2 is now loaded internally - let's begin the MD3 conversion process
    //OSD_Printf("Beginning md3 conversion.\n");
    m3 = (md3model *)calloc(1, sizeof(md3model)); if (!m3) { free(m->skinfn); free(m->basepath); free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }
    m3->mdnum = 3; m3->texid = 0; m3->scale = m->scale;
    m3->head.id = 0x33504449; m3->head.vers = 15;
    // this changes the conversion code to do real MD2->MD3 conversion
    // it breaks HRP MD2 oozfilter, change the flags to 1337 to revert
    // to the old, working code
    m3->head.flags = 0;
    m3->head.numframes = m->numframes;
    m3->head.numtags = 0; m3->head.numsurfs = 1;
    m3->head.numskins = 0;

    m3->numskins = m3->head.numskins;
    m3->numframes = m3->head.numframes;

    m3->head.frames = (md3frame_t *)calloc(m3->head.numframes, sizeof(md3frame_t)); if (!m3->head.frames) { free(m3); free(m->skinfn); free(m->basepath); free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }
    m3->muladdframes = (point3d *)calloc(m->numframes * 2, sizeof(point3d));

    f = (md2frame_t *)(m->frames);

    // frames converting
    i = 0;
    while (i < m->numframes)
    {
        f = (md2frame_t *)&m->frames[i*m->framebytes];
        strcpy(m3->head.frames[i].nam, f->name);
        //OSD_Printf("Copied frame %s.\n", m3->head.frames[i].nam);
        m3->muladdframes[i*2] = f->mul;
        m3->muladdframes[i*2+1] = f->add;
        i++;
    }

    m3->head.tags = NULL;

    m3->head.surfs = (md3surf_t *)calloc(1, sizeof(md3surf_t)); if (!m3->head.surfs) { free(m3->head.frames); free(m3); free(m->skinfn); free(m->basepath); free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }
    s = m3->head.surfs;

    // model converting
    s->id = 0x33504449; s->flags = 0;
    s->numframes = m->numframes; s->numshaders = 0;
    s->numtris = head.numtris;
    s->numverts = head.numtris * 3; // oh man talk about memory effectiveness :((((
    // MD2 is actually more accurate than MD3 in term of uv-mapping, because each triangle has a triangle counterpart on the UV-map.
    // In MD3, each vertex unique UV coordinates, meaning that you have to duplicate vertices if you need non-seamless UV-mapping.

    maxmodelverts = max(maxmodelverts, s->numverts);

    strcpy(s->nam, "Dummy surface from MD2");

    s->shaders = NULL;

    s->tris = (md3tri_t *)calloc(head.numtris, sizeof(md3tri_t)); if (!s->tris) { free(s); free(m3->head.frames); free(m3); free(m->skinfn); free(m->basepath); free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }
    s->uv = (md3uv_t *)calloc(s->numverts, sizeof(md3uv_t)); if (!s->uv) { free(s->tris); free(s); free(m3->head.frames); free(m3); free(m->skinfn); free(m->basepath); free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }
    s->xyzn = (md3xyzn_t *)calloc(s->numverts * m->numframes, sizeof(md3xyzn_t)); if (!s->xyzn) { free(s->uv); free(s->tris); free(s); free(m3->head.frames); free(m3); free(m->skinfn); free(m->basepath); free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m); return(0); }

    //memoryusage += (s->numverts * m->numframes * sizeof(md3xyzn_t));
    //OSD_Printf("Current model geometry memory usage : %i.\n", memoryusage);

    //OSD_Printf("Number of frames : %i\n", m->numframes);
    //OSD_Printf("Number of triangles : %i\n", head.numtris);
    //OSD_Printf("Number of vertices : %i\n", s->numverts);

    // triangle converting
    i = 0;
    while (i < head.numtris)
    {
        j = 0;
        //OSD_Printf("Triangle : %i\n", i);
        while (j < 3)
        {
            // triangle vertex indexes
            s->tris[i].i[j] = i*3 + j;

            // uv coords
            s->uv[i*3+j].u = (float)(m->uv[m->tris[i].u[j]].u) / (float)(head.skinxsiz);
            s->uv[i*3+j].v = (float)(m->uv[m->tris[i].u[j]].v) / (float)(head.skinysiz);

            // vertices for each frame
            k = 0;
            while (k < m->numframes)
            {
                f = (md2frame_t *)&m->frames[k*m->framebytes];
                if (m3->head.flags == 1337)
                {
                    s->xyzn[(k*s->numverts) + (i*3) + j].x = f->verts[m->tris[i].v[j]].v[0];
                    s->xyzn[(k*s->numverts) + (i*3) + j].y = f->verts[m->tris[i].v[j]].v[1];
                    s->xyzn[(k*s->numverts) + (i*3) + j].z = f->verts[m->tris[i].v[j]].v[2];
                }
                else
                {
                    s->xyzn[(k*s->numverts) + (i*3) + j].x = ((f->verts[m->tris[i].v[j]].v[0] * f->mul.x) + f->add.x) * 64;
                    s->xyzn[(k*s->numverts) + (i*3) + j].y = ((f->verts[m->tris[i].v[j]].v[1] * f->mul.y) + f->add.y) * 64;
                    s->xyzn[(k*s->numverts) + (i*3) + j].z = ((f->verts[m->tris[i].v[j]].v[2] * f->mul.z) + f->add.z) * 64;
                }

                k++;
            }
            j++;
        }
        //OSD_Printf("End triangle.\n");
        i++;
    }
    //OSD_Printf("Finished md3 conversion.\n");

    {
        mdskinmap_t *sk;

        sk = (mdskinmap_t *)calloc(1,sizeof(mdskinmap_t));
        sk->palette = 0;
        sk->skinnum = 0;
        sk->surfnum = 0;

        if (m->numskins > 0)
        {
            sk->fn = (char *)malloc(strlen(m->basepath)+strlen(m->skinfn)+1);
            if (sk->palmap)
            {
                //_initprintf("Delete %s",m->skinfn);
                sk->palmap=0;sk->size=0;
            }
            strcpy(sk->fn, m->basepath);
            strcat(sk->fn, m->skinfn);
        }
        m3->skinmap = sk;
    }

    m3->indexes = malloc(sizeof(unsigned short) * s->numtris);
    m3->vindexes = malloc(sizeof(unsigned short) * s->numtris * 3);
    m3->maxdepths = malloc(sizeof(float) * s->numtris);

    m3->vbos = NULL;

    // die MD2 ! DIE !
    free(m->texid); free(m->skinfn); free(m->basepath); free(m->uv); free(m->tris); free(m->glcmds); free(m->frames); free(m);

    return((md2model *)m3);
}
//---------------------------------------- MD2 LIBRARY ENDS ----------------------------------------

// DICHOTOMIC RECURSIVE SORTING - USED BY MD3DRAW - MAY PUT IT IN ITS OWN SOURCE FILE LATER
int partition(unsigned short *indexes, float *depths, int f, int l)
{
    int up,down;
    float tempf;
    unsigned short tempus;
    float piv = depths[f];
    unsigned short piv2 = indexes[f];
    up = f;
    down = l;
    do
    {
        while ((depths[up] <= piv) && (up < l))
            up++;
        while ((depths[down] > piv)  && (down > f))
            down--;
        if (up < down)
        {
            tempf = depths[up];
            depths[up] = depths[down];
            depths[down] = tempf;
            tempus = indexes[up];
            indexes[up] = indexes[down];
            indexes[down] = tempus;
        }
    }
    while (down > up);
    depths[f] = depths[down];
    depths[down] = piv;
    indexes[f] = indexes[down];
    indexes[down] = piv2;
    return down;
}

void quicksort(unsigned short *indexes, float *depths, int first, int last)
{
    int pivIndex = 0;
    if (first < last)
    {
        pivIndex = partition(indexes,depths,first, last);
        quicksort(indexes,depths,first,(pivIndex-1));
        quicksort(indexes,depths,(pivIndex+1),last);
    }
}
// END OF QUICKSORT LIB

//--------------------------------------- MD3 LIBRARY BEGINS ---------------------------------------

static md3model *md3load(int fil)
{
    int i, surfi, ofsurf, offs[4], leng[4];
    int maxtrispersurf;
    md3model *m;
    md3surf_t *s;

    m = (md3model *)calloc(1,sizeof(md3model)); if (!m) return(0);
    m->mdnum = 3; m->texid = 0; m->scale = .01;

    m->muladdframes = NULL;

    kread(fil,&m->head,SIZEOF_MD3HEAD_T);
    m->head.id = B_LITTLE32(m->head.id);             m->head.vers = B_LITTLE32(m->head.vers);
    m->head.flags = B_LITTLE32(m->head.flags);       m->head.numframes = B_LITTLE32(m->head.numframes);
    m->head.numtags = B_LITTLE32(m->head.numtags);   m->head.numsurfs = B_LITTLE32(m->head.numsurfs);
    m->head.numskins = B_LITTLE32(m->head.numskins); m->head.ofsframes = B_LITTLE32(m->head.ofsframes);
    m->head.ofstags = B_LITTLE32(m->head.ofstags); m->head.ofssurfs = B_LITTLE32(m->head.ofssurfs);
    m->head.eof = B_LITTLE32(m->head.eof);

    if ((m->head.id != 0x33504449) && (m->head.vers != 15)) { free(m); return(0); } //"IDP3"

    m->numskins = m->head.numskins; //<- dead code?
    m->numframes = m->head.numframes;

    ofsurf = m->head.ofssurfs;

    klseek(fil,m->head.ofsframes,SEEK_SET); i = m->head.numframes*sizeof(md3frame_t);
    m->head.frames = (md3frame_t *)malloc(i); if (!m->head.frames) { free(m); return(0); }
    kread(fil,m->head.frames,i);

    if (m->head.numtags == 0) m->head.tags = NULL;
    else
    {
        klseek(fil,m->head.ofstags,SEEK_SET); i = m->head.numtags*sizeof(md3tag_t);
        m->head.tags = (md3tag_t *)malloc(i); if (!m->head.tags) { free(m->head.frames); free(m); return(0); }
        kread(fil,m->head.tags,i);
    }

    klseek(fil,m->head.ofssurfs,SEEK_SET); i = m->head.numsurfs*sizeof(md3surf_t);
    m->head.surfs = (md3surf_t *)malloc(i); if (!m->head.surfs) { if (m->head.tags) free(m->head.tags); free(m->head.frames); free(m); return(0); }

#if B_BIG_ENDIAN != 0
    {
        int *l;

        for (i = m->head.numframes-1; i>=0; i--)
        {
            l = (int *)&m->head.frames[i].min;
            for (j=3+3+3+1-1;j>=0;j--) l[j] = B_LITTLE32(l[j]);
        }

        for (i = m->head.numtags-1; i>=0; i--)
        {
            l = (int *)&m->head.tags[i].p;
            for (j=3+3+3+3-1;j>=0;j--) l[j] = B_LITTLE32(l[j]);
        }
    }
#endif

    maxtrispersurf = 0;

    for (surfi=0;surfi<m->head.numsurfs;surfi++)
    {
        s = &m->head.surfs[surfi];
        klseek(fil,ofsurf,SEEK_SET); kread(fil,s,SIZEOF_MD3SURF_T);

#if B_BIG_ENDIAN != 0
        {
            int *l;
            s->id = B_LITTLE32(s->id);
            l =	(int *)&s->flags;
            for	(j=1+1+1+1+1+1+1+1+1+1-1;j>=0;j--) l[j] = B_LITTLE32(l[j]);
        }
#endif

        offs[0] = ofsurf+s->ofstris; leng[0] = s->numtris*sizeof(md3tri_t);
        offs[1] = ofsurf+s->ofsshaders; leng[1] = s->numshaders*sizeof(md3shader_t);
        offs[2] = ofsurf+s->ofsuv; leng[2] = s->numverts*sizeof(md3uv_t);
        offs[3] = ofsurf+s->ofsxyzn; leng[3] = s->numframes*s->numverts*sizeof(md3xyzn_t);
        //memoryusage += (s->numverts * s->numframes * sizeof(md3xyzn_t));
        //OSD_Printf("Current model geometry memory usage : %i.\n", memoryusage);


        s->tris = (md3tri_t *)malloc(leng[0]+leng[1]+leng[2]+leng[3]);
        if (!s->tris)
        {
            for (surfi--;surfi>=0;surfi--) free(m->head.surfs[surfi].tris);
            if (m->head.tags) free(m->head.tags); free(m->head.frames); free(m); return(0);
        }
        s->shaders = (md3shader_t *)(((intptr_t)s->tris)+leng[0]);
        s->uv      = (md3uv_t     *)(((intptr_t)s->shaders)+leng[1]);
        s->xyzn    = (md3xyzn_t   *)(((intptr_t)s->uv)+leng[2]);

        klseek(fil,offs[0],SEEK_SET); kread(fil,s->tris   ,leng[0]);
        klseek(fil,offs[1],SEEK_SET); kread(fil,s->shaders,leng[1]);
        klseek(fil,offs[2],SEEK_SET); kread(fil,s->uv     ,leng[2]);
        klseek(fil,offs[3],SEEK_SET); kread(fil,s->xyzn   ,leng[3]);

#if B_BIG_ENDIAN != 0
        {
            int *l;

            for (i=s->numtris-1;i>=0;i--)
            {
                for (j=2;j>=0;j--) s->tris[i].i[j] = B_LITTLE32(s->tris[i].i[j]);
            }
            for (i=s->numshaders-1;i>=0;i--)
            {
                s->shaders[i].i = B_LITTLE32(s->shaders[i].i);
            }
            for (i=s->numverts-1;i>=0;i--)
            {
                l = (int*)&s->uv[i].u;
                l[0] = B_LITTLE32(l[0]);
                l[1] = B_LITTLE32(l[1]);
            }
            for (i=s->numframes*s->numverts-1;i>=0;i--)
            {
                s->xyzn[i].x = (signed short)B_LITTLE16((unsigned short)s->xyzn[i].x);
                s->xyzn[i].y = (signed short)B_LITTLE16((unsigned short)s->xyzn[i].y);
                s->xyzn[i].z = (signed short)B_LITTLE16((unsigned short)s->xyzn[i].z);
            }
        }
#endif
        maxmodelverts = max(maxmodelverts, s->numverts);
        maxmodeltris = max(maxmodeltris, s->numtris);
        maxtrispersurf = max(maxtrispersurf, s->numtris);
        ofsurf += s->ofsend;
    }

#if 0
    {
        char *buf, st[BMAX_PATH+2], bst[BMAX_PATH+2];
        int j, bsc;

        strcpy(st,filnam);
        for (i=0,j=0;st[i];i++) if ((st[i] == '/') || (st[i] == '\\')) j = i+1;
        st[j] = '*'; st[j+1] = 0;
        kzfindfilestart(st); bsc = -1;
        while (kzfindfile(st))
        {
            if (st[0] == '\\') continue;

            for (i=0,j=0;st[i];i++) if (st[i] == '.') j = i+1;
            if ((!stricmp(&st[j],"JPG")) || (!stricmp(&st[j],"PNG")) || (!stricmp(&st[j],"GIF")) ||
                    (!stricmp(&st[j],"PCX")) || (!stricmp(&st[j],"TGA")) || (!stricmp(&st[j],"BMP")) ||
                    (!stricmp(&st[j],"CEL")))
            {
                for (i=0;st[i];i++) if (st[i] != filnam[i]) break;
                if (i > bsc) { bsc = i; strcpy(bst,st); }
            }
        }
        if (!mdloadskin(&m->texid,&m->usesalpha,bst)) ;//bad!
    }
#endif

    m->indexes = malloc(sizeof(unsigned short) * maxtrispersurf);
    m->vindexes = malloc(sizeof(unsigned short) * maxtrispersurf * 3);
    m->maxdepths = malloc(sizeof(float) * maxtrispersurf);

    m->vbos = NULL;

    return(m);
}

static int md3draw(md3model *m, spritetype *tspr)
{
    point3d fp, fp1, fp2, m0, m1, a0;
    md3xyzn_t *v0, *v1;
    int i, j, k, l, surfi;
    float f, g, k0, k1, k2, k3, k4, k5, k6, k7, mat[16];
    md3surf_t *s;
    GLfloat pc[4];
    int                 texunits = GL_TEXTURE0_ARB;
    mdskinmap_t *sk;
    //PLAG : sorting stuff
    void*               vbotemp;
    point3d*            vertexhandle = NULL;
    unsigned short*     indexhandle;
    char lpal = (tspr->owner >= MAXSPRITES) ? tspr->pal : sprite[tspr->owner].pal;

    if (r_vbos && (m->vbos == NULL))
        mdloadvbos(m);

    //    if ((tspr->cstat&48) == 32) return 0;

    updateanimation((md2model *)m,tspr);

    //create current&next frame's vertex list from whole list

    f = m->interpol; g = 1-f;

    if (m->interpol < 0 || m->interpol > 1 ||
            m->cframe < 0 || m->cframe >= m->numframes ||
            m->nframe < 0 || m->nframe >= m->numframes)
    {
        OSD_Printf("Model frame out of bounds!\n");
        if (m->interpol < 0)
            m->interpol = 0;
        if (m->interpol > 1)
            m->interpol = 1;
        if (m->cframe < 0)
            m->cframe = 0;
        if (m->cframe >= m->numframes)
            m->cframe = m->numframes - 1;
        if (m->nframe < 0)
            m->nframe = 0;
        if (m->nframe >= m->numframes)
            m->nframe = m->numframes - 1;
    }

    if (m->head.flags == 1337)
    {
        // md2
        m0.x = m->scale * g; m1.x = m->scale *f;
        m0.y = m->scale * g; m1.y = m->scale *f;
        m0.z = m->scale * g; m1.z = m->scale *f;
        a0.x = a0.y = 0; a0.z = ((globalorientation&8)?-m->zadd:m->zadd)*m->scale;
    }
    else
    {
        m0.x = (1.0/64.0) * m->scale * g; m1.x = (1.0/64.0) * m->scale *f;
        m0.y = (1.0/64.0) * m->scale * g; m1.y = (1.0/64.0) * m->scale *f;
        m0.z = (1.0/64.0) * m->scale * g; m1.z = (1.0/64.0) * m->scale *f;
        a0.x = a0.y = 0; a0.z = ((globalorientation&8)?-m->zadd:m->zadd)*m->scale;
    }


    // Parkar: Moved up to be able to use k0 for the y-flipping code
    k0 = tspr->z;
    if ((globalorientation&128) && !((globalorientation&48)==32)) k0 += (float)((tilesizy[tspr->picnum]*tspr->yrepeat)<<1);

    // Parkar: Changed to use the same method as centeroriented sprites
    if (globalorientation&8) //y-flipping
    {
        m0.z = -m0.z; m1.z = -m1.z; a0.z = -a0.z;
        k0 -= (float)((tilesizy[tspr->picnum]*tspr->yrepeat)<<2);
    }
    if (globalorientation&4) { m0.y = -m0.y; m1.y = -m1.y; a0.y = -a0.y; } //x-flipping

    f = ((float)tspr->xrepeat)/64*m->bscale;
    m0.x *= f; m1.x *= f; a0.x *= f; f = -f;   // 20040610: backwards models aren't cool
    m0.y *= f; m1.y *= f; a0.y *= f;
    f = ((float)tspr->yrepeat)/64*m->bscale;
    m0.z *= f; m1.z *= f; a0.z *= f;

    // floor aligned
    k1 = tspr->y;
    if ((globalorientation&48)==32)
    {
        m0.z = -m0.z; m1.z = -m1.z; a0.z = -a0.z;
        m0.y = -m0.y; m1.y = -m1.y; a0.y = -a0.y;
        f = a0.x; a0.x = a0.z; a0.z = f;
        k1 += (float)((tilesizy[tspr->picnum]*tspr->yrepeat)>>3);
    }

    f = (65536.0*512.0)/((float)xdimen*viewingrange);
    g = 32.0/((float)xdimen*gxyaspect);
    m0.y *= f; m1.y *= f; a0.y = (((float)(tspr->x-globalposx))/  1024.0 + a0.y)*f;
    m0.x *=-f; m1.x *=-f; a0.x = (((float)(k1     -globalposy))/ -1024.0 + a0.x)*-f;
    m0.z *= g; m1.z *= g; a0.z = (((float)(k0     -globalposz))/-16384.0 + a0.z)*g;

    k0 = ((float)(tspr->x-globalposx))*f/1024.0;
    k1 = ((float)(tspr->y-globalposy))*f/1024.0;
    f = gcosang2*gshang;
    g = gsinang2*gshang;
    k4 = (float)sintable[(tspr->ang+spriteext[tspr->owner].angoff+1024)&2047] / 16384.0;
    k5 = (float)sintable[(tspr->ang+spriteext[tspr->owner].angoff+ 512)&2047] / 16384.0;
    k2 = k0*(1-k4)+k1*k5;
    k3 = k1*(1-k4)-k0*k5;
    k6 = f*gstang - gsinang*gctang; k7 = g*gstang + gcosang*gctang;
    mat[0] = k4*k6 + k5*k7; mat[4] = gchang*gstang; mat[ 8] = k4*k7 - k5*k6; mat[12] = k2*k6 + k3*k7;
    k6 = f*gctang + gsinang*gstang; k7 = g*gctang - gcosang*gstang;
    mat[1] = k4*k6 + k5*k7; mat[5] = gchang*gctang; mat[ 9] = k4*k7 - k5*k6; mat[13] = k2*k6 + k3*k7;
    k6 =           gcosang2*gchang; k7 =           gsinang2*gchang;
    mat[2] = k4*k6 + k5*k7; mat[6] =-gshang;        mat[10] = k4*k7 - k5*k6; mat[14] = k2*k6 + k3*k7;

    mat[12] += a0.y*mat[0] + a0.z*mat[4] + a0.x*mat[ 8];
    mat[13] += a0.y*mat[1] + a0.z*mat[5] + a0.x*mat[ 9];
    mat[14] += a0.y*mat[2] + a0.z*mat[6] + a0.x*mat[10];

    // floor aligned
    if ((globalorientation&48)==32)
    {
        f = mat[4]; mat[4] = mat[8]*16.0; mat[8] = -f*(1.0/16.0);
        f = mat[5]; mat[5] = mat[9]*16.0; mat[9] = -f*(1.0/16.0);
        f = mat[6]; mat[6] = mat[10]*16.0; mat[10] = -f*(1.0/16.0);
    }

    //Mirrors
    if (grhalfxdown10x < 0) { mat[0] = -mat[0]; mat[4] = -mat[4]; mat[8] = -mat[8]; mat[12] = -mat[12]; }

    //------------
    //bit 10 is an ugly hack in game.c\animatesprites telling MD2SPRITE
    //to use Z-buffer hacks to hide overdraw problems with the shadows
    if (tspr->cstat&1024)
    {
        bglDepthFunc(GL_LESS); //NEVER,LESS,(,L)EQUAL,GREATER,(NOT,G)EQUAL,ALWAYS
        bglDepthRange(0.0,0.9999);
    }
    bglPushAttrib(GL_POLYGON_BIT);
    if ((grhalfxdown10x >= 0) ^((globalorientation&8) != 0) ^((globalorientation&4) != 0)) bglFrontFace(GL_CW); else bglFrontFace(GL_CCW);
    bglEnable(GL_CULL_FACE);
    bglCullFace(GL_BACK);

    bglEnable(GL_TEXTURE_2D);

    pc[0] = pc[1] = pc[2] = ((float)(numpalookups-min(max((globalshade * shadescale)+m->shadeoff,0),numpalookups)))/((float)numpalookups);
    if (!(hictinting[globalpal].f&4))
    {
        if (!(m->flags&1) || (!(tspr->owner >= MAXSPRITES) && sector[sprite[tspr->owner].sectnum].floorpal!=0))
        {
            pc[0] *= (float)hictinting[globalpal].r / 255.0;
            pc[1] *= (float)hictinting[globalpal].g / 255.0;
            pc[2] *= (float)hictinting[globalpal].b / 255.0;
            if (hictinting[MAXPALOOKUPS-1].r != 255 || hictinting[MAXPALOOKUPS-1].g != 255 || hictinting[MAXPALOOKUPS-1].b != 255)
            {
                pc[0] *= (float)hictinting[MAXPALOOKUPS-1].r / 255.0;
                pc[1] *= (float)hictinting[MAXPALOOKUPS-1].g / 255.0;
                pc[2] *= (float)hictinting[MAXPALOOKUPS-1].b / 255.0;
            }
        }
        else globalnoeffect=1;
    }

    if (tspr->cstat&2) { if (!(tspr->cstat&512)) pc[3] = 0.66; else pc[3] = 0.33; }
    else pc[3] = 1.0;
    if (m->usesalpha) //Sprites with alpha in texture
    {
        //      bglEnable(GL_BLEND);// bglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        //      bglEnable(GL_ALPHA_TEST); bglAlphaFunc(GL_GREATER,0.32);
        //      float al = 0.32;
        // PLAG : default cutoff removed
        float al = 0.0;
        if (alphahackarray[globalpicnum] != 0)
            al=alphahackarray[globalpicnum];
        if (!peelcompiling)
            bglEnable(GL_BLEND);
        bglEnable(GL_ALPHA_TEST);
        bglAlphaFunc(GL_GREATER,al);
    }
    else
    {
        if (tspr->cstat&2 && (!peelcompiling)) bglEnable(GL_BLEND); //else bglDisable(GL_BLEND);
    }
    bglColor4f(pc[0],pc[1],pc[2],pc[3]);
    //if (m->head.flags == 1337)
    //    bglColor4f(0.0f, 0.0f, 1.0f, 1.0f);
    //------------

    // PLAG: Cleaner model rotation code
    if (spriteext[tspr->owner].pitch || spriteext[tspr->owner].roll || m->head.flags == 1337)
    {
        if (spriteext[tspr->owner].xoff)
            a0.x = (int)(spriteext[tspr->owner].xoff / (2048 * (m0.x+m1.x)));
        else
            a0.x = 0;
        if (spriteext[tspr->owner].yoff)
            a0.y = (int)(spriteext[tspr->owner].yoff / (2048 * (m0.x+m1.x)));
        else
            a0.y = 0;
        if ((spriteext[tspr->owner].zoff) && !(tspr->cstat&1024))
            a0.z = (int)(spriteext[tspr->owner].zoff / (524288 * (m0.z+m1.z)));
        else
            a0.z = 0;
        k0 = (float)sintable[(spriteext[tspr->owner].pitch+512)&2047] / 16384.0;
        k1 = (float)sintable[spriteext[tspr->owner].pitch&2047] / 16384.0;
        k2 = (float)sintable[(spriteext[tspr->owner].roll+512)&2047] / 16384.0;
        k3 = (float)sintable[spriteext[tspr->owner].roll&2047] / 16384.0;
    }
    for (surfi=0;surfi<m->head.numsurfs;surfi++)
    {
        s = &m->head.surfs[surfi];
        v0 = &s->xyzn[m->cframe*s->numverts];
        v1 = &s->xyzn[m->nframe*s->numverts];

        if (r_vertexarrays && r_vbos)
        {
            if (++curvbo >= r_vbocount)
                curvbo = 0;

            bglBindBufferARB(GL_ARRAY_BUFFER_ARB, vertvbos[curvbo]);
            vbotemp = bglMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB);
            vertexhandle = (point3d *)vbotemp;
        }

        for (i=s->numverts-1;i>=0;i--)
        {
            if (spriteext[tspr->owner].pitch || spriteext[tspr->owner].roll || m->head.flags == 1337)
            {
                fp.z = ((m->head.flags == 1337) ? (v0[i].x * m->muladdframes[m->cframe*2].x) + m->muladdframes[m->cframe*2+1].x : v0[i].x) + a0.x;
                fp.x = ((m->head.flags == 1337) ? (v0[i].y * m->muladdframes[m->cframe*2].y) + m->muladdframes[m->cframe*2+1].y : v0[i].y) + a0.y;
                fp.y = ((m->head.flags == 1337) ? (v0[i].z * m->muladdframes[m->cframe*2].z) + m->muladdframes[m->cframe*2+1].z : v0[i].z) + a0.z;
                fp1.x = fp.x*k2 +       fp.y*k3;
                fp1.y = fp.x*k0*(-k3) + fp.y*k0*k2 + fp.z*(-k1);
                fp1.z = fp.x*k1*(-k3) + fp.y*k1*k2 + fp.z*k0;
                fp.z = ((m->head.flags == 1337) ? (v1[i].x * m->muladdframes[m->nframe*2].x) + m->muladdframes[m->nframe*2+1].x : v1[i].x) + a0.x;
                fp.x = ((m->head.flags == 1337) ? (v1[i].y * m->muladdframes[m->nframe*2].y) + m->muladdframes[m->nframe*2+1].y : v1[i].y) + a0.y;
                fp.y = ((m->head.flags == 1337) ? (v1[i].z * m->muladdframes[m->nframe*2].z) + m->muladdframes[m->nframe*2+1].z : v1[i].z) + a0.z;
                fp2.x = fp.x*k2 +       fp.y*k3;
                fp2.y = fp.x*k0*(-k3) + fp.y*k0*k2 + fp.z*(-k1);
                fp2.z = fp.x*k1*(-k3) + fp.y*k1*k2 + fp.z*k0;
                fp.z = (fp1.z - a0.x)*m0.x + (fp2.z - a0.x)*m1.x;
                fp.x = (fp1.x - a0.y)*m0.y + (fp2.x - a0.y)*m1.y;
                fp.y = (fp1.y - a0.z)*m0.z + (fp2.y - a0.z)*m1.z;
            }
            else
            {
                fp.z = v0[i].x*m0.x + v1[i].x*m1.x;
                fp.y = v0[i].z*m0.z + v1[i].z*m1.z;
                fp.x = v0[i].y*m0.y + v1[i].y*m1.y;
            }
            if (r_vertexarrays && r_vbos)
            {
                vertexhandle[i].x = fp.x;
                vertexhandle[i].y = fp.y;
                vertexhandle[i].z = fp.z;
            }
            vertlist[i].x = fp.x;
            vertlist[i].y = fp.y;
            vertlist[i].z = fp.z;
        }

        if (r_vertexarrays && r_vbos)
        {
            bglUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
            bglBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
        }

        bglMatrixMode(GL_MODELVIEW); //Let OpenGL (and perhaps hardware :) handle the matrix rotation
        mat[3] = mat[7] = mat[11] = 0.f; mat[15] = 1.f; bglLoadMatrixf(mat);
        // PLAG: End

        i = mdloadskin((md2model *)m,tile2model[Ptile2tile(tspr->picnum,lpal)].skinnum,globalpal,surfi); if (!i) continue;
        //i = mdloadskin((md2model *)m,tile2model[Ptile2tile(tspr->picnum,lpal)].skinnum,surfi); //hack for testing multiple surfaces per MD3
        bglBindTexture(GL_TEXTURE_2D, i);

        if (r_detailmapping && !r_depthpeeling && !(tspr->cstat&1024))
            i = mdloadskin((md2model *)m,tile2model[Ptile2tile(tspr->picnum,lpal)].skinnum,DETAILPAL,surfi);
        else
            i = 0;

        if (i)
        {
            bglActiveTextureARB(++texunits);

            bglEnable(GL_TEXTURE_2D);
            bglBindTexture(GL_TEXTURE_2D, i);

            bglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
            bglTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);

            bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB);
            bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);

            bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE);
            bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);

            bglTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE);
            bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB);
            bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);

            bglTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 2.0f);

            bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
            bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);

            for (sk = m->skinmap; sk; sk = sk->next)
                if ((int)sk->palette == DETAILPAL && sk->skinnum == tile2model[Ptile2tile(tspr->picnum,lpal)].skinnum && sk->surfnum == surfi)
                    f = sk->param;

            bglMatrixMode(GL_TEXTURE);
            bglLoadIdentity();
            bglScalef(f, f, 1.0f);
            bglMatrixMode(GL_MODELVIEW);
        }

        if (r_glowmapping && !r_depthpeeling && !(tspr->cstat&1024))
            i = mdloadskin((md2model *)m,tile2model[Ptile2tile(tspr->picnum,lpal)].skinnum,GLOWPAL,surfi);
        else
            i = 0;

        if (i)
        {
            bglActiveTextureARB(++texunits);

            bglEnable(GL_TEXTURE_2D);
            bglBindTexture(GL_TEXTURE_2D, i);

            bglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
            bglTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_INTERPOLATE_ARB);

            bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB);
            bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);

            bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE);
            bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);

            bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE2_RGB_ARB, GL_TEXTURE);
            bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND2_RGB_ARB, GL_ONE_MINUS_SRC_ALPHA);

            bglTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE);
            bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB);
            bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);

            bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
            bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        }

        if (r_vertexarrays && r_vbos)
        {
            bglBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, indexvbos[curvbo]);
            vbotemp = bglMapBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB);
            indexhandle = (unsigned short *)vbotemp;
        }
        else
            indexhandle = m->vindexes;

        //PLAG: delayed polygon-level sorted rendering
        if (m->usesalpha && !(tspr->cstat & 1024) && !r_depthpeeling)
        {
            for (i=s->numtris-1;i>=0;i--)
            {
                // Matrix multiplication - ugly but clear
                fp.x = (vertlist[s->tris[i].i[0]].x * mat[0]) + (vertlist[s->tris[i].i[0]].y * mat[4]) + (vertlist[s->tris[i].i[0]].z * mat[8]) + mat[12];
                fp.y = (vertlist[s->tris[i].i[0]].x * mat[1]) + (vertlist[s->tris[i].i[0]].y * mat[5]) + (vertlist[s->tris[i].i[0]].z * mat[9]) + mat[13];
                fp.z = (vertlist[s->tris[i].i[0]].x * mat[2]) + (vertlist[s->tris[i].i[0]].y * mat[6]) + (vertlist[s->tris[i].i[0]].z * mat[10]) + mat[14];

                fp1.x = (vertlist[s->tris[i].i[1]].x * mat[0]) + (vertlist[s->tris[i].i[1]].y * mat[4]) + (vertlist[s->tris[i].i[1]].z * mat[8]) + mat[12];
                fp1.y = (vertlist[s->tris[i].i[1]].x * mat[1]) + (vertlist[s->tris[i].i[1]].y * mat[5]) + (vertlist[s->tris[i].i[1]].z * mat[9]) + mat[13];
                fp1.z = (vertlist[s->tris[i].i[1]].x * mat[2]) + (vertlist[s->tris[i].i[1]].y * mat[6]) + (vertlist[s->tris[i].i[1]].z * mat[10]) + mat[14];

                fp2.x = (vertlist[s->tris[i].i[2]].x * mat[0]) + (vertlist[s->tris[i].i[2]].y * mat[4]) + (vertlist[s->tris[i].i[2]].z * mat[8]) + mat[12];
                fp2.y = (vertlist[s->tris[i].i[2]].x * mat[1]) + (vertlist[s->tris[i].i[2]].y * mat[5]) + (vertlist[s->tris[i].i[2]].z * mat[9]) + mat[13];
                fp2.z = (vertlist[s->tris[i].i[2]].x * mat[2]) + (vertlist[s->tris[i].i[2]].y * mat[6]) + (vertlist[s->tris[i].i[2]].z * mat[10]) + mat[14];

                f = (fp.x * fp.x) + (fp.y * fp.y) + (fp.z * fp.z);

                g = (fp1.x * fp1.x) + (fp1.y * fp1.y) + (fp1.z * fp1.z);
                if (f > g)
                    f = g;
                g = (fp2.x * fp2.x) + (fp2.y * fp2.y) + (fp2.z * fp2.z);
                if (f > g)
                    f = g;

                m->maxdepths[i] = f;
                m->indexes[i] = i;
            }

            // dichotomic recursive sorting - about 100x less iterations than bubblesort
            quicksort(m->indexes, m->maxdepths, 0, s->numtris - 1);

            if (r_vertexarrays)
            {
                k = 0;
                for (i=s->numtris-1;i>=0;i--)
                    for (j=0;j<3;j++)
                        indexhandle[k++] = s->tris[m->indexes[i]].i[j];
            }
            else
            {
                bglBegin(GL_TRIANGLES);
                for (i=s->numtris-1;i>=0;i--)
                    for (j=0;j<3;j++)
                    {
                        k = s->tris[m->indexes[i]].i[j];
                        if (texunits > GL_TEXTURE0_ARB)
                        {
                            l = GL_TEXTURE0_ARB;
                            while (l <= texunits)
                                bglMultiTexCoord2fARB(l++, s->uv[k].u,s->uv[k].v);
                        }
                        else
                            bglTexCoord2f(s->uv[k].u,s->uv[k].v);
                        bglVertex3fv((float *)&vertlist[k]);
                    }
                bglEnd();
            }
        }
        else
        {
            if (r_vertexarrays)
            {
                k = 0;
                for (i=s->numtris-1;i>=0;i--)
                    for (j=0;j<3;j++)
                        indexhandle[k++] = s->tris[i].i[j];
            }
            else
            {
                bglBegin(GL_TRIANGLES);
                for (i=s->numtris-1;i>=0;i--)
                    for (j=0;j<3;j++)
                    {
                        k = s->tris[i].i[j];
                        if (texunits > GL_TEXTURE0_ARB)
                        {
                            l = GL_TEXTURE0_ARB;
                            while (l <= texunits)
                                bglMultiTexCoord2fARB(l++, s->uv[k].u,s->uv[k].v);
                        }
                        else
                            bglTexCoord2f(s->uv[k].u,s->uv[k].v);
                        bglVertex3fv((float *)&vertlist[k]);
                    }
                bglEnd();
            }
        }

        if (r_vertexarrays && r_vbos)
        {
            bglUnmapBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB);
            bglBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
        }

        if (r_vertexarrays)
        {
            if (r_vbos)
                bglBindBufferARB(GL_ARRAY_BUFFER_ARB, m->vbos[surfi]);
            l = GL_TEXTURE0_ARB;
            while (l <= texunits)
            {
                bglClientActiveTextureARB(l++);
                bglEnableClientState(GL_TEXTURE_COORD_ARRAY);
                if (r_vbos)
                    bglTexCoordPointer(2, GL_FLOAT, 0, 0);
                else
                    bglTexCoordPointer(2, GL_FLOAT, 0, &(s->uv[0].u));
            }

            if (r_vbos)
            {
                bglBindBufferARB(GL_ARRAY_BUFFER_ARB, vertvbos[curvbo]);
                bglVertexPointer(3, GL_FLOAT, 0, 0);
            }
            else
                bglVertexPointer(3, GL_FLOAT, 0, &(vertlist[0].x));

            if (r_vbos)
            {
                bglBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, indexvbos[curvbo]);
                bglDrawElements(GL_TRIANGLES, s->numtris * 3, GL_UNSIGNED_SHORT, 0);
            }
            else
                bglDrawElements(GL_TRIANGLES, s->numtris * 3, GL_UNSIGNED_SHORT, m->vindexes);

            if (r_vbos)
            {
                bglBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
                bglBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
            }
        }

        while (texunits > GL_TEXTURE0_ARB)
        {
            bglMatrixMode(GL_TEXTURE);
            bglLoadIdentity();
            bglMatrixMode(GL_MODELVIEW);
            bglTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1.0f);
            bglDisable(GL_TEXTURE_2D);
            if (r_vertexarrays)
            {
                bglDisableClientState(GL_TEXTURE_COORD_ARRAY);
                bglClientActiveTextureARB(texunits - 1);
            }
            bglActiveTextureARB(--texunits);
        }
    }
    //------------

    if (m->usesalpha) bglDisable(GL_ALPHA_TEST);
    bglDisable(GL_CULL_FACE);
    bglPopAttrib();
    if (tspr->cstat&1024)
    {
        bglDepthFunc(GL_LESS); //NEVER,LESS,(,L)EQUAL,GREATER,(NOT,G)EQUAL,ALWAYS
        bglDepthRange(0.0,0.99999);
    }
    bglLoadIdentity();

    globalnoeffect=0;
    return 1;
}

static void md3free(md3model *m)
{
    mdanim_t *anim, *nanim = NULL;
    mdskinmap_t *sk, *nsk = NULL;
    md3surf_t *s;
    int surfi;

    if (!m) return;

    for (anim=m->animations; anim; anim=nanim)
    {
        nanim = anim->next;
        free(anim);
    }
    for (sk=m->skinmap; sk; sk=nsk)
    {
        nsk = sk->next;
        free(sk->fn);
        if (sk->palmap)
        {
            //_initprintf("Kill %d\n",sk->palette);
            free(sk->palmap);sk->palmap=0;
        }
        free(sk);
    }

    if (m->head.surfs)
    {
        for (surfi=m->head.numsurfs-1;surfi>=0;surfi--)
        {
            s = &m->head.surfs[surfi];
            if (s->tris) free(s->tris);
            if (m->head.flags == 1337)
            {
                if (s->shaders) free(s->shaders);
                if (s->uv) free(s->uv);
                if (s->xyzn) free(s->xyzn);
            }
        }
        free(m->head.surfs);
    }
    if (m->head.tags) free(m->head.tags);
    if (m->head.frames) free(m->head.frames);

    if (m->texid) free(m->texid);

    if (m->muladdframes) free(m->muladdframes);

    if (m->indexes) free(m->indexes);
    if (m->vindexes) free(m->vindexes);
    if (m->maxdepths) free(m->maxdepths);

    if (r_vbos && m->vbos)
    {
        bglDeleteBuffersARB(m->head.numsurfs, m->vbos);
        free(m->vbos);
    }

    free(m);
}

//---------------------------------------- MD3 LIBRARY ENDS ----------------------------------------
//--------------------------------------- VOX LIBRARY BEGINS ---------------------------------------

//For loading/conversion only
static int xsiz, ysiz, zsiz, yzsiz, *vbit = 0; //vbit: 1 bit per voxel: 0=air,1=solid
static float xpiv, ypiv, zpiv; //Might want to use more complex/unique names!
static int *vcolhashead = 0, vcolhashsizm1;
typedef struct { int p, c, n; } voxcol_t;
static voxcol_t *vcol = 0; int vnum = 0, vmax = 0;
typedef struct { short x, y; } spoint2d;
static spoint2d *shp;
static int *shcntmal, *shcnt = 0, shcntp;
static int mytexo5, *zbit, gmaxx, gmaxy, garea, pow2m1[33];
static voxmodel *gvox;

//pitch must equal xsiz*4
unsigned gloadtex(int *picbuf, int xsiz, int ysiz, int is8bit, int dapal)
{
    unsigned rtexid;
    coltype *pic, *pic2;
    unsigned char *cptr;
    int i;

    pic = (coltype *)picbuf; //Correct for GL's RGB order; also apply gamma here..
    pic2 = (coltype *)malloc(xsiz*ysiz*sizeof(int)); if (!pic2) return((unsigned)-1);
    cptr = (unsigned char*)&britable[gammabrightness ? 0 : curbrightness][0];
    if (!is8bit)
    {
        for (i=xsiz*ysiz-1;i>=0;i--)
        {
            pic2[i].b = cptr[pic[i].r];
            pic2[i].g = cptr[pic[i].g];
            pic2[i].r = cptr[pic[i].b];
            pic2[i].a = 255;
        }
    }
    else
    {
        if (palookup[dapal] == NULL) dapal = 0;
        for (i=xsiz*ysiz-1;i>=0;i--)
        {
            pic2[i].b = cptr[palette[(int)palookup[dapal][pic[i].a]*3+2]*4];
            pic2[i].g = cptr[palette[(int)palookup[dapal][pic[i].a]*3+1]*4];
            pic2[i].r = cptr[palette[(int)palookup[dapal][pic[i].a]*3+0]*4];
            pic2[i].a = 255;
        }
    }

    bglGenTextures(1,(GLuint*)&rtexid);
    bglBindTexture(GL_TEXTURE_2D,rtexid);
    bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    bglTexImage2D(GL_TEXTURE_2D,0,4,xsiz,ysiz,0,GL_RGBA,GL_UNSIGNED_BYTE,(unsigned char *)pic2);
    free(pic2);
    return(rtexid);
}

static int getvox(int x, int y, int z)
{
    z += x*yzsiz + y*zsiz;
    for (x=vcolhashead[(z*214013)&vcolhashsizm1];x>=0;x=vcol[x].n)
        if (vcol[x].p == z) return(vcol[x].c);
    return(0x808080);
}

static void putvox(int x, int y, int z, int col)
{
    if (vnum >= vmax) { vmax = max(vmax<<1,4096); vcol = (voxcol_t *)realloc(vcol,vmax*sizeof(voxcol_t)); }

    z += x*yzsiz + y*zsiz;
    vcol[vnum].p = z; z = ((z*214013)&vcolhashsizm1);
    vcol[vnum].c = col;
    vcol[vnum].n = vcolhashead[z]; vcolhashead[z] = vnum++;
}

//Set all bits in vbit from (x,y,z0) to (x,y,z1-1) to 0's
#if 0
static void setzrange0(int *lptr, int z0, int z1)
{
    int z, ze;
    if (!((z0^z1)&~31)) { lptr[z0>>5] &= ((~(-1<<SHIFTMOD32(z0)))|(-1<<SHIFTMOD32(z1))); return; }
    z = (z0>>5); ze = (z1>>5);
    lptr[z] &=~(-1<<SHIFTMOD32(z0)); for (z++;z<ze;z++) lptr[z] = 0;
    lptr[z] &= (-1<<SHIFTMOD32(z1));
}
#endif
//Set all bits in vbit from (x,y,z0) to (x,y,z1-1) to 1's
static void setzrange1(int *lptr, int z0, int z1)
{
    int z, ze;
    if (!((z0^z1)&~31)) { lptr[z0>>5] |= ((~(-1<<SHIFTMOD32(z1)))&(-1<<SHIFTMOD32(z0))); return; }
    z = (z0>>5); ze = (z1>>5);
    lptr[z] |= (-1<<SHIFTMOD32(z0)); for (z++;z<ze;z++) lptr[z] = -1;
    lptr[z] |=~(-1<<SHIFTMOD32(z1));
}

static int isrectfree(int x0, int y0, int dx, int dy)
{
#if 0
    int i, j, x;
    i = y0*gvox->mytexx + x0;
    for (dy=0;dy;dy--,i+=gvox->mytexx)
        for (x=0;x<dx;x++) { j = i+x; if (zbit[j>>5]&(1<<SHIFTMOD32(j))) return(0); }
#else
    int i, c, m, m1, x;

    i = y0*mytexo5 + (x0>>5); dx += x0-1; c = (dx>>5) - (x0>>5);
    m = ~pow2m1[x0&31]; m1 = pow2m1[(dx&31)+1];
    if (!c) { for (m&=m1;dy;dy--,i+=mytexo5) if (zbit[i]&m) return(0); }
    else
    {
        for (;dy;dy--,i+=mytexo5)
        {
            if (zbit[i]&m) return(0);
            for (x=1;x<c;x++) if (zbit[i+x]) return(0);
            if (zbit[i+x]&m1) return(0);
        }
    }
#endif
    return(1);
}

static void setrect(int x0, int y0, int dx, int dy)
{
#if 0
    int i, j, y;
    i = y0*gvox->mytexx + x0;
    for (y=0;y<dy;y++,i+=gvox->mytexx)
        for (x=0;x<dx;x++) { j = i+x; zbit[j>>5] |= (1<<SHIFTMOD32(j)); }
#else
    int i, c, m, m1, x;

    i = y0*mytexo5 + (x0>>5); dx += x0-1; c = (dx>>5) - (x0>>5);
    m = ~pow2m1[x0&31]; m1 = pow2m1[(dx&31)+1];
    if (!c) { for (m&=m1;dy;dy--,i+=mytexo5) zbit[i] |= m; }
    else
    {
        for (;dy;dy--,i+=mytexo5)
        {
            zbit[i] |= m;
            for (x=1;x<c;x++) zbit[i+x] = -1;
            zbit[i+x] |= m1;
        }
    }
#endif
}

static void cntquad(int x0, int y0, int z0, int x1, int y1, int z1, int x2, int y2, int z2, int face)
{
    int x, y, z;

    UNREFERENCED_PARAMETER(x1);
    UNREFERENCED_PARAMETER(y1);
    UNREFERENCED_PARAMETER(z1);
    UNREFERENCED_PARAMETER(face);

    x = labs(x2-x0); y = labs(y2-y0); z = labs(z2-z0);
    if (!x) x = z; else if (!y) y = z;
    if (x < y) { z = x; x = y; y = z; }
    shcnt[y*shcntp+x]++;
    if (x > gmaxx) gmaxx = x;
    if (y > gmaxy) gmaxy = y;
    garea += (x+(VOXBORDWIDTH<<1))*(y+(VOXBORDWIDTH<<1));
    gvox->qcnt++;
}

static void addquad(int x0, int y0, int z0, int x1, int y1, int z1, int x2, int y2, int z2, int face)
{
    int i, j, x, y, z, xx, yy, nx = 0, ny = 0, nz = 0, *lptr;
    voxrect_t *qptr;

    x = labs(x2-x0); y = labs(y2-y0); z = labs(z2-z0);
    if (!x) { x = y; y = z; i = 0; }
    else if (!y) { y = z; i = 1; }
    else i = 2;
    if (x < y) { z = x; x = y; y = z; i += 3; }
    z = shcnt[y*shcntp+x]++;
    lptr = &gvox->mytex[(shp[z].y+VOXBORDWIDTH)*gvox->mytexx+(shp[z].x+VOXBORDWIDTH)];
    switch (face)
    {
    case 0:
        ny = y1; x2 = x0; x0 = x1; x1 = x2; break;
    case 1:
        ny = y0; y0++; y1++; y2++; break;
    case 2:
        nz = z1; y0 = y2; y2 = y1; y1 = y0; z0++; z1++; z2++; break;
    case 3:
        nz = z0; break;
    case 4:
        nx = x1; y2 = y0; y0 = y1; y1 = y2; x0++; x1++; x2++; break;
    case 5:
        nx = x0; break;
    }
    for (yy=0;yy<y;yy++,lptr+=gvox->mytexx)
        for (xx=0;xx<x;xx++)
        {
            switch (face)
            {
            case 0:
                if (i < 3) { nx = x1+x-1-xx; nz = z1+yy;   } //back
                else { nx = x1+y-1-yy; nz = z1+xx;   }
                break;
            case 1:
                if (i < 3) { nx = x0+xx;     nz = z0+yy;   } //front
                else { nx = x0+yy;     nz = z0+xx;   }
                break;
            case 2:
                if (i < 3) { nx = x1-x+xx;   ny = y1-1-yy; } //bot
                else { nx = x1-1-yy;   ny = y1-1-xx; }
                break;
            case 3:
                if (i < 3) { nx = x0+xx;     ny = y0+yy;   } //top
                else { nx = x0+yy;     ny = y0+xx;   }
                break;
            case 4:
                if (i < 3) { ny = y1+x-1-xx; nz = z1+yy;   } //right
                else { ny = y1+y-1-yy; nz = z1+xx;   }
                break;
            case 5:
                if (i < 3) { ny = y0+xx;     nz = z0+yy;   } //left
                else { ny = y0+yy;     nz = z0+xx;   }
                break;
            }
            lptr[xx] = getvox(nx,ny,nz);
        }

    //Extend borders horizontally
    for (yy=VOXBORDWIDTH;yy<y+VOXBORDWIDTH;yy++)
        for (xx=0;xx<VOXBORDWIDTH;xx++)
        {
            lptr = &gvox->mytex[(shp[z].y+yy)*gvox->mytexx+shp[z].x];
            lptr[xx] = lptr[VOXBORDWIDTH]; lptr[xx+x+VOXBORDWIDTH] = lptr[x-1+VOXBORDWIDTH];
        }
    //Extend borders vertically
    for (yy=0;yy<VOXBORDWIDTH;yy++)
    {
        memcpy(&gvox->mytex[(shp[z].y+yy)*gvox->mytexx+shp[z].x],
               &gvox->mytex[(shp[z].y+VOXBORDWIDTH)*gvox->mytexx+shp[z].x],
               (x+(VOXBORDWIDTH<<1))<<2);
        memcpy(&gvox->mytex[(shp[z].y+y+yy+VOXBORDWIDTH)*gvox->mytexx+shp[z].x],
               &gvox->mytex[(shp[z].y+y-1+VOXBORDWIDTH)*gvox->mytexx+shp[z].x],
               (x+(VOXBORDWIDTH<<1))<<2);
    }

    qptr = &gvox->quad[gvox->qcnt];
    qptr->v[0].x = x0; qptr->v[0].y = y0; qptr->v[0].z = z0;
    qptr->v[1].x = x1; qptr->v[1].y = y1; qptr->v[1].z = z1;
    qptr->v[2].x = x2; qptr->v[2].y = y2; qptr->v[2].z = z2;
    for (j=0;j<3;j++) { qptr->v[j].u = shp[z].x+VOXBORDWIDTH; qptr->v[j].v = shp[z].y+VOXBORDWIDTH; }
    if (i < 3) qptr->v[1].u += x; else qptr->v[1].v += y;
    qptr->v[2].u += x; qptr->v[2].v += y;

    qptr->v[3].u = qptr->v[0].u - qptr->v[1].u + qptr->v[2].u;
    qptr->v[3].v = qptr->v[0].v - qptr->v[1].v + qptr->v[2].v;
    qptr->v[3].x = qptr->v[0].x - qptr->v[1].x + qptr->v[2].x;
    qptr->v[3].y = qptr->v[0].y - qptr->v[1].y + qptr->v[2].y;
    qptr->v[3].z = qptr->v[0].z - qptr->v[1].z + qptr->v[2].z;
    if (gvox->qfacind[face] < 0) gvox->qfacind[face] = gvox->qcnt;
    gvox->qcnt++;

}

static int isolid(int x, int y, int z)
{
    if ((unsigned int)x >= (unsigned int)xsiz) return(0);
    if ((unsigned int)y >= (unsigned int)ysiz) return(0);
    if ((unsigned int)z >= (unsigned int)zsiz) return(0);
    z += x*yzsiz + y*zsiz; return(vbit[z>>5]&(1<<SHIFTMOD32(z)));
}

static voxmodel *vox2poly()
{
    int i, j, x, y, z, v, ov, oz = 0, cnt, sc, x0, y0, dx, dy,*bx0, *by0;
    void (*daquad)(int, int, int, int, int, int, int, int, int, int);

    gvox = (voxmodel *)malloc(sizeof(voxmodel)); if (!gvox) return(0);
    memset(gvox,0,sizeof(voxmodel));

    //x is largest dimension, y is 2nd largest dimension
    x = xsiz; y = ysiz; z = zsiz;
    if ((x < y) && (x < z)) x = z; else if (y < z) y = z;
    if (x < y) { z = x; x = y; y = z; }
    shcntp = x; i = x*y*sizeof(int);
    shcntmal = (int *)malloc(i); if (!shcntmal) { free(gvox); return(0); }
    memset(shcntmal,0,i); shcnt = &shcntmal[-shcntp-1];
    gmaxx = gmaxy = garea = 0;

    if (pow2m1[32] != -1) { for (i=0;i<32;i++) pow2m1[i] = (1<<i)-1; pow2m1[32] = -1; }
    for (i=0;i<7;i++) gvox->qfacind[i] = -1;

    i = ((max(ysiz,zsiz)+1)<<2);
    bx0 = (int *)malloc(i<<1); if (!bx0) { free(gvox); return(0); }
    by0 = (int *)(((intptr_t)bx0)+i);

    for (cnt=0;cnt<2;cnt++)
    {
        if (!cnt) daquad = cntquad;
        else daquad = addquad;
        gvox->qcnt = 0;

        memset(by0,-1,(max(ysiz,zsiz)+1)<<2); v = 0;

        for (i=-1;i<=1;i+=2)
            for (y=0;y<ysiz;y++)
                for (x=0;x<=xsiz;x++)
                    for (z=0;z<=zsiz;z++)
                    {
                        ov = v; v = (isolid(x,y,z) && (!isolid(x,y+i,z)));
                        if ((by0[z] >= 0) && ((by0[z] != oz) || (v >= ov)))
                            { daquad(bx0[z],y,by0[z],x,y,by0[z],x,y,z,i>=0); by0[z] = -1; }
                        if (v > ov) oz = z; else if ((v < ov) && (by0[z] != oz)) { bx0[z] = x; by0[z] = oz; }
                    }

        for (i=-1;i<=1;i+=2)
            for (z=0;z<zsiz;z++)
                for (x=0;x<=xsiz;x++)
                    for (y=0;y<=ysiz;y++)
                    {
                        ov = v; v = (isolid(x,y,z) && (!isolid(x,y,z-i)));
                        if ((by0[y] >= 0) && ((by0[y] != oz) || (v >= ov)))
                            { daquad(bx0[y],by0[y],z,x,by0[y],z,x,y,z,(i>=0)+2); by0[y] = -1; }
                        if (v > ov) oz = y; else if ((v < ov) && (by0[y] != oz)) { bx0[y] = x; by0[y] = oz; }
                    }

        for (i=-1;i<=1;i+=2)
            for (x=0;x<xsiz;x++)
                for (y=0;y<=ysiz;y++)
                    for (z=0;z<=zsiz;z++)
                    {
                        ov = v; v = (isolid(x,y,z) && (!isolid(x-i,y,z)));
                        if ((by0[z] >= 0) && ((by0[z] != oz) || (v >= ov)))
                            { daquad(x,bx0[z],by0[z],x,y,by0[z],x,y,z,(i>=0)+4); by0[z] = -1; }
                        if (v > ov) oz = z; else if ((v < ov) && (by0[z] != oz)) { bx0[z] = y; by0[z] = oz; }
                    }

        if (!cnt)
        {
            shp = (spoint2d *)malloc(gvox->qcnt*sizeof(spoint2d));
            if (!shp) { free(bx0); free(gvox); return(0); }

            sc = 0;
            for (y=gmaxy;y;y--)
                for (x=gmaxx;x>=y;x--)
                {
                    i = shcnt[y*shcntp+x]; shcnt[y*shcntp+x] = sc; //shcnt changes from counter to head index
                    for (;i>0;i--) { shp[sc].x = x; shp[sc].y = y; sc++; }
                }

            for (gvox->mytexx=32;gvox->mytexx<(gmaxx+(VOXBORDWIDTH<<1));gvox->mytexx<<=1);
            for (gvox->mytexy=32;gvox->mytexy<(gmaxy+(VOXBORDWIDTH<<1));gvox->mytexy<<=1);
            while (gvox->mytexx*gvox->mytexy*8 < garea*9) //This should be sufficient to fit most skins...
            {
skindidntfit:
                ;
                if (gvox->mytexx <= gvox->mytexy) gvox->mytexx <<= 1; else gvox->mytexy <<= 1;
            }
            mytexo5 = (gvox->mytexx>>5);

            i = (((gvox->mytexx*gvox->mytexy+31)>>5)<<2);
            zbit = (int *)malloc(i); if (!zbit) { free(bx0); free(gvox); free(shp); return(0); }
            memset(zbit,0,i);

            v = gvox->mytexx*gvox->mytexy;
            for (z=0;z<sc;z++)
            {
                dx = shp[z].x+(VOXBORDWIDTH<<1); dy = shp[z].y+(VOXBORDWIDTH<<1); i = v;
                do
                {
#if (VOXUSECHAR != 0)
                    x0 = (((rand()&32767)*(min(gvox->mytexx,255)-dx))>>15);
                    y0 = (((rand()&32767)*(min(gvox->mytexy,255)-dy))>>15);
#else
                    x0 = (((rand()&32767)*(gvox->mytexx+1-dx))>>15);
                    y0 = (((rand()&32767)*(gvox->mytexy+1-dy))>>15);
#endif
                    i--;
                    if (i < 0) //Time-out! Very slow if this happens... but at least it still works :P
                    {
                        free(zbit);

                        //Re-generate shp[].x/y (box sizes) from shcnt (now head indices) for next pass :/
                        j = 0;
                        for (y=gmaxy;y;y--)
                            for (x=gmaxx;x>=y;x--)
                            {
                                i = shcnt[y*shcntp+x];
                                for (;j<i;j++) { shp[j].x = x0; shp[j].y = y0; }
                                x0 = x; y0 = y;
                            }
                        for (;j<sc;j++) { shp[j].x = x0; shp[j].y = y0; }

                        goto skindidntfit;
                    }
                }
                while (!isrectfree(x0,y0,dx,dy));
                while ((y0) && (isrectfree(x0,y0-1,dx,1))) y0--;
                while ((x0) && (isrectfree(x0-1,y0,1,dy))) x0--;
                setrect(x0,y0,dx,dy);
                shp[z].x = x0; shp[z].y = y0; //Overwrite size with top-left location
            }

            gvox->quad = (voxrect_t *)malloc(gvox->qcnt*sizeof(voxrect_t));
            if (!gvox->quad) { free(zbit); free(shp); free(bx0); free(gvox); return(0); }

            gvox->mytex = (int *)malloc(gvox->mytexx*gvox->mytexy*sizeof(int));
            if (!gvox->mytex) { free(gvox->quad); free(zbit); free(shp); free(bx0); free(gvox); return(0); }
        }
    }
    free(shp); free(zbit); free(bx0);
    return(gvox);
}

static int loadvox(const char *filnam)
{
    int i, j, k, x, y, z, pal[256], fil;
    unsigned char c[3], *tbuf;

    fil = kopen4load((char *)filnam,0); if (fil < 0) return(-1);
    kread(fil,&xsiz,4); xsiz = B_LITTLE32(xsiz);
    kread(fil,&ysiz,4); ysiz = B_LITTLE32(ysiz);
    kread(fil,&zsiz,4); zsiz = B_LITTLE32(zsiz);
    xpiv = ((float)xsiz)*.5;
    ypiv = ((float)ysiz)*.5;
    zpiv = ((float)zsiz)*.5;

    klseek(fil,-768,SEEK_END);
    for (i=0;i<256;i++)
        { kread(fil,c,3); pal[i] = (((int)c[0])<<18)+(((int)c[1])<<10)+(((int)c[2])<<2)+(i<<24); }
    pal[255] = -1;

    vcolhashsizm1 = 8192-1;
    vcolhashead = (int *)malloc((vcolhashsizm1+1)*sizeof(int)); if (!vcolhashead) { kclose(fil); return(-1); }
    memset(vcolhashead,-1,(vcolhashsizm1+1)*sizeof(int));

    yzsiz = ysiz*zsiz; i = ((xsiz*yzsiz+31)>>3);
    vbit = (int *)malloc(i); if (!vbit) { kclose(fil); return(-1); }
    memset(vbit,0,i);

    tbuf = (unsigned char *)malloc(zsiz*sizeof(char)); if (!tbuf) { kclose(fil); return(-1); }

    klseek(fil,12,SEEK_SET);
    for (x=0;x<xsiz;x++)
        for (y=0,j=x*yzsiz;y<ysiz;y++,j+=zsiz)
        {
            kread(fil,tbuf,zsiz);
            for (z=zsiz-1;z>=0;z--)
                { if (tbuf[z] != 255) { i = j+z; vbit[i>>5] |= (1<<SHIFTMOD32(i)); } }
        }

    klseek(fil,12,SEEK_SET);
    for (x=0;x<xsiz;x++)
        for (y=0,j=x*yzsiz;y<ysiz;y++,j+=zsiz)
        {
            kread(fil,tbuf,zsiz);
            for (z=0;z<zsiz;z++)
            {
                if (tbuf[z] == 255) continue;
                if ((!x) || (!y) || (!z) || (x == xsiz-1) || (y == ysiz-1) || (z == zsiz-1))
                    { putvox(x,y,z,pal[tbuf[z]]); continue; }
                k = j+z;
                if ((!(vbit[(k-yzsiz)>>5]&(1<<SHIFTMOD32(k-yzsiz)))) ||
                        (!(vbit[(k+yzsiz)>>5]&(1<<SHIFTMOD32(k+yzsiz)))) ||
                        (!(vbit[(k- zsiz)>>5]&(1<<SHIFTMOD32(k- zsiz)))) ||
                        (!(vbit[(k+ zsiz)>>5]&(1<<SHIFTMOD32(k+ zsiz)))) ||
                        (!(vbit[(k-    1)>>5]&(1<<SHIFTMOD32(k-    1)))) ||
                        (!(vbit[(k+    1)>>5]&(1<<SHIFTMOD32(k+    1)))))
                    { putvox(x,y,z,pal[tbuf[z]]); continue; }
            }
        }

    free(tbuf); kclose(fil); return(0);
}

static int loadkvx(const char *filnam)
{
    int i, j, k, x, y, z, pal[256], z0, z1, mip1leng, ysizp1, fil;
    unsigned short *xyoffs;
    unsigned char c[3], *tbuf, *cptr;

    fil = kopen4load((char *)filnam,0); if (fil < 0) return(-1);
    kread(fil,&mip1leng,4); mip1leng = B_LITTLE32(mip1leng);
    kread(fil,&xsiz,4);     xsiz = B_LITTLE32(xsiz);
    kread(fil,&ysiz,4);     ysiz = B_LITTLE32(ysiz);
    kread(fil,&zsiz,4);     zsiz = B_LITTLE32(zsiz);
    kread(fil,&i,4); xpiv = ((float)B_LITTLE32(i))/256.0;
    kread(fil,&i,4); ypiv = ((float)B_LITTLE32(i))/256.0;
    kread(fil,&i,4); zpiv = ((float)B_LITTLE32(i))/256.0;
    klseek(fil,(xsiz+1)<<2,SEEK_CUR);
    ysizp1 = ysiz+1;
    i = xsiz*ysizp1*sizeof(short);
    xyoffs = (unsigned short *)malloc(i); if (!xyoffs) { kclose(fil); return(-1); }
    kread(fil,xyoffs,i); for (i=i/sizeof(short)-1; i>=0; i--) xyoffs[i] = B_LITTLE16(xyoffs[i]);

    klseek(fil,-768,SEEK_END);
    for (i=0;i<256;i++)
        { kread(fil,c,3); pal[i] = B_LITTLE32((((int)c[0])<<18)+(((int)c[1])<<10)+(((int)c[2])<<2)+(i<<24)); }

    yzsiz = ysiz*zsiz; i = ((xsiz*yzsiz+31)>>3);
    vbit = (int *)malloc(i); if (!vbit) { free(xyoffs); kclose(fil); return(-1); }
    memset(vbit,0,i);

    for (vcolhashsizm1=4096;vcolhashsizm1<(mip1leng>>1);vcolhashsizm1<<=1); vcolhashsizm1--; //approx to numvoxs!
    vcolhashead = (int *)malloc((vcolhashsizm1+1)*sizeof(int)); if (!vcolhashead) { free(xyoffs); kclose(fil); return(-1); }
    memset(vcolhashead,-1,(vcolhashsizm1+1)*sizeof(int));

    klseek(fil,28+((xsiz+1)<<2)+((ysizp1*xsiz)<<1),SEEK_SET);

    i = kfilelength(fil)-ktell(fil);
    tbuf = (unsigned char *)malloc(i); if (!tbuf) { free(xyoffs); kclose(fil); return(-1); }
    kread(fil,tbuf,i); kclose(fil);

    cptr = tbuf;
    for (x=0;x<xsiz;x++) //Set surface voxels to 1 else 0
        for (y=0,j=x*yzsiz;y<ysiz;y++,j+=zsiz)
        {
            i = xyoffs[x*ysizp1+y+1] - xyoffs[x*ysizp1+y]; if (!i) continue;
            z1 = 0;
            while (i)
            {
                z0 = (int)cptr[0]; k = (int)cptr[1]; cptr += 3;
                if (!(cptr[-1]&16)) setzrange1(vbit,j+z1,j+z0);
                i -= k+3; z1 = z0+k;
                setzrange1(vbit,j+z0,j+z1);
                for (z=z0;z<z1;z++) putvox(x,y,z,pal[*cptr++]);
            }
        }

    free(tbuf); free(xyoffs); return(0);
}

static int loadkv6(const char *filnam)
{
    int i, j, x, y, numvoxs, z0, z1, fil;
    unsigned short *ylen;
    unsigned char c[8];

    fil = kopen4load((char *)filnam,0); if (fil < 0) return(-1);
    kread(fil,&i,4); if (B_LITTLE32(i) != 0x6c78764b) { kclose(fil); return(-1); } //Kvxl
    kread(fil,&xsiz,4);    xsiz = B_LITTLE32(xsiz);
    kread(fil,&ysiz,4);    ysiz = B_LITTLE32(ysiz);
    kread(fil,&zsiz,4);    zsiz = B_LITTLE32(zsiz);
    kread(fil,&i,4);       xpiv = (float)(B_LITTLE32(i));
    kread(fil,&i,4);       ypiv = (float)(B_LITTLE32(i));
    kread(fil,&i,4);       zpiv = (float)(B_LITTLE32(i));
    kread(fil,&numvoxs,4); numvoxs = B_LITTLE32(numvoxs);

    ylen = (unsigned short *)malloc(xsiz*ysiz*sizeof(short));
    if (!ylen) { kclose(fil); return(-1); }

    klseek(fil,32+(numvoxs<<3)+(xsiz<<2),SEEK_SET);
    kread(fil,ylen,xsiz*ysiz*sizeof(short)); for (i=xsiz*ysiz-1; i>=0; i--) ylen[i] = B_LITTLE16(ylen[i]);
    klseek(fil,32,SEEK_SET);

    yzsiz = ysiz*zsiz; i = ((xsiz*yzsiz+31)>>3);
    vbit = (int *)malloc(i); if (!vbit) { free(ylen); kclose(fil); return(-1); }
    memset(vbit,0,i);

    for (vcolhashsizm1=4096;vcolhashsizm1<numvoxs;vcolhashsizm1<<=1); vcolhashsizm1--;
    vcolhashead = (int *)malloc((vcolhashsizm1+1)*sizeof(int)); if (!vcolhashead) { free(ylen); kclose(fil); return(-1); }
    memset(vcolhashead,-1,(vcolhashsizm1+1)*sizeof(int));

    for (x=0;x<xsiz;x++)
        for (y=0,j=x*yzsiz;y<ysiz;y++,j+=zsiz)
        {
            z1 = zsiz;
            for (i=ylen[x*ysiz+y];i>0;i--)
            {
                kread(fil,c,8); //b,g,r,a,z_lo,z_hi,vis,dir
                z0 = B_LITTLE16(*(unsigned short *)&c[4]);
                if (!(c[6]&16)) setzrange1(vbit,j+z1,j+z0);
                vbit[(j+z0)>>5] |= (1<<SHIFTMOD32(j+z0));
                putvox(x,y,z0,B_LITTLE32(*(int *)&c[0])&0xffffff);
                z1 = z0+1;
            }
        }
    free(ylen); kclose(fil); return(0);
}

#if 0
//While this code works, it's way too slow and can only cause trouble.
static int loadvxl(const char *filnam)
{
    int i, j, x, y, z, fil;
    unsigned char *v, *vbuf;

    fil = kopen4load((char *)filnam,0); if (fil < 0) return(-1);
    kread(fil,&i,4);
    kread(fil,&xsiz,4);
    kread(fil,&ysiz,4);
    if ((i != 0x09072000) || (xsiz != 1024) || (ysiz != 1024)) { kclose(fil); return(-1); }
    zsiz = 256;
    klseek(fil,96,SEEK_CUR); //skip pos&orient
    xpiv = ((float)xsiz)*.5;
    ypiv = ((float)ysiz)*.5;
    zpiv = ((float)zsiz)*.5;

    yzsiz = ysiz*zsiz; i = ((xsiz*yzsiz+31)>>3);
    vbit = (int *)malloc(i); if (!vbit) { kclose(fil); return(-1); }
    memset(vbit,-1,i);

    vcolhashsizm1 = 1048576-1;
    vcolhashead = (int *)malloc((vcolhashsizm1+1)*sizeof(int)); if (!vcolhashead) { kclose(fil); return(-1); }
    memset(vcolhashead,-1,(vcolhashsizm1+1)*sizeof(int));

    //Allocate huge buffer and load rest of file into it...
    i = kfilelength(fil)-ktell(fil);
    vbuf = (unsigned char *)malloc(i); if (!vbuf) { kclose(fil); return(-1); }
    kread(fil,vbuf,i);
    kclose(fil);

    v = vbuf;
    for (y=0;y<ysiz;y++)
        for (x=0,j=y*zsiz;x<xsiz;x++,j+=yzsiz)
        {
            z = 0;
            while (1)
            {
                setzrange0(vbit,j+z,j+v[1]);
                for (z=v[1];z<=v[2];z++) putvox(x,y,z,(*(int *)&v[(z-v[1]+1)<<2])&0xffffff);
                if (!v[0]) break; z = v[2]-v[1]-v[0]+2; v += v[0]*4;
                for (z+=v[3];z<v[3];z++) putvox(x,y,z,(*(int *)&v[(z-v[3])<<2])&0xffffff);
            }
            v += ((((int)v[2])-((int)v[1])+2)<<2);
        }
    free(vbuf); return(0);
}
#endif

void voxfree(voxmodel *m)
{
    if (!m) return;
    if (m->mytex) free(m->mytex);
    if (m->quad) free(m->quad);
    if (m->texid) free(m->texid);
    free(m);
}

voxmodel *voxload(const char *filnam)
{
    int i, is8bit, ret;
    voxmodel *vm;

    i = strlen(filnam)-4; if (i < 0) return(0);
    if (!Bstrcasecmp(&filnam[i],".vox")) { ret = loadvox(filnam); is8bit = 1; }
    else if (!Bstrcasecmp(&filnam[i],".kvx")) { ret = loadkvx(filnam); is8bit = 1; }
    else if (!Bstrcasecmp(&filnam[i],".kv6")) { ret = loadkv6(filnam); is8bit = 0; }
    //else if (!Bstrcasecmp(&filnam[i],".vxl")) { ret = loadvxl(filnam); is8bit = 0; }
    else return(0);
    if (ret >= 0) vm = vox2poly(); else vm = 0;
    if (vm)
    {
        vm->mdnum = 1; //VOXel model id
        vm->scale = vm->bscale = 1.5;
        vm->xsiz = xsiz; vm->ysiz = ysiz; vm->zsiz = zsiz;
        vm->xpiv = xpiv; vm->ypiv = ypiv; vm->zpiv = zpiv;
        vm->is8bit = is8bit;

        vm->texid = (unsigned int *)calloc(MAXPALOOKUPS,sizeof(unsigned int));
        if (!vm->texid) { voxfree(vm); vm = 0; }
    }
    if (shcntmal) { free(shcntmal); shcntmal = 0; }
    if (vbit) { free(vbit); vbit = 0; }
    if (vcol) { free(vcol); vcol = 0; vnum = 0; vmax = 0; }
    if (vcolhashead) { free(vcolhashead); vcolhashead = 0; }
    return(vm);
}

//Draw voxel model as perfect cubes
int voxdraw(voxmodel *m, spritetype *tspr)
{
    point3d fp, m0, a0;
    int i, j, fi, xx, yy, zz;
    float ru, rv, phack[2], clut[6] = {1,1,1,1,1,1}; //1.02,1.02,0.94,1.06,0.98,0.98};
    float f, g, k0, k1, k2, k3, k4, k5, k6, k7, mat[16], omat[16], pc[4];
    vert_t *vptr;

    if ((intptr_t)m == (intptr_t)(-1)) // hackhackhack
        return 0;
    if ((tspr->cstat&48)==32) return 0;

    //updateanimation((md2model *)m,tspr);

    m0.x = m->scale;
    m0.y = m->scale;
    m0.z = m->scale;
    a0.x = a0.y = 0; a0.z = ((globalorientation&8)?-m->zadd:m->zadd)*m->scale;

    //if (globalorientation&8) //y-flipping
    //{
    //   m0.z = -m0.z; a0.z = -a0.z;
    //      //Add height of 1st frame (use same frame to prevent animation bounce)
    //   a0.z += m->zsiz*m->scale;
    //}
    //if (globalorientation&4) { m0.y = -m0.y; a0.y = -a0.y; } //x-flipping

    f = ((float)tspr->xrepeat)*(256.0/320.0)/64.0*m->bscale;
    m0.x *= f; a0.x *= f; f = -f;
    m0.y *= f; a0.y *= f;
    f = ((float)tspr->yrepeat)/64.0*m->bscale;
    m0.z *= f; a0.z *= f;

    k0 = tspr->z;
    if (globalorientation&128) k0 += (float)((tilesizy[tspr->picnum]*tspr->yrepeat)<<1);

    f = (65536.0*512.0)/((float)xdimen*viewingrange);
    g = 32.0/((float)xdimen*gxyaspect);
    m0.y *= f; a0.y = (((float)(tspr->x-globalposx))/  1024.0 + a0.y)*f;
    m0.x *=-f; a0.x = (((float)(tspr->y-globalposy))/ -1024.0 + a0.x)*-f;
    m0.z *= g; a0.z = (((float)(k0     -globalposz))/-16384.0 + a0.z)*g;

    k0 = ((float)(tspr->x-globalposx))*f/1024.0;
    k1 = ((float)(tspr->y-globalposy))*f/1024.0;
    f = gcosang2*gshang;
    g = gsinang2*gshang;
    k4 = (float)sintable[(tspr->ang+spriteext[tspr->owner].angoff+1024)&2047] / 16384.0;
    k5 = (float)sintable[(tspr->ang+spriteext[tspr->owner].angoff+ 512)&2047] / 16384.0;
    k2 = k0*(1-k4)+k1*k5;
    k3 = k1*(1-k4)-k0*k5;
    k6 = f*gstang - gsinang*gctang; k7 = g*gstang + gcosang*gctang;
    mat[0] = k4*k6 + k5*k7; mat[4] = gchang*gstang; mat[ 8] = k4*k7 - k5*k6; mat[12] = k2*k6 + k3*k7;
    k6 = f*gctang + gsinang*gstang; k7 = g*gctang - gcosang*gstang;
    mat[1] = k4*k6 + k5*k7; mat[5] = gchang*gctang; mat[ 9] = k4*k7 - k5*k6; mat[13] = k2*k6 + k3*k7;
    k6 =           gcosang2*gchang; k7 =           gsinang2*gchang;
    mat[2] = k4*k6 + k5*k7; mat[6] =-gshang;        mat[10] = k4*k7 - k5*k6; mat[14] = k2*k6 + k3*k7;

    mat[12] += a0.y*mat[0] + a0.z*mat[4] + a0.x*mat[ 8];
    mat[13] += a0.y*mat[1] + a0.z*mat[5] + a0.x*mat[ 9];
    mat[14] += a0.y*mat[2] + a0.z*mat[6] + a0.x*mat[10];

    //Mirrors
    if (grhalfxdown10x < 0) { mat[0] = -mat[0]; mat[4] = -mat[4]; mat[8] = -mat[8]; mat[12] = -mat[12]; }

    //------------
    //bit 10 is an ugly hack in game.c\animatesprites telling MD2SPRITE
    //to use Z-buffer hacks to hide overdraw problems with the shadows
    if (tspr->cstat&1024)
    {
        bglDepthFunc(GL_LESS); //NEVER,LESS,(,L)EQUAL,GREATER,(NOT,G)EQUAL,ALWAYS
        bglDepthRange(0.0,0.9999);
    }
    bglPushAttrib(GL_POLYGON_BIT);
    if ((grhalfxdown10x >= 0) /*^ ((globalorientation&8) != 0) ^ ((globalorientation&4) != 0)*/) bglFrontFace(GL_CW); else bglFrontFace(GL_CCW);
    bglEnable(GL_CULL_FACE);
    bglCullFace(GL_BACK);

    bglEnable(GL_TEXTURE_2D);

    pc[0] = pc[1] = pc[2] = ((float)(numpalookups-min(max((globalshade * shadescale)+m->shadeoff,0),numpalookups)))/((float)numpalookups);
    pc[0] *= (float)hictinting[globalpal].r / 255.0;
    pc[1] *= (float)hictinting[globalpal].g / 255.0;
    pc[2] *= (float)hictinting[globalpal].b / 255.0;
    if (tspr->cstat&2) { if (!(tspr->cstat&512)) pc[3] = 0.66; else pc[3] = 0.33; }
    else pc[3] = 1.0;
    if (tspr->cstat&2 && (!peelcompiling)) bglEnable(GL_BLEND); //else bglDisable(GL_BLEND);
    //------------

    //transform to Build coords
    memcpy(omat,mat,sizeof(omat));
    f = 1.f/64.f;
    g = m0.x*f; mat[0] *= g; mat[1] *= g; mat[2] *= g;
    g = m0.y*f; mat[4] = omat[8]*g; mat[5] = omat[9]*g; mat[6] = omat[10]*g;
    g =-m0.z*f; mat[8] = omat[4]*g; mat[9] = omat[5]*g; mat[10] = omat[6]*g;
    mat[12] -= (m->xpiv*mat[0] + m->ypiv*mat[4] + (m->zpiv+m->zsiz*.5)*mat[ 8]);
    mat[13] -= (m->xpiv*mat[1] + m->ypiv*mat[5] + (m->zpiv+m->zsiz*.5)*mat[ 9]);
    mat[14] -= (m->xpiv*mat[2] + m->ypiv*mat[6] + (m->zpiv+m->zsiz*.5)*mat[10]);
    bglMatrixMode(GL_MODELVIEW); //Let OpenGL (and perhaps hardware :) handle the matrix rotation
    mat[3] = mat[7] = mat[11] = 0.f; mat[15] = 1.f;

    bglLoadMatrixf(mat);

    ru = 1.f/((float)m->mytexx);
    rv = 1.f/((float)m->mytexy);
#if (VOXBORDWIDTH == 0)
    uhack[0] = ru*.125; uhack[1] = -uhack[0];
    vhack[0] = rv*.125; vhack[1] = -vhack[0];
#endif
    phack[0] = 0; phack[1] = 1.f/256.f;

    if (!m->texid[globalpal]) m->texid[globalpal] = gloadtex(m->mytex,m->mytexx,m->mytexy,m->is8bit,globalpal);
    else bglBindTexture(GL_TEXTURE_2D,m->texid[globalpal]);
    bglBegin(GL_QUADS);
    for (i=0,fi=0;i<m->qcnt;i++)
    {
        if (i == m->qfacind[fi]) { f = clut[fi++]; bglColor4f(pc[0]*f,pc[1]*f,pc[2]*f,pc[3]*f); }
        vptr = &m->quad[i].v[0];

        xx = vptr[0].x+vptr[2].x;
        yy = vptr[0].y+vptr[2].y;
        zz = vptr[0].z+vptr[2].z;

        for (j=0;j<4;j++)
        {
#if (VOXBORDWIDTH == 0)
            bglTexCoord2f(((float)vptr[j].u)*ru+uhack[vptr[j].u!=vptr[0].u],
                          ((float)vptr[j].v)*rv+vhack[vptr[j].v!=vptr[0].v]);
#else
            bglTexCoord2f(((float)vptr[j].u)*ru,((float)vptr[j].v)*rv);
#endif
            fp.x = ((float)vptr[j].x) - phack[xx>vptr[j].x*2] + phack[xx<vptr[j].x*2];
            fp.y = ((float)vptr[j].y) - phack[yy>vptr[j].y*2] + phack[yy<vptr[j].y*2];
            fp.z = ((float)vptr[j].z) - phack[zz>vptr[j].z*2] + phack[zz<vptr[j].z*2];
            bglVertex3fv((float *)&fp);
        }
    }
    bglEnd();

    //------------
    bglDisable(GL_CULL_FACE);
    bglPopAttrib();
    if (tspr->cstat&1024)
    {
        bglDepthFunc(GL_LESS); //NEVER,LESS,(,L)EQUAL,GREATER,(NOT,G)EQUAL,ALWAYS
        bglDepthRange(0.0,0.99999);
    }
    bglLoadIdentity();
    return 1;
}

//---------------------------------------- VOX LIBRARY ENDS ----------------------------------------
//--------------------------------------- MD LIBRARY BEGINS  ---------------------------------------

mdmodel *mdload(const char *filnam)
{
    mdmodel *vm;
    int fil;
    int i;

    vm = (mdmodel*)voxload(filnam); if (vm) return(vm);

    fil = kopen4load((char *)filnam,0); if (fil < 0) return(0);
    kread(fil,&i,4); klseek(fil,0,SEEK_SET);
    switch (B_LITTLE32(i))
    {
    case 0x32504449:
//        initprintf("Warning: model '%s' is version IDP2; wanted version IDP3\n",filnam);
        vm = (mdmodel*)md2load(fil,filnam); break; //IDP2
    case 0x33504449:
        vm = (mdmodel*)md3load(fil); break; //IDP3
    default:
        vm = (mdmodel*)0; break;
    }
    kclose(fil);
    return(vm);
}

int mddraw(spritetype *tspr)
{
    mdmodel *vm;
    int i;

    if (r_vbos && (r_vbocount > allocvbos))
    {
        indexvbos = realloc(indexvbos, sizeof(GLuint) * r_vbocount);
        vertvbos = realloc(vertvbos, sizeof(GLuint) * r_vbocount);

        bglGenBuffersARB(r_vbocount - allocvbos, &(indexvbos[allocvbos]));
        bglGenBuffersARB(r_vbocount - allocvbos, &(vertvbos[allocvbos]));

        i = allocvbos;
        while (i < r_vbocount)
        {
            bglBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, indexvbos[i]);
            bglBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB, maxmodeltris * 3 * sizeof(unsigned short), NULL, GL_STREAM_DRAW_ARB);
            bglBindBufferARB(GL_ARRAY_BUFFER_ARB, vertvbos[i]);
            bglBufferDataARB(GL_ARRAY_BUFFER_ARB, maxmodelverts * sizeof(point3d), NULL, GL_STREAM_DRAW_ARB);
            i++;
        }

        bglBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
        bglBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);

        allocvbos = r_vbocount;
    }

    if (maxmodelverts > allocmodelverts)
    {
        point3d *vl = (point3d *)realloc(vertlist,sizeof(point3d)*maxmodelverts);
        if (!vl) { OSD_Printf("ERROR: Not enough memory to allocate %d vertices!\n",maxmodelverts); return 0; }
        vertlist = vl;
        allocmodelverts = maxmodelverts;
    }

    vm = models[tile2model[Ptile2tile(tspr->picnum,(tspr->owner >= MAXSPRITES) ? tspr->pal : sprite[tspr->owner].pal)].modelid];
    if (vm->mdnum == 1) { return voxdraw((voxmodel *)vm,tspr); }
    if (vm->mdnum == 3) { return md3draw((md3model *)vm,tspr); }
    return 0;
}

void mdfree(mdmodel *vm)
{
    if (vm->mdnum == 1) { voxfree((voxmodel *)vm); return; }
    if (vm->mdnum == 3) { md3free((md3model *)vm); return; }
}

#endif

//---------------------------------------- MD LIBRARY ENDS  ----------------------------------------
