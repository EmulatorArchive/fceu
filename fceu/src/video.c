/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Xodnizel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <zlib.h>

#ifdef _USE_SHARED_MEMORY_
#include <windows.h>
#endif

#include "types.h"
#include "video.h"
#include "fceu.h"
#include "general.h"
#include "memory.h"
#include "crc32.h"
#include "state.h"
#include "movie.h"
#include "palette.h"
#include "nsf.h"
#include "input.h"
#include "vsuni.h"
#include "fceulua.h"

uint8 *XBuf=NULL;
uint8 *XBackBuf=NULL;
static uint8 *xbsave=NULL;

//for input display
extern int input_display;
extern uint32 cur_input_display;

int DontStopMovieAVIMessage;
int ShowMovieAVIMessage;

#ifdef _USE_SHARED_MEMORY_
HANDLE mapXBuf;
#endif

void FCEU_KillVirtualVideo(void)
{
 if(xbsave)
 {
  free(xbsave);
  xbsave=0;
 }
 #ifdef _USE_SHARED_MEMORY_
  if(XBuf)
  {
   UnmapViewOfFile(XBuf);
   CloseHandle(mapXBuf);
   mapXBuf=NULL;
  }
 #endif
 if(XBackBuf)
 {
  free(XBackBuf);
  XBackBuf=0;
 }
}

int FCEU_InitVirtualVideo(void)
{
 if(!XBuf)		/* Some driver code may allocate XBuf externally. */
			/* 256 bytes per scanline, * 240 scanline maximum, +8 for alignment,
			*/
#ifdef _USE_SHARED_MEMORY_
	mapXBuf  = CreateFileMapping((HANDLE)0xFFFFFFFF,NULL,PAGE_READWRITE, 0, 256 * 256 + 8,"fceu.XBuf");
	if(mapXBuf == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
	{
		CloseHandle(mapXBuf);
		mapXBuf = NULL;
		XBuf= (uint8*) (FCEU_malloc(256 * 256 + 8));
		XBackBuf= (uint8*) (FCEU_malloc(256 * 256 + 8));
	}
	else
	{
		XBuf     = (uint8 *)MapViewOfFile(mapXBuf, FILE_MAP_WRITE, 0, 0, 0);

		XBackBuf = (uint8*) (FCEU_malloc(256 * 256 + 8));
	}

	if (!XBuf || !XBackBuf)
		return 0;
#else
 if(!(XBuf= (uint8*) (FCEU_malloc(256 * 256 + 8))) ||
    !(XBackBuf= (uint8*) (FCEU_malloc(256 * 256 + 8))))
  return 0;
#endif //_USE_SHARED_MEMORY_
 xbsave=XBuf;

 if(sizeof(uint8*)==4)
 {
  uint32 m;
  m=(uint32)XBuf;
  m=(4-m)&3;
  XBuf+=m;
 }
 memset(XBuf,128,256*256); //*240);
 memset(XBackBuf,128,256*256);
 return 1;
}

int howlong;
static char errmsg[86];

#include "drawing.h"
#ifdef FRAMESKIP

//#define SHOWFPS
void ShowFPS(void);
void FCEU_PutImageDummy(void)
{
 #ifdef SHOWFPS
 ShowFPS();
 #endif
 if(FCEUGameInfo->type!=GIT_NSF)
 {
  FCEU_DrawNTSCControlBars(XBuf);
  FCEU_DrawSaveStates(XBuf);
  FCEU_DrawMovies(XBuf);
 }
 if(howlong) howlong--; /* DrawMessage() */
}
#endif

static int dosnapsave=0;
void FCEUI_SaveSnapshot(void)
{
 dosnapsave=1;
}
 
static void FCEU_DrawRecordingStatus(uint8* XBuf)
{
	int n=0;
	if(FCEUD_ShowStatusIcon())
	{
		if(FCEUI_EmulationPaused())
			n=3;
		else if(FCEUI_IsMovieActive()>0)
			n=2;
		else if(FCEUI_IsMovieActive()<0)
			n=1;
	}

	if(n)
		FCEU_DrawRecordingStatusN(XBuf, n);
}

static void ReallySnap(void)
{
 int x=SaveSnapshot();
 if(!x)
  FCEU_DispMessage("Error saving screen snapshot.");
 else
  FCEU_DispMessage("Screen snapshot %d saved.",x-1);
}

void FCEU_PutImage(void)
{
	#ifdef SHOWFPS
	ShowFPS();
	#endif
/*
		 // HACK to record input and/or frame counter into AVI
		 extern int frame_display, input_display, movcounter;
		 int movieDrawInAVI = FCEUI_AviIsRecording() && (frame_display || input_display) && movcounter;
		 if(movieDrawInAVI)
		 {
			FCEU_DrawMovies(XBuf);
			DrawMessage();
		 }
*/
        // HACK to record movie messages into AVI
				if (ShowMovieAVIMessage)
				{
					if (DontStopMovieAVIMessage && FCEUI_AviIsRecording()) DrawMessage();
					if (FCEUI_EmulationPaused()==0) ShowMovieAVIMessage--;
				}
        if(FCEUGameInfo->type==GIT_NSF)
        {
         DrawNSF(XBuf);
         /* Save snapshot after NSF screen is drawn.  Why would we want to
            do it before?
         */
         if(dosnapsave)
         {
          ReallySnap();
          dosnapsave=0;
         }
        } 
        else
        {
         /* Update AVI before overlay stuff is written */
         if(!FCEUI_EmulationPaused())
          FCEUI_AviVideoUpdate(XBuf);
         /* Save backbuffer before overlay stuff is written. */
         if(!FCEUI_EmulationPaused())
          memcpy(XBackBuf, XBuf, 256*256);
         /* Save snapshot before overlay stuff is written. */
         if(dosnapsave)
         {
          ReallySnap();
          dosnapsave=0;
         }
         if(FCEUGameInfo->type==GIT_VSUNI)
          FCEU_VSUniDraw(XBuf);
		 FCEU_DrawSaveStates(XBuf);
		 FCEU_DrawMovies(XBuf);
         FCEU_DrawNTSCControlBars(XBuf);
		 FCEU_DrawRecordingStatus(XBuf);
        }
//		if(!movieDrawInAVI)
	        DrawMessage();
        FCEU_DrawInput(XBuf);
        FCEU_LuaGui(XBuf);

		//Fancy input display code
		if(input_display)
		{
			int controller, c, color;
			int i, j;
			char * t=XBuf+(FSettings.LastSLine-9)*256 + 20;
			if(input_display > 4) input_display = 4;
			for(controller = 0; controller < input_display; controller++, t += 56)
			{
				for(i = 0; i < 34;i++)
					for(j = 0; j <9 ; j++)
						t[i+j*256] = (t[i+j*256] & 15) | 0xC0;
				for(i = 3; i < 6; i++)
					for(j = 3; j< 6; j++)
						t[i+j*256] = 3;
				c = cur_input_display >> (controller * 8);
				c &= 255;
				//A
				color = c&1?4:3;
				for(i=0; i < 4; i++)
				{
					for(j = 0; j < 4; j++)
					{
						if(i%3==0 && j %3 == 0)
							continue;
						t[30+4*256+i+j*256] = color;
					}
				}
				//B
				color = c&2?4:3;
				for(i=0; i < 4; i++)
				{
					for(j = 0; j < 4; j++)
					{
						if(i%3==0 && j %3 == 0)
							continue;
						t[24+4*256+i+j*256] = color;
					}
				}
				//Select
				color = c&4?4:3;
				for(i = 0; i < 4; i++)
				{
					t[11+5*256+i] = color;
					t[11+6*256+i] = color;
				}
				//Start
				color = c&8?4:3;
				for(i = 0; i < 4; i++)
				{
					t[17+5*256+i] = color;
					t[17+6*256+i] = color;
				}
				//Up
				color = c&16?4:3;
				for(i = 0; i < 3; i++)
				{
					for(j = 0; j < 3; j++)
					{
						t[3+i+256*j] = color;
					}
				}
				//Down
				color = c&32?4:3;
				for(i = 0; i < 3; i++)
				{
					for(j = 0; j < 3; j++)
					{
						t[3+i+256*j+6*256] = color;
					}
				}
				//Left
				color = c&64?4:3;
				for(i = 0; i < 3; i++)
				{
					for(j = 0; j < 3; j++)
					{
						t[3*256+i+256*j] = color;
					}
				}
				//Right
				color = c&128?4:3;
				for(i = 0; i < 3; i++)
				{
					for(j = 0; j < 3; j++)
					{
						t[6+3*256+i+256*j] = color;
					}
				}
			}
		}
}

void FCEU_DispMessage(char *format, ...)
{
 va_list ap;

 va_start(ap,format);
 vsprintf(errmsg,format,ap);
 va_end(ap);

 howlong=180;

 extern int movcounter;
 movcounter=0;
}

void FCEU_ResetMessages(void)
{
 howlong=0;
}


static int WritePNGChunk(FILE *fp, uint32 size, char *type, uint8 *data)
{
 uint32 crc;

 uint8 tempo[4];

 tempo[0]=size>>24;
 tempo[1]=size>>16;
 tempo[2]=size>>8;
 tempo[3]=size;

 if(fwrite(tempo,4,1,fp)!=1)
  return 0;
 if(fwrite(type,4,1,fp)!=1)
  return 0;

 if(size)
  if(fwrite(data,1,size,fp)!=size)
   return 0;

 crc=CalcCRC32(0,(uint8 *)type,4);
 if(size)
  crc=CalcCRC32(crc,data,size);

 tempo[0]=crc>>24;
 tempo[1]=crc>>16;
 tempo[2]=crc>>8;
 tempo[3]=crc;

 if(fwrite(tempo,4,1,fp)!=1)
  return 0;
 return 1;
}

int SaveSnapshot(void)
{
 static unsigned int lastu=0;

 char *fn=0;
 int totallines=FSettings.LastSLine-FSettings.FirstSLine+1;
 int x,u,y;
 FILE *pp=NULL;
 uint8 *compmem=NULL;
 uLongf compmemsize=totallines*263+12;

 if(!(compmem=(uint8 *)FCEU_malloc(compmemsize)))
  return 0;

 for(u=lastu;u<99999;u++)
 {
  pp=FCEUD_UTF8fopen((fn=FCEU_MakeFName(FCEUMKF_SNAP,u,"png")),"rb");
  if(pp==NULL) break;
  fclose(pp);
 }

 lastu=u;

 if(!(pp=FCEUD_UTF8fopen(fn,"wb")))
 {
  free(fn);
  return 0;
 }
 free(fn);
 {
  static uint8 header[8]={137,80,78,71,13,10,26,10};
  if(fwrite(header,8,1,pp)!=1)
   goto PNGerr;
 }

 {
  uint8 chunko[13];

  chunko[0]=chunko[1]=chunko[3]=0;
  chunko[2]=0x1;      // Width of 256

  chunko[4]=chunko[5]=chunko[6]=0;
  chunko[7]=totallines;      // Height

  chunko[8]=8;        // bit depth
  chunko[9]=3;        // Color type; indexed 8-bit
  chunko[10]=0;        // compression: deflate
  chunko[11]=0;        // Basic adapative filter set(though none are used).
  chunko[12]=0;        // No interlace.

  if(!WritePNGChunk(pp,13,"IHDR",chunko))
   goto PNGerr;
 }

 {
  uint8 pdata[256*3];
  for(x=0;x<256;x++)
   FCEUD_GetPalette(x,pdata+x*3,pdata+x*3+1,pdata+x*3+2);
  if(!WritePNGChunk(pp,256*3,"PLTE",pdata))
   goto PNGerr;
 }

 {
  uint8 *tmp=XBuf+FSettings.FirstSLine*256;
  uint8 *dest,*mal,*mork;

  if(!(mal=mork=dest=(uint8 *)malloc((totallines<<8)+totallines)))
   goto PNGerr;
 //   mork=dest=XBuf;

  for(y=0;y<totallines;y++)
  {
   *dest=0;      // No filter.
   dest++;
   for(x=256;x;x--,tmp++,dest++)
    *dest=*tmp;
  }

  if(compress(compmem,&compmemsize,mork,(totallines<<8)+totallines)!=Z_OK)
  {
   if(mal) free(mal);
   goto PNGerr;
  }
  if(mal) free(mal);
  if(!WritePNGChunk(pp,compmemsize,"IDAT",compmem))
   goto PNGerr;
 }
 if(!WritePNGChunk(pp,0,"IEND",0))
  goto PNGerr;

 free(compmem);
 fclose(pp);

 return u+1;


 PNGerr:
 if(compmem)
  free(compmem);
 if(pp)
  fclose(pp);
 return(0);
}
#ifdef SHOWFPS
uint64 FCEUD_GetTime(void);
uint64 FCEUD_GetTimeFreq(void);

static uint64 boop[60];
static int boopcount = 0;

void ShowFPS(void)
{
 uint64 da = FCEUD_GetTime() - boop[boopcount];
 char fpsmsg[16];
 int booplimit = PAL?50:60;
 boop[boopcount] = FCEUD_GetTime();

 sprintf(fpsmsg, "%8.1f",(double)booplimit / ((double)da / FCEUD_GetTimeFreq()));
 DrawTextTrans(XBuf + (256-8-8*8) + (FSettings.FirstSLine+4)*256,256,fpsmsg,4);
 // It's not averaging FPS over exactly 1 second, but it's close enough.
 boopcount = (boopcount + 1) % booplimit;
}
#endif

void FCEUAVI_ShowMovieAVIMessage(int opt) {
	DontStopMovieAVIMessage = opt;
}
