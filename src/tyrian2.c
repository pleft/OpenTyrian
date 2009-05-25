/* 
 * OpenTyrian Classic: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "animlib.h"
#include "backgrnd.h"
#include "destruct.h"
#include "episodes.h"
#include "error.h"
#include "fonthand.h"
#include "game_menu.h"
#include "joystick.h"
#include "keyboard.h"
#include "lds_play.h"
#include "loudness.h"
#include "lvllib.h"
#include "menus.h"
#include "mainint.h"
#include "mtrand.h"
#include "network.h"
#include "newshape.h"
#include "nortsong.h"
#include "opentyr.h"
#include "params.h"
#include "pcxload.h"
#include "pcxmast.h"
#include "picload.h"
#include "setup.h"
#include "tyrian2.h"
#include "vga256d.h"
#include "video.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

void blit_enemy( SDL_Surface *surface, unsigned int i, signed int x_offset, signed int y_offset, signed int sprite_offset );

void intro_logos( void );

bool skip_intro_logos = false;

boss_bar_t boss_bar[2];

/* Level Event Data */
JE_boolean quit, first, loadLevelOk;

struct JE_EventRecType eventRec[EVENT_MAXIMUM]; /* [1..eventMaximum] */
JE_word levelEnemyMax;
JE_word levelEnemyFrequency;
JE_word levelEnemy[40]; /* [1..40] */

char tempStr[31];

/* Data used for ItemScreen procedure to indicate items available */
JE_byte itemAvail[9][10]; /* [1..9, 1..10] */
JE_byte itemAvailMax[9]; /* [1..9] */

//const JE_byte weaponReset[7] = { 0, 1, 2, 0, 0, 3, 4 };

void JE_starShowVGA( void )
{
	JE_byte *src;
	Uint8 *s = NULL; /* screen pointer, 8-bit specific */

	int x, y, lightx, lighty, lightdist;
	
	if (!playerEndLevel && !skipStarShowVGA)
	{
		
		s = VGAScreenSeg->pixels;
		
		src = game_screen->pixels;
		src += 24;
		
		if (smoothScroll != 0 /*&& thisPlayerNum != 2*/)
		{
			wait_delay();
			setjasondelay(frameCountMax);
		}
		
		if (starShowVGASpecialCode == 1)
		{
			src += game_screen->pitch * 183;
			for (y = 0; y < 184; y++)
			{
				memmove(s, src, 264);
				s += VGAScreenSeg->pitch;
				src -= game_screen->pitch;
			}
		}
		else if (starShowVGASpecialCode == 2 && processorType >= 2)
		{
			lighty = 172 - PY;
			lightx = 281 - PX;
			
			for (y = 184; y; y--)
			{
				if (lighty > y)
				{
					for (x = 320 - 56; x; x--)
					{
						*s = (*src & 0xf0) | ((*src >> 2) & 0x03);
						s++;
						src++;
					}
				}
				else
				{
					for (x = 320 - 56; x; x--)
					{
						lightdist = abs(lightx - x) + lighty;
						if (lightdist < y)
							*s = *src;
						else if (lightdist - y <= 5)
							*s = (*src & 0xf0) | (((*src & 0x0f) + (3 * (5 - (lightdist - y)))) / 4);
						else
							*s = (*src & 0xf0) | ((*src & 0x0f) >> 2);
						s++;
						src++;
					}
				}
				s += 56 + VGAScreenSeg->pitch - 320;
				src += 56 + VGAScreenSeg->pitch - 320;
			}
		}
		else
		{
			for (y = 0; y < 184; y++)
			{
				memmove(s, src, 264);
				s += VGAScreenSeg->pitch;
				src += game_screen->pitch;
			}
		}
		JE_showVGA();
	}
	
	quitRequested = false;
	skipStarShowVGA = false;
}

void blit_enemy( SDL_Surface *surface, unsigned int i, signed int x_offset, signed int y_offset, signed int sprite_offset )
{
	Uint8 *p; /* shape pointer */
	Uint8 *s; /* screen pointer, 8-bit specific */
	Uint8 *s_limit; /* buffer boundary */
	
	s = (Uint8 *)surface->pixels;
	s += (enemy[i].ey + y_offset) * surface->pitch + (enemy[i].ex + x_offset) + tempMapXOfs;
	
	s_limit = (Uint8 *)surface->pixels;
	s_limit += surface->h * surface->pitch;
	
	p = enemy[i].shapeseg;
	p += SDL_SwapLE16(((Uint16 *)p)[enemy[i].egr[enemy[i].enemycycle - 1] + sprite_offset]);
	
	while (*p != 0x0f)
	{
		s += *p & 0x0f;
		int count = (*p & 0xf0) >> 4;
		if (count)
		{
			while (count--)
			{
				p++;
				if (s >= s_limit)
					return;
				if ((void *)s >= surface->pixels)
					*s = (enemy[i].filter == 0) ? *p : (*p & 0x0f) | enemy[i].filter;
				s++;
			}
		}
		else
		{
			s -= 12;
			s += surface->pitch;
		}
		p++;
	}
}

void JE_drawEnemy( int enemyOffset ) // actually does a whole lot more than just drawing
{
	PX -= 25;
	
	for (int i = enemyOffset - 25; i < enemyOffset; i++)
	{
		if (enemyAvail[i] != 1)
		{
			enemy[i].mapoffset = tempMapXOfs;
			
			if (enemy[i].xaccel && enemy[i].xaccel - 89 > mt_rand() % 11)
			{
				if (PX > enemy[i].ex)
				{
					if (enemy[i].exc < enemy[i].xaccel - 89)
						enemy[i].exc++;
				}
				else
				{
					if (enemy[i].exc >= 0 || -enemy[i].exc < enemy[i].xaccel - 89)
						enemy[i].exc--;
				}
			}
			
			if (enemy[i].yaccel && enemy[i].yaccel - 89 > mt_rand() % 11)
			{
				if (PY > enemy[i].ey)
				{
					if (enemy[i].eyc < enemy[i].yaccel - 89)
						enemy[i].eyc++;
				}
				else
				{
					if (enemy[i].eyc >= 0 || -enemy[i].eyc < enemy[i].yaccel - 89)
						enemy[i].eyc--;
				}
			}
			
 			if (enemy[i].ex + tempMapXOfs > -29 && enemy[i].ex + tempMapXOfs < 300)
			{
				if (enemy[i].aniactive == 1)
				{
					enemy[i].enemycycle++;
					
					if (enemy[i].enemycycle == enemy[i].animax)
						enemy[i].aniactive = enemy[i].aniwhenfire;
					else if (enemy[i].enemycycle > enemy[i].ani)
						enemy[i].enemycycle = enemy[i].animin;
				}
				
				if (enemy[i].egr[enemy[i].enemycycle - 1] == 999)
					goto enemy_gone;
				
				if (enemy[i].size == 1) // 2x2 enemy
				{
					if (enemy[i].ey > -13)
					{
						blit_enemy(VGAScreen, i, -6, -7, -1);
						blit_enemy(VGAScreen, i,  6, -7,  0);
					}
					if (enemy[i].ey > -26 && enemy[i].ey < 182)
					{
						blit_enemy(VGAScreen, i, -6,  7, 18);
						blit_enemy(VGAScreen, i,  6,  7, 19);
					}
				}
				else
				{
					if (enemy[i].ey > -13)
						blit_enemy(VGAScreen, i, 0, 0, -1);
				}
				
				enemy[i].filter = 0;
			}
			
			if (enemy[i].excc)
			{
				if (--enemy[i].exccw <= 0)
				{
					if (enemy[i].exc == enemy[i].exrev)
					{
						enemy[i].excc = -enemy[i].excc;
						enemy[i].exrev = -enemy[i].exrev;
						enemy[i].exccadd = -enemy[i].exccadd;
					}
					else
					{
						enemy[i].exc += enemy[i].exccadd;
						enemy[i].exccw = enemy[i].exccwmax;
						if (enemy[i].exc == enemy[i].exrev)
						{
							enemy[i].excc = -enemy[i].excc;
							enemy[i].exrev = -enemy[i].exrev;
							enemy[i].exccadd = -enemy[i].exccadd;
						}
					}
				}
			}
			
			if (enemy[i].eycc)
			{
				if (--enemy[i].eyccw <= 0)
				{
					if (enemy[i].eyc == enemy[i].eyrev)
					{
						enemy[i].eycc = -enemy[i].eycc;
						enemy[i].eyrev = -enemy[i].eyrev;
						enemy[i].eyccadd = -enemy[i].eyccadd;
					}
					else
					{
						enemy[i].eyc += enemy[i].eyccadd;
						enemy[i].eyccw = enemy[i].eyccwmax;
						if (enemy[i].eyc == enemy[i].eyrev)
						{
							enemy[i].eycc = -enemy[i].eycc;
							enemy[i].eyrev = -enemy[i].eyrev;
							enemy[i].eyccadd = -enemy[i].eyccadd;
						}
					}
				}
			}
			
			enemy[i].ey += enemy[i].fixedmovey;
			
			enemy[i].ex += enemy[i].exc;
			if (enemy[i].ex < -80 || enemy[i].ex > 340)
				goto enemy_gone;
			
			enemy[i].ey += enemy[i].eyc;
			if (enemy[i].ey < -112 || enemy[i].ey > 190)
				goto enemy_gone;
			
			goto enemy_still_exists;
			
enemy_gone:
			/* enemy[i].egr[10] &= 0x00ff; <MXD> madness? */
			enemyAvail[i] = 1;
			goto draw_enemy_end;
			
enemy_still_exists:
			
			/*X bounce*/
			if (enemy[i].ex <= enemy[i].xminbounce || enemy[i].ex >= enemy[i].xmaxbounce)
				enemy[i].exc = -enemy[i].exc;
			
			/*Y bounce*/
			if (enemy[i].ey <= enemy[i].yminbounce || enemy[i].ey >= enemy[i].ymaxbounce)
				enemy[i].eyc = -enemy[i].eyc;
			
			/* Evalue != 0 - score item at boundary */
			if (enemy[i].scoreitem)
			{
				if (enemy[i].ex < -5)
					enemy[i].ex++;
				if (enemy[i].ex > 245)
					enemy[i].ex--;
			}
			
			enemy[i].ey += tempBackMove;
			
			if (enemy[i].ex <= -24 || enemy[i].ex >= 296)
				goto draw_enemy_end;
			
			tempX = enemy[i].ex;
			tempY = enemy[i].ey;
			
			temp = enemy[i].enemytype;
			
			/* Enemy Shots */
			if (enemy[i].edamaged == 1)
				goto draw_enemy_end;
			
			enemyOnScreen++;
			
			if (enemy[i].iced)
			{
				enemy[i].iced--;
				if (enemy[i].enemyground != 0)
				{
					enemy[i].filter = 9;
				}
				goto draw_enemy_end;
			}
			
			for (int j = 3; j > 0; j--)
			{
				if (enemy[i].freq[j-1])
				{
					temp3 = enemy[i].tur[j-1];
					
					if (--enemy[i].eshotwait[j-1] == 0 && temp3)
					{
						enemy[i].eshotwait[j-1] = enemy[i].freq[j-1];
						if (difficultyLevel > 2)
						{
							enemy[i].eshotwait[j-1] = (enemy[i].eshotwait[j-1] / 2) + 1;
							if (difficultyLevel > 7)
								enemy[i].eshotwait[j-1] = (enemy[i].eshotwait[j-1] / 2) + 1;
						}
						
						if (galagaMode && (enemy[i].eyc == 0 || (mt_rand() % 400) >= galagaShotFreq))
							goto draw_enemy_end;
						
						switch (temp3)
						{
							case 252: /* Savara Boss DualMissile */
								if (enemy[i].ey > 20)
								{
									JE_setupExplosion(tempX - 8 + tempMapXOfs, tempY - 20 - backMove * 8, -2, 6, false, false);
									JE_setupExplosion(tempX + 4 + tempMapXOfs, tempY - 20 - backMove * 8, -2, 6, false, false);
								}
								break;
							case 251: /* Suck-O-Magnet */
								tempI4 = 4 - (abs(PX - tempX) + abs(PY - tempY)) / 100;
								if (PX > tempX)
								{
									lastTurn2 -= tempI4;
								} else {
									lastTurn2 += tempI4;
								}
								break;
							case 253: /* Left ShortRange Magnet */
								if (abs(PX + 25 - 14 - tempX) < 24 && abs(PY - tempY) < 28)
								{
									lastTurn2 += 2;
								}
								if (twoPlayerMode &&
								   (abs(PXB - 14 - tempX) < 24 && abs(PYB - tempY) < 28))
								{
									lastTurn2B += 2;
								}
								break;
							case 254: /* Left ShortRange Magnet */
								if (abs(PX + 25 - 14 - tempX) < 24 && abs(PY - tempY) < 28)
								{
									lastTurn2 -= 2;
								}
								if (twoPlayerMode &&
								   (abs(PXB - 14 - tempX) < 24 && abs(PYB - tempY) < 28))
								{
									lastTurn2B -= 2;
								}
								break;
							case 255: /* Magneto RePulse!! */
								if (difficultyLevel != 1) /*DIF*/
								{
									if (j == 3)
									{
										enemy[i].filter = 112;
									}
									else
									{
										tempI4 = 4 - (abs(PX - tempX) + abs(PY - tempY)) / 20;
										if (tempI4 > 0)
										{
											if (PX > tempX)
												lastTurn2 += tempI4;
											else
												lastTurn2 -= tempI4;
										}
									}
								}
								break;
							default:
							/*Rot*/
								for (tempCount = weapons[temp3].multi; tempCount > 0; tempCount--)
								{
									for (b = 0; b < ENEMY_SHOT_MAX; b++)
									{
										if (enemyShotAvail[b] == 1)
											break;
									}
									if (b == ENEMY_SHOT_MAX)
										goto draw_enemy_end;
									
									enemyShotAvail[b]--;
									
									if (weapons[temp3].sound > 0)
									{
										do
											temp = mt_rand() % 8;
										while (temp == 3);
										soundQueue[temp] = weapons[temp3].sound;
									}
									
									tempPos = weapons[temp3].max;
									
									if (enemy[i].aniactive == 2)
										enemy[i].aniactive = 1;
									
									if (++enemy[i].eshotmultipos[j-1] > tempPos)
										enemy[i].eshotmultipos[j-1] = 1;
									
									tempPos = enemy[i].eshotmultipos[j-1];
									
									if (j == 1)
										temp2 = 4;
									
									enemyShot[b].sx = tempX + weapons[temp3].bx[tempPos-1] + tempMapXOfs;
									enemyShot[b].sy = tempY + weapons[temp3].by[tempPos-1];
									enemyShot[b].sdmg = weapons[temp3].attack[tempPos-1];
									enemyShot[b].tx = weapons[temp3].tx;
									enemyShot[b].ty = weapons[temp3].ty;
									enemyShot[b].duration = weapons[temp3].del[tempPos-1];
									enemyShot[b].animate = 0;
									enemyShot[b].animax = weapons[temp3].weapani;
									
									enemyShot[b].sgr = weapons[temp3].sg[tempPos-1];
									switch (j)
									{
										case 1:
											enemyShot[b].syc = weapons[temp3].acceleration;
											enemyShot[b].sxc = weapons[temp3].accelerationx;
											
											enemyShot[b].sxm = weapons[temp3].sx[tempPos-1];
											enemyShot[b].sym = weapons[temp3].sy[tempPos-1];
											break;
										case 3:
											enemyShot[b].sxc = -weapons[temp3].acceleration;
											enemyShot[b].syc = weapons[temp3].accelerationx;
											
											enemyShot[b].sxm = -weapons[temp3].sy[tempPos-1];
											enemyShot[b].sym = -weapons[temp3].sx[tempPos-1];
											break;
										case 2:
											enemyShot[b].sxc = weapons[temp3].acceleration;
											enemyShot[b].syc = -weapons[temp3].acceleration;
											
											enemyShot[b].sxm = weapons[temp3].sy[tempPos-1];
											enemyShot[b].sym = -weapons[temp3].sx[tempPos-1];
											break;
									}
									
									if (weapons[temp3].aim > 0)
									{
										temp4 = weapons[temp3].aim;
										
										/*DIF*/
										if (difficultyLevel > 2)
										{
											temp4 += difficultyLevel - 2;
										}
										
										tempX2 = PX;
										tempY2 = PY;
											
										if (twoPlayerMode)
										{
											if (playerAliveB && !playerAlive)
												temp = 1;
											else if (playerAlive && !playerAliveB)
												temp = 0;
											else
												temp = mt_rand() % 2;
											
											if (temp == 1)
											{
												tempX2 = PXB - 25;
												tempY2 = PYB;
											}
										}
										
										tempI = (tempX2 + 25) - tempX - tempMapXOfs - 4;
										if (tempI == 0)
											tempI++;
										tempI2 = tempY2 - tempY;
										if (tempI2 == 0)
											tempI2++;
										if (abs(tempI) > abs(tempI2))
											tempI3 = abs(tempI);
										else
											tempI3 = abs(tempI2);
										enemyShot[b].sxm = round(((float)tempI / tempI3) * temp4);
										enemyShot[b].sym = round(((float)tempI2 / tempI3) * temp4);
									}
								}
								break;
						}
					}
				}
			}
			
			/* Enemy Launch Routine */
			if (enemy[i].launchfreq)
			{
				if (--enemy[i].launchwait == 0)
				{
					enemy[i].launchwait = enemy[i].launchfreq;
					
					if (enemy[i].launchspecial != 0)
					{
						/*Type  1 : Must be inline with player*/
						if (abs(enemy[i].ey - PY) > 5)
							goto draw_enemy_end;
					}
					
					if (enemy[i].aniactive == 2)
					{
						enemy[i].aniactive = 1;
					}
					
					if (enemy[i].launchtype == 0)
						goto draw_enemy_end;
					
					tempW = enemy[i].launchtype;
					JE_newEnemy(enemyOffset == 50 ? 75 : enemyOffset - 25);
					
					/*Launch Enemy Placement*/
					if (b > 0)
					{
						tempI = tempX;
						tempI2 = tempY + enemyDat[enemy[b-1].enemytype].startyc;
						if (enemy[b-1].size == 0)
						{
							tempI -= 0;
							tempI2 -= 7;
						}
						if (enemy[b-1].launchtype > 0 && enemy[b-1].launchfreq == 0)
						{
							
							if (enemy[b-1].launchtype > 90)
							{
								tempI += mt_rand() % ((enemy[b-1].launchtype - 90) * 4) - (enemy[b-1].launchtype - 90) * 2;
							}
							else
							{
								tempI4 = (PX + 25) - tempX - tempMapXOfs - 4;
								if (tempI4 == 0)
									tempI4++;
								tempI5 = PY - tempY;
								if (tempI5 == 0)
									tempI5++;
								if (abs(tempI4) > abs(tempI5))
									tempI3 = abs(tempI4);
								else
									tempI3 = abs(tempI5);
								enemy[b-1].exc = round(((float)tempI4 / tempI3) * enemy[b-1].launchtype);
								enemy[b-1].eyc = round(((float)tempI5 / tempI3) * enemy[b-1].launchtype);
							}
						}
						
						do
							temp = mt_rand() % 8;
						while (temp == 3);
						soundQueue[temp] = randomEnemyLaunchSounds[(mt_rand() % 3)];
						
						if (enemy[i].launchspecial == 1
							&& enemy[i].linknum < 100)
						{
							enemy[b-1].linknum = enemy[i].linknum;
						}
						
						enemy[b-1].ex = tempI;
						enemy[b-1].ey = tempI2;
					}
				}
			}
		}
draw_enemy_end:
		;
	}

	PX += 25;
}

void JE_main( void )
{
	int i, j, l;
	JE_byte **bp;

	JE_byte *p; /* source/shape pointer */
	Uint8 *s; /* screen pointer, 8-bit specific */
	Uint8 *s_limit; /* buffer boundary */

	char buffer[256];

	int lastEnemyOnScreen;

	/* Setup Player Items/General Data */
	for (z = 0; z < 12; z++)
	{
		pItems[z] = 0;
	}
	shieldSet = 5;

	/* Setup Graphics */
	JE_updateColorsFast(black);

	/* NOTE: BEGIN MAIN PROGRAM HERE AFTER LOADING A GAME OR STARTING A NEW ONE */

	/* ----------- GAME ROUTINES ------------------------------------- */
	/* We need to jump to the beginning to make space for the routines */
	/* --------------------------------------------------------------- */
	goto start_level_first;


	/*------------------------------GAME LOOP-----------------------------------*/


	/* Startlevel is called after a previous level is over.  If the first level
	   is started for a gaming session, startlevelfirst is called instead and
	   this code is skipped.  The code here finishes the level and prepares for
	   the loadmap function. */

start_level:
	
	if (galagaMode)
		twoPlayerMode = false;
	
	JE_clearKeyboard();
	
	if (eShapes1 != NULL)
	{
		free(eShapes1);
		eShapes1 = NULL;
	}
	if (eShapes2 != NULL)
	{
		free(eShapes2);
		eShapes2 = NULL;
	}
	if (eShapes3 != NULL)
	{
		free(eShapes3);
		eShapes3 = NULL;
	}
	if (eShapes4 != NULL)
	{
		free(eShapes4);
		eShapes4 = NULL;
	}

	/* Normal speed */
	if (fastPlay != 0)
	{
		smoothScroll = true;
		speed = 0x4300;
		JE_resetTimerInt();
		JE_setTimerInt();
	}

	if (play_demo || record_demo)
	{
		if (demo_file)
		{
			fclose(demo_file);
			demo_file = NULL;
		}
		
		if (play_demo)
		{
			stop_song();
			fade_black(10);
			
			wait_noinput(true, true, true);
		}
	}

	difficultyLevel = oldDifficultyLevel;   /*Return difficulty to normal*/

	if (!play_demo)
	{
		if (((playerAlive || (twoPlayerMode && playerAliveB))
		   || normalBonusLevelCurrent || bonusLevelCurrent)
		   && !playerEndLevel)
		{
			mainLevel = nextLevel;
			JE_endLevelAni();
			
			fade_song();
		}
		else
		{
			fade_song();
			fade_black(10);
			
			JE_loadGame(twoPlayerMode ? 22 : 11);
			if (doNotSaveBackup)
			{
				superTyrian = false;
				onePlayerAction = false;
				pItems[P_SUPERARCADE] = SA_NONE;
			}
			if (bonusLevelCurrent && !playerEndLevel)
			{
				mainLevel = nextLevel;
			}
		}
	}
	doNotSaveBackup = false;

start_level_first:
	
	set_volume(tyrMusicVolume, fxVolume);
	
	JE_loadCompShapes(&shapes6, &shapes6Size, '1');  /* Items */

	endLevel = false;
	reallyEndLevel = false;
	playerEndLevel = false;
	extraGame = false;

	/*debuginfo('Loading LEVELS.DAT');*/

	doNotSaveBackup = false;
	JE_loadMap();
	
	fade_song();
	
	playerAlive = true;
	playerAliveB = true;
	oldDifficultyLevel = difficultyLevel;
	if (episodeNum == 4)
	{
		difficultyLevel--;
	}
	if (difficultyLevel < 1)
	{
		difficultyLevel = 1;
	}

	if (loadDestruct)
	{
		JE_destructGame();
		loadDestruct = false;
		loadTitleScreen = true;
		goto start_level_first;
	}

	PX = 100;
	PY = 180;

	PXB = 190;
	PYB = 180;

	playerHNotReady = true;

	lastPXShotMove = PX;
	lastPYShotMove = PY;

	if (twoPlayerMode)
	{
		JE_loadPic(6, false);
	} else {
		JE_loadPic(3, false);
	}

	tempScreenSeg = VGAScreen;
	JE_drawOptions();

	if (twoPlayerMode)
	{
		temp = 76;
	} else {
		temp = 118;
	}
	JE_outText(268, temp, levelName, 12, 4);

	JE_showVGA();
	JE_gammaCorrect(&colors, gammaCorrection);
	JE_fadeColor(50);

	JE_loadCompShapes(&shapes6, &shapes6Size, '6'); /* Explosions */

	/* MAPX will already be set correctly */
	mapY = 300 - 8;
	mapY2 = 600 - 8;
	mapY3 = 600 - 8;
	mapYPos = &megaData1->mainmap[mapY][0] - 1;
	mapY2Pos = &megaData2->mainmap[mapY2][0] - 1;
	mapY3Pos = &megaData3->mainmap[mapY3][0] - 1;
	mapXPos = 0;
	mapXOfs = 0;
	mapX2Pos = 0;
	mapX3Pos = 0;
	mapX3Ofs = 0;
	mapXbpPos = 0;
	mapX2bpPos = 0;
	mapX3bpPos = 0;

	map1YDelay = 1;
	map1YDelayMax = 1;
	map2YDelay = 1;
	map2YDelayMax = 1;

	musicFade = false;

	backPos = 0;
	backPos2 = 0;
	backPos3 = 0;
	power = 0;
	starY = VGAScreen->pitch;

	/* Setup maximum player speed */
	/* ==== Mouse Input ==== */
	baseSpeed = 6;
	baseSpeedKeyH = (baseSpeed / 4) + 1;
	baseSpeedKeyV = (baseSpeed / 4) + 1;

	baseSpeedOld = baseSpeed;
	baseSpeedOld2 = (baseSpeed * 0.7f) + 1;
	baseSpeed2  = 100 - (((baseSpeed + 1) / 4) + 1);
	baseSpeed2B = 100 + 100 - baseSpeed2;
	baseSpeed   = 100 - (((baseSpeed + 1) / 4) + 1);
	baseSpeedB  = 100 + 100 - baseSpeed;
	shadowyDist = 10;

	/* Setup player ship graphics */
	JE_getShipInfo();
	tempI = (((PX - mouseX) / (100 - baseSpeed)) * 2) * 168;
	lastTurn   = 0;
	lastTurnB  = 0;
	lastTurn2  = 0;
	lastTurn2B = 0;

	playerInvulnerable1 = 100;
	playerInvulnerable2 = 100;

	newkey = newmouse = false;

	/* Initialize Level Data and Debug Mode */
	levelEnd = 255;
	levelEndWarp = -4;
	levelEndFxWait = 0;
	warningCol = 120;
	warningColChange = 1;
	warningSoundDelay = 0;
	armorShipDelay = 50;

	bonusLevel = false;
	readyToEndLevel = false;
	firstGameOver = true;
	eventLoc = 1;
	curLoc = 0;
	backMove = 1;
	backMove2 = 2;
	backMove3 = 3;
	explodeMove = 2;
	enemiesActive = true;
	for(temp = 0; temp < 3; temp++)
	{
		button[temp] = false;
	}
	stopBackgrounds = false;
	stopBackgroundNum = 0;
	background3x1   = false;
	background3x1b  = false;
	background3over = 0;
	background2over = 1;
	topEnemyOver = false;
	skyEnemyOverAll = false;
	smallEnemyAdjust = false;
	starActive = true;
	enemyContinualDamage = false;
	levelEnemyFrequency = 96;
	quitRequested = false;

	for (unsigned int i = 0; i < COUNTOF(boss_bar); i++)
		boss_bar[i].link_num = 0;

	forceEvents = false;  /*Force events to continue if background movement = 0*/

	uniqueEnemy = false;  /*Look in MakeEnemy under shape bank*/

	superEnemy254Jump = 0;   /*When Enemy with PL 254 dies*/

	/* Filter Status */
	filterActive = true;
	filterFade = true;
	filterFadeStart = false;
	levelFilter = -99;
	levelBrightness = -14;
	levelBrightnessChg = 1;

	background2notTransparent = false;

	/* Initially erase power bars */
	lastPower = power / 10;

	/* Initial Text */
	JE_drawTextWindow(miscText[20]);

	/* Setup Armor/Shield Data */
	shieldWait = 1;
	shield     = shields[pItems[P_SHIELD]].mpwr;
	shieldT    = shields[pItems[P_SHIELD]].tpwr * 20;
	shieldMax  = shield * 2;
	shield2    = shields[pItemsPlayer2[P_SHIELD]].mpwr;
	shieldMax2 = shield * 2;
	JE_drawShield();
	JE_drawArmor();

	superBomb[0] = 0;
	superBomb[1] = 0;

	/* Set cubes to 0 */
	cubeMax = 0;

	lastPortPower[0] = 0;
	lastPortPower[1] = 0;
	lastPortPower[2] = 0;
	lastPortPower[3] = 0;

	/* Secret Level Display */
	flash = 0;
	flashChange = 1;
	displayTime = 0;

	play_song(levelSong - 1);

	JE_drawPortConfigButtons();

	/* --- MAIN LOOP --- */

	newkey = false;

	if (isNetworkGame)
	{
		JE_clearSpecialRequests();
		mt_srand(32402394);
	}

	JE_setupStars();

	JE_setNewGameSpeed();

	/* JE_setVol(tyrMusicVolume, fxPlayVol >> 2); NOTE: MXD killed this because it was broken */

	/*Save backup game*/
	if (!play_demo && !doNotSaveBackup)
	{
		temp = twoPlayerMode ? 22 : 11;
		JE_saveGame(temp, "LAST LEVEL    ");
	}
	
	if (!play_demo && record_demo)
	{
		Uint8 new_demo_num = 0;
		
		dont_die = true; // for JE_find
		do
		{
			sprintf(tempStr, "demorec.%d", new_demo_num++);
		}
		while (JE_find(tempStr)); // until file doesn't exist
		dont_die = false;
		
		demo_file = fopen_check(tempStr, "wb");
		if (!demo_file)
		{
			printf("error: failed to open '%s' (mode '%s')\n", tempStr, "wb");
			exit(1);
		}
		
		efwrite(&episodeNum, 1,  1, demo_file);
		efwrite(levelName,   1, 10, demo_file);
		efwrite(&lvlFileNum, 1,  1, demo_file);
		efwrite(pItems,      1, 12, demo_file);
		efwrite(portPower,   1,  5, demo_file);
		efwrite(&levelSong,  1,  1, demo_file);
		
		demo_keys = 0;
		demo_keys_wait = 0;
	}

	twoPlayerLinked = false;
	linkGunDirec = M_PI;

	JE_calcPurpleBall(1);
	JE_calcPurpleBall(2);

	damageRate = 2;  /*Normal Rate for Collision Damage*/

	chargeWait   = 5;
	chargeLevel  = 0;
	chargeMax    = 5;
	chargeGr     = 0;
	chargeGrWait = 3;

	portConfigChange = false;

	makeMouseDelay = false;

	/*Destruction Ratio*/
	totalEnemy = 0;
	enemyKilled = 0;

	astralDuration = 0;

	superArcadePowerUp = 1;

	yourInGameMenuRequest = false;

	constantLastX = -1;

	playerStillExploding = 0;
	playerStillExploding2 = 0;

	if (isNetworkGame)
	{
		JE_loadItemDat();
	}

	memset(enemyAvail,       1, sizeof(enemyAvail));
	for (i = 0; i < COUNTOF(enemyShotAvail); i++)
		enemyShotAvail[i] = 1;
	
	/*Initialize Shots*/
	memset(playerShotData,   0, sizeof(playerShotData));
	memset(shotAvail,        0, sizeof(shotAvail));
	memset(shotMultiPos,     0, sizeof(shotMultiPos));
	memset(shotRepeat,       1, sizeof(shotRepeat));
	
	memset(button,           0, sizeof(button));
	
	memset(globalFlags,      0, sizeof(globalFlags));
	
	memset(explosions,       0, sizeof(explosions));
	memset(rep_explosions,   0, sizeof(rep_explosions));
	
	/* --- Clear Sound Queue --- */
	memset(soundQueue,       0, sizeof(soundQueue));
	soundQueue[3] = V_GOOD_LUCK;

	memset(enemyShapeTables, 0, sizeof(enemyShapeTables));
	memset(enemy,            0, sizeof(enemy));

	memset(SFCurrentCode,    0, sizeof(SFCurrentCode));
	memset(SFExecuted,       0, sizeof(SFExecuted));

	zinglonDuration = 0;
	specialWait = 0;
	nextSpecialWait = 0;
	optionAttachmentMove  = 0;    /*Launch the Attachments!*/
	optionAttachmentLinked = true;

	editShip1 = false;
	editShip2 = false;

	memset(smoothies, 0, sizeof(smoothies));

	levelTimer = false;
	randomExplosions = false;

	last_superpixel = 0;
	memset(superpixels, 0, sizeof(superpixels));

	returnActive = false;

	galagaShotFreq = 0;

	if (galagaMode)
	{
		difficultyLevel = 2;
	}
	galagaLife = 10000;

	JE_drawOptionLevel();

	BKwrap1 = &megaData1->mainmap[1][0];
	BKwrap1to = &megaData1->mainmap[1][0];
	BKwrap2 = &megaData2->mainmap[1][0];
	BKwrap2to = &megaData2->mainmap[1][0];
	BKwrap3 = &megaData3->mainmap[1][0];
	BKwrap3to = &megaData3->mainmap[1][0];

level_loop:

	tempScreenSeg = game_screen; /* side-effect of game_screen */

	if (isNetworkGame)
	{
		smoothies[9-1] = false;
		smoothies[6-1] = false;
	} else {
		starShowVGASpecialCode = smoothies[9-1] + (smoothies[6-1] << 1);
	}

	/*Background Wrapping*/
	if (mapYPos <= BKwrap1)
	{
		mapYPos = BKwrap1to;
	}
	if (mapY2Pos <= BKwrap2)
	{
		mapY2Pos = BKwrap2to;
	}
	if (mapY3Pos <= BKwrap3)
	{
		mapY3Pos = BKwrap3to;
	}


	allPlayersGone = !playerAlive &&
	                 (!playerAliveB || !twoPlayerMode) &&
	                 ((portPower[0] == 1 && playerStillExploding == 0) || (!onePlayerAction && !twoPlayerMode)) &&
	                 ((portPower[1] == 1 && playerStillExploding2 == 0) || !twoPlayerMode);


	/*-----MUSIC FADE------*/
	if (musicFade)
	{
		if (tempVolume > 10)
		{
			tempVolume--;
			set_volume(tempVolume, fxVolume);
		}
		else
		{
			musicFade = false;
		}
	}

	if (!allPlayersGone && levelEnd > 0 && endLevel)
	{
		play_song(9);
		musicFade = false;
	}
	else if (!playing && firstGameOver)
	{
		play_song(levelSong - 1);
	}


	if (!endLevel) // draw HUD
	{
		VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
		
		/*-----------------------Message Bar------------------------*/
		if (textErase > 0 && --textErase == 0)
			blit_shape(VGAScreenSeg, 16, 189, OPTION_SHAPES, 36);  // in-game message area
		
		/*------------------------Shield Gen-------------------------*/
		if (galagaMode)
		{
			shield = 0;
			shield2 = 0;
			
			if (portPower[2-1] == 0 || armorLevel2 == 0)
				twoPlayerMode = false;
			
			if (score >= galagaLife)
			{
				soundQueue[6] = S_EXPLOSION_11;
				soundQueue[7] = S_SOUL_OF_ZINGLON;
				
				if (portPower[1-1] < 11)
					portPower[1-1]++;
				else
					score += 1000;
				
				if (galagaLife == 10000)
					galagaLife = 20000;
				else
					galagaLife += 25000;
			}
		}
		else // not galagaMode
		{
			if (twoPlayerMode)
			{
				if (--shieldWait == 0)
				{
					shieldWait = 20 - shieldSet;
					
					if (shield < shieldMax && playerAlive)
						shield++;
					if (shield2 < shieldMax2 && playerAliveB)
						shield2++;
					
					JE_drawShield();
				}
			}
			else if (playerAlive && shield < shieldMax && power > shieldT)
			{
				if (--shieldWait == 0)
				{
					shieldWait = 20 - shieldSet;
					
					power -= shieldT;
					shield++;
					if (shield2 < shieldMax)
						shield2++;
					
					JE_drawShield();
				}
			}
		}
		
		/*---------------------Weapon Display-------------------------*/
		if (lastPortPower[1-1] != portPower[1-1])
		{
			lastPortPower[1-1] = portPower[1-1];
			
			if (twoPlayerMode)
			{
				tempW2 = 6;
				tempW = 286;
			}
			else
			{
				tempW2 = 17;
				tempW = 289;
			}
			
			JE_c_bar(tempW, tempW2, tempW + 1 + 10 * 2, tempW2 + 2, 0);
			
			for (temp = 1; temp <= portPower[1-1]; temp++)
			{
				JE_rectangle(tempW, tempW2, tempW + 1, tempW2 + 2, 115 + temp); /* <MXD> SEGa000 */
				tempW += 2;
			}
		}
		
		if (lastPortPower[2-1] != portPower[2-1])
		{
			lastPortPower[2-1] = portPower[2-1];
			
			if (twoPlayerMode)
			{
				tempW2 = 100;
				tempW = 286;
			}
			else
			{
				tempW2 = 38;
				tempW = 289;
			}
			
			JE_c_bar(tempW, tempW2, tempW + 1 + 10 * 2, tempW2 + 2, 0);
			
			for (temp = 1; temp <= portPower[2-1]; temp++)
			{
				JE_rectangle(tempW, tempW2, tempW + 1, tempW2 + 2, 115 + temp); /* <MXD> SEGa000 */
				tempW += 2;
			}
		}
		
		/*------------------------Power Bar-------------------------*/
		if (twoPlayerMode || onePlayerAction)
		{
			power = 900;
		}
		else
		{
			power = power + powerAdd;
			if (power > 900)
				power = 900;
			
			temp = power / 10;
			
			if (temp != lastPower)
			{
				if (temp > lastPower)
					JE_c_bar(269, 113 - 11 - temp, 276, 114 - 11 - lastPower, 113 + temp / 7);
				else
					JE_c_bar(269, 113 - 11 - lastPower, 276, 114 - 11 - temp, 0);
			}
			
			lastPower = temp;
		}

		oldMapX3Ofs = mapX3Ofs;

		enemyOnScreen = 0;
	}
	
	/* use game_screen for all the generic drawing functions */
	VGAScreen = game_screen;
	
	/*---------------------------EVENTS-------------------------*/
	while (eventRec[eventLoc-1].eventtime <= curLoc && eventLoc <= maxEvent)
		JE_eventSystem();
	
	if (isNetworkGame && reallyEndLevel)
		goto start_level;
	
	
	/* SMOOTHIES! */
	JE_checkSmoothies();
	if (anySmoothies)
		VGAScreen = smoothiesScreen;
	
	/* --- BACKGROUNDS --- */
	/* --- BACKGROUND 1 --- */
	
	if (forceEvents && !backMove)
		curLoc++;
	
	if (map1YDelayMax > 1 && backMove < 2)
		backMove = (map1YDelay == 1) ? 1 : 0;
	
	/*Draw background*/
	if (astralDuration == 0)
		draw_background_1(VGAScreen);
	else
		JE_clr256();
	
	/*Set Movement of background 1*/
	if (--map1YDelay == 0)
	{
		map1YDelay = map1YDelayMax;

		curLoc += backMove;

		backPos += backMove;

		if (backPos > 27)
		{
			backPos -= 28;
			mapY--;
			mapYPos -= 14;  /*Map Width*/
		}
	}

	/*---------------------------STARS--------------------------*/
	/* DRAWSTARS */
	if (starActive || astralDuration > 0)
	{
		s = (Uint8 *)VGAScreen->pixels;
		
		for (i = MAX_STARS; i--; )
		{
			starDat[i].sLoc += starDat[i].sMov + starY;
			if (starDat[i].sLoc < 177 * VGAScreen->pitch)
			{
				if (*(s + starDat[i].sLoc) == 0)
				{
					*(s + starDat[i].sLoc) = starDat[i].sC;
				}
				if (starDat[i].sC - 4 >= 9 * 16)
				{
					if (*(s + starDat[i].sLoc + 1) == 0)
					{
						*(s + starDat[i].sLoc + 1) = starDat[i].sC - 4;
					}
					if (starDat[i].sLoc > 0 && *(s + starDat[i].sLoc - 1) == 0)
					{
						*(s + starDat[i].sLoc - 1) = starDat[i].sC - 4;
					}
					if (*(s + starDat[i].sLoc + VGAScreen->pitch) == 0)
					{
						*(s + starDat[i].sLoc + VGAScreen->pitch) = starDat[i].sC - 4;
					}
					if (starDat[i].sLoc >= VGAScreen->pitch && *(s + starDat[i].sLoc - VGAScreen->pitch) == 0)
					{
						*(s + starDat[i].sLoc - VGAScreen->pitch) = starDat[i].sC - 4;
					}
				}
			}
		}
	}

	if (processorType > 1 && smoothies[5-1])
		JE_smoothies3(); // iced motion blur

	/*-----------------------BACKGROUNDS------------------------*/
	/*-----------------------BACKGROUND 2------------------------*/
	if (background2over == 3)
	{
		draw_background_2(VGAScreen);
		background2 = true;
	}

	if (background2over == 0)
	{
		if (!(smoothies[2-1] && processorType < 4) && !(smoothies[1-1] && processorType == 3))
		{
			if (wild && !background2notTransparent)
				draw_background_2_blend(VGAScreen);
			else
				draw_background_2(VGAScreen);
		}
	}

	if (smoothies[1-1] && processorType > 2 && SDAT[1-1] == 0)
		JE_smoothies1(); // lava
	if (smoothies[2-1] && processorType > 2)
		JE_smoothies2(); // water

	/*-----------------------Ground Enemy------------------------*/
	lastEnemyOnScreen = enemyOnScreen;

	tempMapXOfs = mapXOfs;
	tempBackMove = backMove;
	JE_drawEnemy(50);
	JE_drawEnemy(100);

	if (enemyOnScreen == 0 || enemyOnScreen == lastEnemyOnScreen)
	{
		if (stopBackgroundNum == 1)
			stopBackgroundNum = 9;
	}

	if (smoothies[1-1] && processorType > 2 && SDAT[1-1] > 0)
		JE_smoothies1(); // lava

	if (superWild)
	{
		neat += 3;
		JE_darkenBackground(neat);
	}

	/*-----------------------BACKGROUNDS------------------------*/
	/*-----------------------BACKGROUND 2------------------------*/
	if (!(smoothies[2-1] && processorType < 4) &&
	    !(smoothies[1-1] && processorType == 3))
	{
		if (background2over == 1)
		{
			if (wild && !background2notTransparent)
				draw_background_2_blend(VGAScreen);
			else
				draw_background_2(VGAScreen);
		}
	}

	if (superWild)
	{
		neat++;
		JE_darkenBackground(neat);
	}

	if (background3over == 2)
		draw_background_3(VGAScreen);

	/* New Enemy */
	if (enemiesActive && mt_rand() % 100 > levelEnemyFrequency)
	{
		tempW = levelEnemy[mt_rand() % levelEnemyMax];
		if (tempW == 2)
			soundQueue[3] = S_WEAPON_7;
		JE_newEnemy(0);
	}

	if (processorType > 1 && smoothies[3-1])
		JE_smoothies3(); // iced motion blur
	if (processorType > 1 && smoothies[4-1])
		JE_smoothies4(); // motion blur

	/* Draw Sky Enemy */
	if (!skyEnemyOverAll)
	{
		lastEnemyOnScreen = enemyOnScreen;

		tempMapXOfs = mapX2Ofs;
		tempBackMove = 0;
		JE_drawEnemy(25);

		if (enemyOnScreen == lastEnemyOnScreen)
		{
			if (stopBackgroundNum == 2)
				stopBackgroundNum = 9;
		}
	}

	if (background3over == 0)
		draw_background_3(VGAScreen);

	/* Draw Top Enemy */
	if (!topEnemyOver)
	{
		tempMapXOfs = (background3x1 == 0) ? oldMapX3Ofs : mapXOfs;
		tempBackMove = backMove3;
		JE_drawEnemy(75);
	}

	/* Player Shot Images */
	for (z = 0; z < MAX_PWEAPON; z++)
	{
		if (shotAvail[z] != 0)
		{
			shotAvail[z]--;
			if (z != MAX_PWEAPON - 1)
			{

				playerShotData[z].shotXM += playerShotData[z].shotXC;
				playerShotData[z].shotX += playerShotData[z].shotXM;
				tempI4 = playerShotData[z].shotXM;

				if (playerShotData[z].shotXM > 100)
				{
					if (playerShotData[z].shotXM == 101)
					{
						playerShotData[z].shotX -= 101;
						playerShotData[z].shotX += PXChange;
						playerShotData[z].shotY += PYChange;
					}
					else
					{
						playerShotData[z].shotX -= 120;
						playerShotData[z].shotX += PXChange;
					}
				}

				playerShotData[z].shotYM += playerShotData[z].shotYC;
				playerShotData[z].shotY += playerShotData[z].shotYM;

				if (playerShotData[z].shotYM > 100)
				{
					playerShotData[z].shotY -= 120;
					playerShotData[z].shotY += PYChange;
				}

				if (playerShotData[z].shotComplicated != 0)
				{
					playerShotData[z].shotDevX += playerShotData[z].shotDirX;
					playerShotData[z].shotX += playerShotData[z].shotDevX;

					if (abs(playerShotData[z].shotDevX) == playerShotData[z].shotCirSizeX)
						playerShotData[z].shotDirX = -playerShotData[z].shotDirX;

					playerShotData[z].shotDevY += playerShotData[z].shotDirY;
					playerShotData[z].shotY += playerShotData[z].shotDevY;

					if (abs(playerShotData[z].shotDevY) == playerShotData[z].shotCirSizeY)
						playerShotData[z].shotDirY = -playerShotData[z].shotDirY;
					
					/*Double Speed Circle Shots - add a second copy of above loop*/
				}
				
				tempShotX = playerShotData[z].shotX;
				tempShotY = playerShotData[z].shotY;
				
				if (playerShotData[z].shotX < -34 || playerShotData[z].shotX > 290 ||
				    playerShotData[z].shotY < -15 || playerShotData[z].shotY > 190)
				{
					shotAvail[z] = 0;
					goto draw_player_shot_loop_end;
				}
				
				if (playerShotData[z].shotTrail != 255)
				{
					if (playerShotData[z].shotTrail == 98)
						JE_setupExplosion(playerShotData[z].shotX - playerShotData[z].shotXM, playerShotData[z].shotY - playerShotData[z].shotYM, 0, playerShotData[z].shotTrail, false, false);
					else
						JE_setupExplosion(playerShotData[z].shotX, playerShotData[z].shotY, 0, playerShotData[z].shotTrail, false, false);
				}
				
				if (playerShotData[z].aimAtEnemy != 0)
				{
					if (--playerShotData[z].aimDelay == 0)
					{
						playerShotData[z].aimDelay = playerShotData[z].aimDelayMax;
						
						if (enemyAvail[playerShotData[z].aimAtEnemy] != 1)
						{
							if (playerShotData[z].shotX < enemy[playerShotData[z].aimAtEnemy].ex)
								playerShotData[z].shotXM++;
							else
								playerShotData[z].shotXM--;
							
							if (playerShotData[z].shotY < enemy[playerShotData[z].aimAtEnemy].ey)
								playerShotData[z].shotYM++;
							else
								playerShotData[z].shotYM--;
						}
						else
						{
							if (playerShotData[z].shotXM > 0)
								playerShotData[z].shotXM++;
							else
								playerShotData[z].shotXM--;
						}
					}
				}
				
				tempW = playerShotData[z].shotGr + playerShotData[z].shotAni;
				if (++playerShotData[z].shotAni == playerShotData[z].shotAniMax)
					playerShotData[z].shotAni = 0;
				
				tempI2 = playerShotData[z].shotDmg;
				temp2 = playerShotData[z].shotBlastFilter;
				chain = playerShotData[z].chainReaction;
				playerNum = playerShotData[z].playerNumber;
				
				tempSpecial = tempW > 60000;
				
				if (tempSpecial)
				{
					blit_shape_blend(VGAScreen, tempShotX+1, tempShotY, OPTION_SHAPES, tempW - 60001);
					
					tempX2 = shapeX[OPTION_SHAPES][tempW - 60001] / 2;
					tempY2 = shapeY[OPTION_SHAPES][tempW - 60001] / 2;
				}
				else
				{
					if (tempW > 1000)
					{
						JE_doSP(tempShotX+1 + 6, tempShotY + 6, 5, 3, (tempW / 1000) << 4);
						tempW = tempW % 1000;
					}
					if (tempW > 500)
					{
						if (background2 && tempShotY + shadowyDist < 190 && tempI4 < 100)
							JE_drawShape2Shadow(tempShotX+1, tempShotY + shadowyDist, tempW - 500, shapesW2);
						JE_drawShape2(tempShotX+1, tempShotY, tempW - 500, shapesW2);
					}
					else
					{
						if (background2 && tempShotY + shadowyDist < 190 && tempI4 < 100)
							JE_drawShape2Shadow(tempShotX+1, tempShotY + shadowyDist, tempW, shapesC1);
						JE_drawShape2(tempShotX+1, tempShotY, tempW, shapesC1);
					}
				}
				
			}

			for (b = 0; b < 100; b++)
			{
				if (enemyAvail[b] == 0)
				{
					if (z == MAX_PWEAPON - 1)
					{
						temp = 25 - abs(zinglonDuration - 25);
						tempB = abs(enemy[b].ex + enemy[b].mapoffset - (PX + 7)) < temp;
						temp2 = 9;
						chain = 0;
						tempI2 = 10;
					}
					else if (tempSpecial)
					{
						tempB = ((enemy[b].enemycycle == 0) &&
						        (abs(enemy[b].ex + enemy[b].mapoffset - tempShotX - tempX2) < (25 + tempX2)) &&
						        (abs(enemy[b].ey - tempShotY - 12 - tempY2)                 < (29 + tempY2))) ||
						        ((enemy[b].enemycycle > 0) &&
						        (abs(enemy[b].ex + enemy[b].mapoffset - tempShotX - tempX2) < (13 + tempX2)) &&
						        (abs(enemy[b].ey - tempShotY - 6 - tempY2)                  < (15 + tempY2)));
					}
					else
					{
						tempB = ((enemy[b].enemycycle == 0) &&
						        (abs(enemy[b].ex + enemy[b].mapoffset - tempShotX) < 25) && (abs(enemy[b].ey - tempShotY - 12) < 29)) ||
						        ((enemy[b].enemycycle > 0) &&
						        (abs(enemy[b].ex + enemy[b].mapoffset - tempShotX) < 13) && (abs(enemy[b].ey - tempShotY - 6) < 15));
					}
					
					if (tempB)
					{
						if (chain > 0)
						{
							shotMultiPos[5-1] = 0;
							JE_initPlayerShot(0, 5, tempShotX, tempShotY, mouseX, mouseY, chain, playerNum);
							shotAvail[z] = 0;
							goto draw_player_shot_loop_end;
						}
						
						infiniteShot = false;
						
						if (tempI2 == 99)
						{
							tempI2 = 0;
							doIced = 40;
							enemy[b].iced = 40;
						}
						else
						{
							doIced = 0;
							if (tempI2 >= 250)
							{
								tempI2 = tempI2 - 250;
								infiniteShot = true;
							}
						}
						
						tempI = enemy[b].armorleft;
						
						temp = enemy[b].linknum;
						if (temp == 0)
							temp = 255;
						
						if (enemy[b].armorleft < 255)
						{
							for (unsigned int i = 0; i < COUNTOF(boss_bar); i++)
								if (temp == boss_bar[i].link_num)
									boss_bar[i].color = 6;
							
							if (enemy[b].enemyground)
								enemy[b].filter = temp2;
							
							for (unsigned int e = 0; e < COUNTOF(enemy); e++)
							{
								if (enemy[e].linknum == temp &&
								    enemyAvail[e] != 1 &&
								    enemy[e].enemyground != 0)
								{
									if (doIced)
										enemy[e].iced = doIced;
									enemy[e].filter = temp2;
								}
							}
						}
						
						if (tempI > tempI2)
						{
							if (z != MAX_PWEAPON - 1)
							{
								if (enemy[b].armorleft != 255)
								{
									enemy[b].armorleft -= tempI2;
									JE_setupExplosion(tempShotX, tempShotY, 0, 0, false, false);
								}
								else
								{
									JE_doSP(tempShotX + 6, tempShotY + 6, tempI2 / 2 + 3, tempI2 / 4 + 2, temp2);
								}
							}
							
							soundQueue[5] = S_ENEMY_HIT;
							
							if ((tempI - tempI2 <= enemy[b].edlevel) &&
							    ((!enemy[b].edamaged) ^ (enemy[b].edani < 0)))
							{
								
								for (temp3 = 0; temp3 < 100; temp3++)
								{
									if (enemyAvail[temp3] != 1)
									{
										temp4 = enemy[temp3].linknum;
										if ((temp3 == b) ||
										    ((temp != 255) &&
										    (((enemy[temp3].edlevel > 0) && (temp4 == temp)) ||
										    ((enemyContinualDamage && (temp - 100 == temp4)) ||
										    ((temp4 > 40) && (temp4 / 20 == temp / 20) && (temp4 <= temp))))))
										{
										
											enemy[temp3].enemycycle = 1;
											
											enemy[temp3].edamaged = !enemy[temp3].edamaged;
											
											if (enemy[temp3].edani != 0)
											{
												enemy[temp3].ani = abs(enemy[temp3].edani);
												enemy[temp3].aniactive = 1;
												enemy[temp3].animax = 0;
												enemy[temp3].animin = enemy[temp3].edgr;
												enemy[temp3].enemycycle = enemy[temp3].animin - 1;
												
											}
											else if (enemy[temp3].edgr > 0)
											{
												enemy[temp3].egr[1-1] = enemy[temp3].edgr;
												enemy[temp3].ani = 1;
												enemy[temp3].aniactive = 0;
												enemy[temp3].animax = 0;
												enemy[temp3].animin = 1;
											}
											else
											{
												enemyAvail[temp3] = 1;
												enemyKilled++;
											}
											
											enemy[temp3].aniwhenfire = 0;
											
											if (enemy[temp3].armorleft > enemy[temp3].edlevel)
												enemy[temp3].armorleft = enemy[temp3].edlevel;
											
											tempX = enemy[temp3].ex + enemy[temp3].mapoffset;
											tempY = enemy[temp3].ey;
											
											if (enemyDat[enemy[temp3].enemytype].esize != 1)
												JE_setupExplosion(tempX, tempY - 6, 0, 1, false, false);
											else
												JE_setupExplosionLarge(enemy[temp3].enemyground, enemy[temp3].explonum / 2, tempX, tempY);
										}
									}
								}
							}
						}
						else
						{
							
							if ((temp == 254) && (superEnemy254Jump > 0))
								JE_eventJump(superEnemy254Jump);
						
							for (temp2 = 0; temp2 < 100; temp2++)
							{
								if (enemyAvail[temp2] != 1)
								{
									temp3 = enemy[temp2].linknum;
									if ((temp2 == b) || (temp == 254) ||
									    ((temp != 255) && ((temp == temp3) || (temp - 100 == temp3)
									    || ((temp3 > 40) && (temp3 / 20 == temp / 20) && (temp3 <= temp)))))
									{
										
										tempI3 = enemy[temp2].ex + enemy[temp2].mapoffset;
										
										if (enemy[temp2].special)
											globalFlags[enemy[temp2].flagnum] = enemy[temp2].setto;
										
										if ((enemy[temp2].enemydie > 0) &&
										    !((superArcadeMode != SA_NONE) &&
										      (enemyDat[enemy[temp2].enemydie].value == 30000)))
										{
											int temp_b = b;
											tempW = enemy[temp2].enemydie;
											tempW2 = temp2 - (temp2 % 25);
											if (enemyDat[tempW].value > 30000)
											{
												tempW2 = 0;
											}
											JE_newEnemy(tempW2);
											if (b != 0) {
												if ((superArcadeMode != SA_NONE) && (enemy[b-1].evalue > 30000))
												{
													superArcadePowerUp++;
													if (superArcadePowerUp > 5)
														superArcadePowerUp = 1;
													enemy[b-1].egr[1-1] = 5 + superArcadePowerUp * 2;
													enemy[b-1].evalue = 30000 + superArcadePowerUp;
												}
												
												if (enemy[b-1].evalue != 0)
													enemy[b-1].scoreitem = true;
												else
													enemy[b-1].scoreitem = false;
												
												enemy[b-1].ex = enemy[temp2].ex;
												enemy[b-1].ey = enemy[temp2].ey;
											}
											b = temp_b;
										}
										
										if ((enemy[temp2].evalue > 0) && (enemy[temp2].evalue < 10000))
										{
											if (enemy[temp2].evalue == 1)
											{
												cubeMax++;
											}
											else
											{
												if ((playerNum < 2) || galagaMode)
													score += enemy[temp2].evalue;
												else
													score2 += enemy[temp2].evalue;
											}
										}
										
										if ((enemy[temp2].edlevel == -1) && (temp == temp3))
										{
											enemy[temp2].edlevel = 0;
											enemyAvail[temp2] = 2;
											enemy[temp2].egr[1-1] = enemy[temp2].edgr;
											enemy[temp2].ani = 1;
											enemy[temp2].aniactive = 0;
											enemy[temp2].animax = 0;
											enemy[temp2].animin = 1;
											enemy[temp2].edamaged = true;
											enemy[temp2].enemycycle = 1;
										} else {
											enemyAvail[temp2] = 1;
											enemyKilled++;
										}
										
										if (enemyDat[enemy[temp2].enemytype].esize == 1)
										{
											JE_setupExplosionLarge(enemy[temp2].enemyground, enemy[temp2].explonum, tempI3, enemy[temp2].ey);
											soundQueue[6] = S_EXPLOSION_9;
										}
										else
										{
											JE_setupExplosion(tempI3, enemy[temp2].ey, 0, 1, false, false);
											soundQueue[6] = S_SELECT; // S_EXPLOSION_8
										}
									}
								}
							}
						}
						
						if (infiniteShot)
						{
							tempI2 += 250;
						}
						else if (z != MAX_PWEAPON - 1)
						{
							if (tempI2 <= tempI)
							{
								shotAvail[z] = 0;
								goto draw_player_shot_loop_end;
							}
							else
							{
								playerShotData[z].shotDmg -= tempI;
							}
						}
						
					}
				}
			}

draw_player_shot_loop_end:
			;
		}
	}

	/* Player movement indicators for shots that track your ship */
	lastPXShotMove = PX;
	lastPYShotMove = PY;
	
	/*=================================*/
	/*=======Collisions Detection======*/
	/*=================================*/
	
	if (playerAlive && !endLevel)
		JE_playerCollide(&PX, &PY, &lastTurn, &lastTurn2, &score, &armorLevel, &shield, &playerAlive, &playerStillExploding, 1, playerInvulnerable1);
	
	if (twoPlayerMode && playerAliveB && !endLevel)
		JE_playerCollide(&PXB, &PYB, &lastTurnB, &lastTurn2B, &score2, &armorLevel2, &shield2, &playerAliveB, &playerStillExploding2, 2, playerInvulnerable2);
	
	if (firstGameOver)
		JE_mainGamePlayerFunctions();      /*--------PLAYER DRAW+MOVEMENT---------*/
	
	if (!endLevel)
	{    /*MAIN DRAWING IS STOPPED STARTING HERE*/

		/* Draw Enemy Shots */
		for (z = 0; z < ENEMY_SHOT_MAX; z++)
		{
			if (enemyShotAvail[z] == 0)
			{
				enemyShot[z].sxm += enemyShot[z].sxc;
				enemyShot[z].sx += enemyShot[z].sxm;

				if (enemyShot[z].tx != 0)
				{
					if (enemyShot[z].sx > PX)
					{
						if (enemyShot[z].sxm > -enemyShot[z].tx)
						{
							enemyShot[z].sxm--;
						}
					} else {
						if (enemyShot[z].sxm < enemyShot[z].tx)
						{
							enemyShot[z].sxm++;
						}
					}
				}

				enemyShot[z].sym += enemyShot[z].syc;
				enemyShot[z].sy += enemyShot[z].sym;

				if (enemyShot[z].ty != 0)
				{
					if (enemyShot[z].sy > PY)
					{
						if (enemyShot[z].sym > -enemyShot[z].ty)
						{
							enemyShot[z].sym--;
						}
					} else {
						if (enemyShot[z].sym < enemyShot[z].ty)
						{
							enemyShot[z].sym++;
						}
					}
				}

				if (enemyShot[z].duration-- == 0 || enemyShot[z].sy > 190 || enemyShot[z].sy <= -14 || enemyShot[z].sx > 275 || enemyShot[z].sx <= 0)
				{
					enemyShotAvail[z] = 1;
				} else {
					
					if (((temp3 = 1)
					     && playerAlive != 0
					     && enemyShot[z].sx - PX > sAniXNeg && enemyShot[z].sx - PX < sAniX
					     && enemyShot[z].sy - PY > sAniYNeg && enemyShot[z].sy - PY < sAniY)
					 || ((temp3 = 2)
					     && twoPlayerMode != 0
					     && playerAliveB != 0
					     && enemyShot[z].sx - PXB > sAniXNeg && enemyShot[z].sx - PXB < sAniX
					     && enemyShot[z].sy - PYB > sAniYNeg && enemyShot[z].sy - PYB < sAniY))
					{
						tempX = enemyShot[z].sx;
						tempY = enemyShot[z].sy;
						temp = enemyShot[z].sdmg;
						
						enemyShotAvail[z] = 1;
						
						JE_setupExplosion(tempX, tempY, 0, 0, false, false);
						
						switch (temp3)
						{
							case 1:
								if (playerInvulnerable1 == 0)
								{
									if ((temp = JE_playerDamage(tempX, tempY, temp, &PX, &PY, &playerAlive, &playerStillExploding, &armorLevel, &shield)) > 0)
									{
										lastTurn2 += (enemyShot[z].sxm * temp) / 2;
										lastTurn  += (enemyShot[z].sym * temp) / 2;
									}
								}
								break;
							case 2:
								if (playerInvulnerable2 == 0)
								{
									if ((temp = JE_playerDamage(tempX, tempY, temp, &PXB, &PYB, &playerAliveB, &playerStillExploding2, &armorLevel2, &shield2)) > 0)
									{
										lastTurn2B += (enemyShot[z].sxm * temp) / 2;
										lastTurnB  += (enemyShot[z].sym * temp) / 2;
									}
								}
								break;
						}
					} else {
						s = (Uint8 *)VGAScreen->pixels;
						s += enemyShot[z].sy * VGAScreen->pitch + enemyShot[z].sx;

						s_limit = (Uint8 *)VGAScreen->pixels;
						s_limit += VGAScreen->h * VGAScreen->pitch;

						if (enemyShot[z].animax != 0)
						{
							if (++enemyShot[z].animate >= enemyShot[z].animax)
							{
								enemyShot[z].animate = 0;
							}
						}

						if (enemyShot[z].sgr >= 500)
						{
							p = shapesW2;
							p += SDL_SwapLE16(((JE_word *)p)[enemyShot[z].sgr + enemyShot[z].animate - 500 - 1]);
						} else {
							p = shapesC1;
							p += SDL_SwapLE16(((JE_word *)p)[enemyShot[z].sgr + enemyShot[z].animate - 1]);
						}

						while (*p != 0x0f)
						{
							s += *p & 0x0f;
							i = (*p & 0xf0) >> 4;
							if (i)
							{
								while (i--)
								{
									p++;
									if (s >= s_limit)
										goto enemy_shot_draw_overflow;
									if ((void *)s >= VGAScreen->pixels)
										*s = *p;
									s++;
								}
							} else {
								s -= 12;
								s += VGAScreen->pitch;
							}
							p++;
						}
					}

				}

enemy_shot_draw_overflow:
				;
			}
		}
	}
	
	if (background3over == 1)
		draw_background_3(VGAScreen);
	
	/* Draw Top Enemy */
	if (topEnemyOver)
	{
		tempMapXOfs = (background3x1 == 0) ? oldMapX3Ofs : oldMapXOfs;
		tempBackMove = backMove3;
		JE_drawEnemy(75);
	}
	
	/* Draw Sky Enemy */
	if (skyEnemyOverAll)
	{
		lastEnemyOnScreen = enemyOnScreen;
		
		tempMapXOfs = mapX2Ofs;
		tempBackMove = 0;
		JE_drawEnemy(25);
		
		if (enemyOnScreen == lastEnemyOnScreen)
		{
			if (stopBackgroundNum == 2)
				stopBackgroundNum = 9;
		}
	}
	
	/*-------------------------- Sequenced Explosions -------------------------*/
	enemyStillExploding = false;
	for (int i = 0; i < MAX_REPEATING_EXPLOSIONS; i++)
	{
		if (rep_explosions[i].ttl != 0)
		{
			enemyStillExploding = true;
			
			if (rep_explosions[i].delay > 0)
			{
				rep_explosions[i].delay--;
				continue;
			}
			
			rep_explosions[i].y += backMove2 + 1;
			tempX = rep_explosions[i].x + (mt_rand() % 24) - 12;
			tempY = rep_explosions[i].y + (mt_rand() % 27) - 24;
			
			if (rep_explosions[i].big)
			{
				JE_setupExplosionLarge(false, 2, tempX, tempY);
				
				if (rep_explosions[i].ttl == 1 || mt_rand() % 5 == 1)
					soundQueue[7] = S_EXPLOSION_11;
				else
					soundQueue[6] = S_EXPLOSION_9;
				
				rep_explosions[i].delay = 4 + (mt_rand() % 3);
			}
			else
			{
				JE_setupExplosion(tempX, tempY, 0, 1, false, false);
				
				soundQueue[5] = S_EXPLOSION_4;
				
				rep_explosions[i].delay = 3;
			}
			
			rep_explosions[i].ttl--;
		}
	}

	/*---------------------------- Draw Explosions ----------------------------*/
	for (int j = 0; j < MAX_EXPLOSIONS; j++)
	{
		if (explosions[j].ttl != 0)
		{
			if (explosions[j].fixed_position != true)
			{
				explosions[j].sprite++;
				explosions[j].y += explodeMove;
			} else if (explosions[j].follow_player == true) {
				explosions[j].x += explosionFollowAmountX;
				explosions[j].y += explosionFollowAmountY;
			}
			explosions[j].y += explosions[j].delta_y;
			explosions[j].x += explosions[j].delta_x;
			
			s = (Uint8 *)VGAScreen->pixels;
			s += explosions[j].y * VGAScreen->pitch + explosions[j].x;
			
			s_limit = (Uint8 *)VGAScreen->pixels;
			s_limit += VGAScreen->h * VGAScreen->pitch;
			
			if (s + VGAScreen->pitch * 14 > s_limit)
			{
				explosions[j].ttl = 0;
			} else {
				p = shapes6;
				p += SDL_SwapLE16(((JE_word *)p)[explosions[j].sprite]);
				
				if (explosionTransparent)
				{
					while (*p != 0x0f)
					{
						s += *p & 0x0f;
						i = (*p & 0xf0) >> 4;
						if (i)
						{
							while (i--)
							{
								p++;
								if (s >= s_limit)
									goto explosion_draw_overflow;
								if ((void *)s >= VGAScreen->pixels)
									*s = (((*p & 0x0f) + (*s & 0x0f)) >> 1) | (*p & 0xf0);
								s++;
							}
						} else {
							s -= 12;
							s += VGAScreen->pitch;
						}
						p++;
					}
				} else {
					while (*p != 0x0f)
					{
						s += *p & 0x0f;
						i = (*p & 0xf0) >> 4;
						if (i)
						{
							while (i--)
							{
								p++;
								if (s >= s_limit)
									goto explosion_draw_overflow;
								if ((void *)s >= VGAScreen->pixels)
									*s = *p;
								s++;
							}
						} else {
							s -= 12;
							s += VGAScreen->pitch;
						}
						p++;
					}
				}
explosion_draw_overflow:
				
				explosions[j].ttl--;
			}
		}
	}
	
	if (!portConfigChange)
		portConfigDone = true;
	
	
	/*-----------------------BACKGROUNDS------------------------*/
	/*-----------------------BACKGROUND 2------------------------*/
	if (!(smoothies[2-1] && processorType < 4) &&
	    !(smoothies[1-1] && processorType == 3))
	{
		if (background2over == 2)
		{
			if (wild && !background2notTransparent)
				draw_background_2_blend(VGAScreen);
			else
				draw_background_2(VGAScreen);
		}
	}

	/*-------------------------Warning---------------------------*/
	if ((playerAlive && armorLevel < 6) ||
	    (twoPlayerMode && !galagaMode && playerAliveB && armorLevel2 < 6))
	{
		tempW2 = (playerAlive && armorLevel < 6) ? armorLevel : armorLevel2;
		
		if (armorShipDelay > 0)
		{
			armorShipDelay--;
		}
		else
		{
			tempW = 560;
			JE_newEnemy(50);
			if (b > 0)
			{
				enemy[b-1].enemydie = 560 + (mt_rand() % 3) + 1;
				enemy[b-1].eyc -= backMove3;
				enemy[b-1].armorleft = 4;
			}
			armorShipDelay = 500;
		}
		
		if ((playerAlive && armorLevel < 6 && (!isNetworkGame || thisPlayerNum == 1))
		    || (twoPlayerMode && playerAliveB && armorLevel2 < 6 && (!isNetworkGame || thisPlayerNum == 2)))
		{
				
			tempW = tempW2 * 4 + 8;
			if (warningSoundDelay > tempW)
				warningSoundDelay = tempW;
			
			if (warningSoundDelay > 1)
			{
				warningSoundDelay--;
			}
			else
			{
				soundQueue[7] = S_WARNING;
				warningSoundDelay = tempW;
			}
			
			warningCol += warningColChange;
			if (warningCol > 113 + (14 - (tempW2 * 2)))
			{
				warningColChange = -warningColChange;
				warningCol = 113 + (14 - (tempW2 * 2));
			}
			else if (warningCol < 113)
			{
				warningColChange = -warningColChange;
			}
			JE_bar(24, 181, 138, 183, warningCol);
			JE_bar(175, 181, 287, 183, warningCol);
			JE_bar(24, 0, 287, 3, warningCol);
			
			JE_outText(140, 178, "WARNING", 7, (warningCol % 16) / 2);
			
		}
	}

	/*------- Random Explosions --------*/
	if (randomExplosions && mt_rand() % 10 == 1)
		JE_setupExplosionLarge(false, 20, mt_rand() % 280, mt_rand() % 180);
	
	/*=================================*/
	/*=======The Sound Routine=========*/
	/*=================================*/
	if (firstGameOver)
	{
		temp = 0;
		for (temp2 = 0; temp2 < SFX_CHANNELS; temp2++)
		{
			if (soundQueue[temp2] != S_NONE)
			{
				temp = soundQueue[temp2];
				if (temp2 == 3)
					temp3 = fxPlayVol;
				else if (temp == 15)
					temp3 = fxPlayVol / 4;
				else   /*Lightning*/
					temp3 = fxPlayVol / 2;
				
				JE_multiSamplePlay(digiFx[temp-1], fxSize[temp-1], temp2, temp3);
				
				soundQueue[temp2] = S_NONE;
			}
		}
	}
	
	if (returnActive && enemyOnScreen == 0)
	{
		JE_eventJump(65535);
		returnActive = false;
	}

	/*-------      DEbug      ---------*/
	debugTime = SDL_GetTicks();
	tempW = lastmouse_but;
	tempX = mouse_x;
	tempY = mouse_y;

	if (debug)
	{
		strcpy(tempStr, "");
		for (temp = 0; temp < 9; temp++)
		{
			sprintf(tempStr, "%s%c", tempStr,  smoothies[temp] + 48);
		}
		sprintf(buffer, "SM = %s", tempStr);
		JE_outText(30, 70, buffer, 4, 0);

		sprintf(buffer, "Memory left = %d", -1);
		JE_outText(30, 80, buffer, 4, 0);
		sprintf(buffer, "Enemies onscreen = %d", enemyOnScreen);
		JE_outText(30, 90, buffer, 6, 0);

		debugHist = debugHist + abs((JE_longint)debugTime - (JE_longint)lastDebugTime);
		debugHistCount++;
		sprintf(tempStr, "%2.3f", 1000.0f / round(debugHist / debugHistCount));
		sprintf(buffer, "X:%d Y:%-5d  %s FPS  %d %d %d %d", (mapX - 1) * 12 + PX, curLoc, tempStr, lastTurn2, lastTurn, PX, PY);
		JE_outText(45, 175, buffer, 15, 3);
		lastDebugTime = debugTime;
	}

	if (displayTime > 0)
	{
		displayTime--;
		JE_outTextAndDarken(90, 10, miscText[59], 15, (JE_byte)flash - 8, FONT_SHAPES);
		flash += flashChange;
		if (flash > 4 || flash == 0)
			flashChange = -flashChange;
	}

	/*Pentium Speed Mode?*/
	if (pentiumMode)
	{
		frameCountMax = (frameCountMax == 2) ? 3 : 2;
	}
	
	/*--------  Level Timer    ---------*/
	if (levelTimer && levelTimerCountdown > 0)
	{
		levelTimerCountdown--;
		if (levelTimerCountdown == 0)
			JE_eventJump(levelTimerJumpTo);
		
		if (levelTimerCountdown > 200)
		{
			if (levelTimerCountdown % 100 == 0)
				soundQueue[7] = S_WARNING;
			
			if (levelTimerCountdown % 10 == 0)
				soundQueue[6] = S_CLICK;
		}
		else if (levelTimerCountdown % 20 == 0)
		{
			soundQueue[7] = S_WARNING;
		}
		
		JE_textShade (140, 6, miscText[66], 7, (levelTimerCountdown % 20) / 3, FULL_SHADE);
		sprintf(buffer, "%.1f", levelTimerCountdown / 100.0f);
		JE_dString (100, 2, buffer, SMALL_FONT_SHAPES);
	}
	
	/*GAME OVER*/
	if (!constantPlay && !constantDie)
	{
		if (allPlayersGone)
		{
			if (playerStillExploding > 0 || playerStillExploding2 > 0)
			{
				if (galagaMode)
					playerStillExploding2 = 0;
				
				musicFade = true;
			}
			else
			{
				if (play_demo || normalBonusLevelCurrent || bonusLevelCurrent)
					reallyEndLevel = true;
				else
					JE_dString(120, 60, miscText[21], FONT_SHAPES); // game over
				
				set_mouse_position(159, 100);
				if (firstGameOver)
				{
					if (!play_demo)
					{
						play_song(SONG_GAMEOVER);
						set_volume(tyrMusicVolume, fxVolume);
					}
					firstGameOver = false;
				}

				if (!play_demo)
				{
					push_joysticks_as_keyboard();
					service_SDL_events(true);
					if ((newkey || button[0] || button[1] || button[2]) || newmouse)
					{
						reallyEndLevel = true;
					}
				}

				if (isNetworkGame)
					reallyEndLevel = true;
			}
		}
	}

	if (play_demo) // input kills demo
	{
		push_joysticks_as_keyboard();
		service_SDL_events(false);
		
		if (newkey || newmouse)
		{
			reallyEndLevel = true;
			
			stopped_demo = true;
			skip_intro_logos = true;
		}
	}
	else // input handling for pausing, menu, cheats
	{
		service_SDL_events(false);
		
		if (newkey)
		{
			skipStarShowVGA = false;
			JE_mainKeyboardInput();
			newkey = false;
			if (skipStarShowVGA)
				goto level_loop;
		}
		
		if (pause_pressed)
		{
			pause_pressed = false;
			
			if (isNetworkGame)
				pauseRequest = true;
			else
				JE_pauseGame();
		}
		
		if (ingamemenu_pressed)
		{
			ingamemenu_pressed = false;
			
			if (isNetworkGame)
			{
				inGameMenuRequest = true;
			}
			else
			{
				yourInGameMenuRequest = true;
				JE_doInGameSetup();
				skipStarShowVGA = true;
			}
		}
	}
	
	/*Network Update*/
	if (isNetworkGame)
	{
		if (!reallyEndLevel)
		{
			Uint16 requests = (pauseRequest == true) |
			                  (inGameMenuRequest == true) << 1 |
			                  (skipLevelRequest == true) << 2 |
			                  (nortShipRequest == true) << 3;
			SDLNet_Write16(requests,        &packet_state_out[0]->data[14]);
			
			SDLNet_Write16(difficultyLevel, &packet_state_out[0]->data[16]);
			SDLNet_Write16(PX,              &packet_state_out[0]->data[18]);
			SDLNet_Write16(PXB,             &packet_state_out[0]->data[20]);
			SDLNet_Write16(PY,              &packet_state_out[0]->data[22]);
			SDLNet_Write16(PYB,             &packet_state_out[0]->data[24]);
			SDLNet_Write16(curLoc,          &packet_state_out[0]->data[26]);
			
			network_state_send();
			
			if (network_state_update())
			{
				assert(SDLNet_Read16(&packet_state_in[0]->data[26]) == SDLNet_Read16(&packet_state_out[network_delay]->data[26]));
				
				requests = SDLNet_Read16(&packet_state_in[0]->data[14]) ^ SDLNet_Read16(&packet_state_out[network_delay]->data[14]);
				if (requests & 1)
				{
					JE_pauseGame();
				}
				if (requests & 2)
				{
					yourInGameMenuRequest = SDLNet_Read16(&packet_state_out[network_delay]->data[14]) & 2;
					JE_doInGameSetup();
					yourInGameMenuRequest = false;
					if (haltGame)
						reallyEndLevel = true;
				}
				if (requests & 4)
				{
					levelTimer = true;
					levelTimerCountdown = 0;
					endLevel = true;
					levelEnd = 40;
				}
				if (requests & 8)
				{
					pItems[P_SHIP] = 12;
					pItems[P_SPECIAL] = 13;
					pItems[P_FRONT] = 36;
					pItems[P_REAR] = 37;
					shipGr = 1;
				}
				
				for (int i = 0; i < 2; i++)
				{
					if (SDLNet_Read16(&packet_state_in[0]->data[18 + i * 2]) != SDLNet_Read16(&packet_state_out[network_delay]->data[18 + i * 2]) || SDLNet_Read16(&packet_state_in[0]->data[20 + i * 2]) != SDLNet_Read16(&packet_state_out[network_delay]->data[20 + i * 2]))
					{
						char temp[64];
						sprintf(temp, "Player %d is unsynchronized!", i + 1);
						
						tempScreenSeg = game_screen;
						JE_textShade(40, 110 + i * 10, temp, 9, 2, FULL_SHADE);
						tempScreenSeg = VGAScreen;
					}
				}
			}
		}
		
		JE_clearSpecialRequests();
	}

	/** Test **/
	JE_drawSP();

	/*Filtration*/
	if (filterActive)
	{
		JE_filterScreen(levelFilter, levelBrightness);
	}

	draw_boss_bar();

	JE_inGameDisplays();

	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

	JE_starShowVGA();

	/*Start backgrounds if no enemies on screen
	  End level if number of enemies left to kill equals 0.*/
	if (stopBackgroundNum == 9 && backMove == 0 && !enemyStillExploding)
	{
		backMove = 1;
		backMove2 = 2;
		backMove3 = 3;
		explodeMove = 2;
		stopBackgroundNum = 0;
		stopBackgrounds = false;
		if (waitToEndLevel)
		{
			endLevel = true;
			levelEnd = 40;
		}
		if (allPlayersGone)
		{
			reallyEndLevel = true;
		}
	}

	if (!endLevel && enemyOnScreen == 0)
	{
		if (readyToEndLevel && !enemyStillExploding)
		{
			if (levelTimerCountdown > 0)
			{
				levelTimer = false;
			}
			readyToEndLevel = false;
			endLevel = true;
			levelEnd = 40;
			if (allPlayersGone)
			{
				reallyEndLevel = true;
			}
		}
		if (stopBackgrounds)
		{
			stopBackgrounds = false;
			backMove = 1;
			backMove2 = 2;
			backMove3 = 3;
			explodeMove = 2;
		}
	}


	/*Other Network Functions*/
	JE_handleChat();

	if (reallyEndLevel)
	{
		goto start_level;
	}
	goto level_loop;
}

/* --- Load Level/Map Data --- */
void JE_loadMap( void )
{

	FILE *lvlFile, *shpFile;
/*	FILE *tempFile;*/ /*Extract map file from LVL file*/


	JE_char char_mapFile, char_shapeFile;

	JE_DanCShape shape;
	JE_boolean shapeBlank;


	FILE *f;
	JE_char k2, k3;
	JE_word x, y;
	JE_integer yy, z, a, b;
	JE_word mapSh[3][128]; /* [1..3, 0..127] */
	JE_byte *ref[3][128]; /* [1..3, 0..127] */
	char s[256];
	JE_byte col, planets, shade;



	JE_byte mapBuf[15 * 600]; /* [1..15 * 600] */
	JE_word bufLoc;

	char buffer[256];
	int i;
	Uint8 pic_buffer[320*200]; /* screen buffer, 8-bit specific */
	Uint8 *vga, *pic, *vga2; /* screen pointer, 8-bit specific */

	lastCubeMax = cubeMax;

	/*Defaults*/
	songBuy = DEFAULT_SONG_BUY;  /*Item Screen default song*/
	
	if (loadTitleScreen || play_demo)
	{
		if (!skip_intro_logos && !isNetworkGame)
			intro_logos();
		
		JE_initPlayerData();
		JE_sortHighScores();
		
		moveTyrianLogoUp = true;
		JE_titleScreen(true);
		
		loadTitleScreen = false;
	}

	/* Load LEVELS.DAT - Section = MAINLEVEL */
	saveLevel = mainLevel;

new_game:
	galagaMode  = false;
	useLastBank = false;
	extraGame   = false;
	haltGame = false;
	
	if (loadTitleScreen)
	{
		JE_initPlayerData();
		JE_sortHighScores();
		
		JE_titleScreen(true);
		loadTitleScreen = false;
	}
	
	gameLoaded = false;
	
	first = true;
	
	if (loadDestruct)
		return;
	
	if (!play_demo)
	{
		do
		{
			JE_resetFile(&lvlFile, macroFile);

			x = 0;
			jumpSection = false;
			loadLevelOk = false;

			/* Seek Section # Mainlevel */
			while (x < mainLevel)
			{
				JE_readCryptLn(lvlFile, s);
				if (s[0] == '*')
				{
					x++;
					s[0] = ' ';
				}
			}

			ESCPressed = false;

			do
			{

				if (gameLoaded)
				{
					if (mainLevel == 0)
					{
						loadTitleScreen = true;
					}
					fclose(lvlFile);
					goto new_game;
				}

				strcpy(s, " ");
				JE_readCryptLn(lvlFile, s);

				switch (s[0])
				{
					case ']':
						switch (s[1])
						{
							case 'A':
								JE_playAnim("tyrend.anm", 1, true, 7);
								break;

							case 'G':
								mapOrigin = atoi(strnztcpy(buffer, s + 4, 2));
								mapPNum   = atoi(strnztcpy(buffer, s + 7, 1));
								for (i = 0; i < mapPNum; i++)
								{
									mapPlanet[i] = atoi(strnztcpy(buffer, s + 1 + (i + 1) * 8, 2));
									mapSection[i] = atoi(strnztcpy(buffer, s + 4 + (i + 1) * 8, 3));
								}
								break;

							case '?':
								temp = atoi(strnztcpy(buffer, s + 4, 2));
								for (i = 0; i < temp; i++)
								{
									cubeList[i] = atoi(strnztcpy(buffer, s + 3 + (i + 1) * 4, 3));
								}
								if (cubeMax > temp)
								{
									cubeMax = temp;
								}
								break;

							case '!':
								cubeMax = atoi(strnztcpy(buffer, s + 4, 2));    /*Auto set CubeMax*/
								break;
							case '+':
								temp = atoi(strnztcpy(buffer, s + 4, 2));
								cubeMax += temp;
								if (cubeMax > 4)
								{
									cubeMax = 4;
								}
								break;

							case 'g':
								galagaMode = true;   /*GALAGA mode*/
								memcpy(&pItemsPlayer2, &pItems, sizeof(pItemsPlayer2));
								pItemsPlayer2[P_REAR] = 15; /*Player 2 starts with 15 - MultiCannon and 2 single shot options*/
								pItemsPlayer2[P_LEFT_SIDEKICK] = 0;
								pItemsPlayer2[P_RIGHT_SIDEKICK] = 0;
								break;

							case 'x':
								extraGame = true;
								break;

							case 'e': // ENGAGE mode, used for mini-games
								doNotSaveBackup = true;
								constantDie = false;
								onePlayerAction = true;
								superTyrian = true;
								twoPlayerMode = false;

								score = 0;

								pItems[P_SHIP] = 13;           // The Stalker 21.126
								pItems[P_FRONT] = 39;          // Atomic RailGun
								pItems[P_REAR] = 0;            // None
								pItems[P_LEFT_SIDEKICK] = 0;   // None
								pItems[P_RIGHT_SIDEKICK] = 0;  // None
								pItems[P_GENERATOR] = 2;       // Advanced MR-12
								pItems[P_SHIELD] = 4;          // Advanced Integrity Field
								pItems[P_SPECIAL] = 0;         // None
								pItems[P2_SIDEKICK_MODE] = 2;  // not sure
								pItems[P2_SIDEKICK_TYPE] = 1;  // not sure
								
								portPower[0] = 3;
								portPower[1] = 0;
								break;

							case 'J':  // section jump
								temp = atoi(strnztcpy(buffer, s + 3, 3));
								mainLevel = temp;
								jumpSection = true;
								break;
							case '2':  // two-player section jump
								temp = atoi(strnztcpy(buffer, s + 3, 3));
								if (twoPlayerMode || onePlayerAction)
								{
									mainLevel = temp;
									jumpSection = true;
								}
								break;
							case 'w':  // Stalker 21.126 section jump
								temp = atoi(strnztcpy(buffer, s + 3, 3));   /*Allowed to go to Time War?*/
								if (pItems[P_SHIP] == 13)
								{
									mainLevel = temp;
									jumpSection = true;
								}
								break;
							case 't':
								temp = atoi(strnztcpy(buffer, s + 3, 3));
								if (levelTimer && levelTimerCountdown == 0)
								{
									mainLevel = temp;
									jumpSection = true;
								}
								break;
							case 'l':
								temp = atoi(strnztcpy(buffer, s + 3, 3));
								if (!playerAlive || (twoPlayerMode && !playerAliveB))
								{
									mainLevel = temp;
									jumpSection = true;
								}
								break;
							case 's':
								saveLevel = mainLevel;
								break; /*store savepoint*/
							case 'b':
								if (twoPlayerMode)
								{
									temp = 22;
								} else {
									temp = 11;
								}
								JE_saveGame(11, "LAST LEVEL    ");
								break;

							case 'i':
								temp = atoi(strnztcpy(buffer, s + 3, 3));
								songBuy = temp - 1;
								break;
							case 'I': /*Load Items Available Information*/

								memset(&itemAvail, 0, sizeof(itemAvail));

								for (temp = 0; temp < 9; temp++)
								{
									char buf[256];

									JE_readCryptLn(lvlFile, s);

									sprintf(buf, "%s ", (strlen(s) > 8 ? s+8 : ""));
									/*strcat(strcpy(s, s + 8), " ");*/
									temp2 = 0;
									while (JE_getNumber(buf, &itemAvail[temp][temp2]))
									{
										temp2++;
									}

									itemAvailMax[temp] = temp2;
								}

								JE_itemScreen();
								break;

							case 'L':
								nextLevel = atoi(strnztcpy(buffer, s + 9, 3));
								strnztcpy(levelName, s + 13, 9);
								levelSong = atoi(strnztcpy(buffer, s + 22, 2));
								if (nextLevel == 0)
								{
									nextLevel = mainLevel + 1;
								}
								lvlFileNum = atoi(strnztcpy(buffer, s + 25, 2));
								loadLevelOk = true;
								bonusLevelCurrent = (strlen(s) > 28) & (s[28] == '$');
								normalBonusLevelCurrent = (strlen(s) > 27) & (s[27] == '$');
								gameJustLoaded = false;
								break;

							case '@':
								useLastBank = !useLastBank;
								break;

							case 'Q':
								ESCPressed = false;
								temp = secretHint + (mt_rand() % 3) * 3;

								if (twoPlayerMode)
								{
									sprintf(levelWarningText[0], "%s %d", miscText[40], score);
									sprintf(levelWarningText[1], "%s %d", miscText[41], score2);
									strcpy(levelWarningText[2], "");
									levelWarningLines = 3;
								}
								else
								{
									sprintf(levelWarningText[0], "%s %d", miscText[37], JE_totalScore(score, pItems));
									strcpy(levelWarningText[1], "");
									levelWarningLines = 2;
								}

								for (x = 0; x < temp - 1; x++)
								{
									do
										JE_readCryptLn(lvlFile, s);
									while (s[0] != '#');
								}

								do
								{
									JE_readCryptLn(lvlFile, s);
									strcpy(levelWarningText[levelWarningLines], s);
									levelWarningLines++;
								}
								while (s[0] != '#');
								levelWarningLines--;

								JE_wipeKey();
								frameCountMax = 4;
								if (!constantPlay)
									JE_displayText();

								JE_fadeBlack(15);

								JE_nextEpisode();

								if (jumpBackToEpisode1 && !twoPlayerMode)
								{
									JE_loadPic(1, false); // huh?
									JE_clr256();
									
									if (superTyrian)
									{
										// if completed Zinglon's Revenge, show SuperTyrian and Destruct codes
										// if completed SuperTyrian, show Nort-Ship Z code
										
										superArcadeMode = (initialDifficulty == 8) ? 8 : 1;
										
										jumpSection = true;
										loadTitleScreen = true;
									}
									
									if (superArcadeMode < SA_ENGAGE)
									{
										if (SANextShip[superArcadeMode] == SA_ENGAGE)
										{
											sprintf(buffer, "%s %s", miscTextB[4], pName[0]);
											JE_dString(JE_fontCenter(buffer, FONT_SHAPES), 100, buffer, FONT_SHAPES);
											
											sprintf(buffer, "Or play... %s", specialName[7]);
											JE_dString(80, 180, buffer, SMALL_FONT_SHAPES);
										}
										else
										{
											JE_dString(JE_fontCenter(superShips[0], FONT_SHAPES), 30, superShips[0], FONT_SHAPES);
											JE_dString(JE_fontCenter(superShips[SANextShip[superArcadeMode]], SMALL_FONT_SHAPES), 100, superShips[SANextShip[superArcadeMode]], SMALL_FONT_SHAPES);
										}
										
										if (SANextShip[superArcadeMode] < SA_NORTSHIPZ)
											JE_drawShape2x2(148, 70, ships[SAShip[SANextShip[superArcadeMode]-1]].shipgraphic, shapes9);
										else if (SANextShip[superArcadeMode] == SA_NORTSHIPZ)
											trentWin = true;
										
										sprintf(buffer, "Type %s at Title", specialName[SANextShip[superArcadeMode]-1]);
										JE_dString(JE_fontCenter(buffer, SMALL_FONT_SHAPES), 160, buffer, SMALL_FONT_SHAPES);
										JE_showVGA();
										
										JE_fadeColor(50);
										
										if (!constantPlay)
											wait_input(true, true, true);
									}

									jumpSection = true;
									
									if (isNetworkGame)
										JE_readTextSync();
									
									if (superTyrian)
										JE_fadeBlack(10);
								}
								break;

							case 'P':
								if (!constantPlay)
								{
									tempX = atoi(strnztcpy(buffer, s + 3, 3));
									if (tempX > 900)
									{
										memcpy(colors, palettes[pcxpal[tempX-1 - 900]], sizeof(colors));
										JE_clr256();
										JE_showVGA();
										JE_fadeColor(1);
									} else {
										if (tempX == 0)
										{
											JE_loadPCX("tshp2.pcx");
										} else {
											JE_loadPic(tempX, false);
										}
										JE_showVGA();
										JE_fadeColor(10);
									}
								}
								break;

							case 'U':
								if (!constantPlay)
								{
									memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

									tempX = atoi(strnztcpy(buffer, s + 3, 3));
									JE_loadPic(tempX, false);
									memcpy(pic_buffer, VGAScreen->pixels, sizeof(pic_buffer));

									service_SDL_events(true);
									
									for (z = 0; z <= 199; z++)
									{
										if (!newkey)
										{
											vga = VGAScreen->pixels;
											vga2 = VGAScreen2->pixels;
											pic = pic_buffer + (199 - z) * 320;

											setjasondelay(1); /* attempting to emulate JE_waitRetrace();*/
											
											for (y = 0; y < 199; y++)
											{
												if (y <= z)
												{
													memcpy(vga, pic, 320);
													pic += 320;
												} else {
													memcpy(vga, vga2, VGAScreen->pitch);
													vga2 += VGAScreen->pitch;
												}
												vga += VGAScreen->pitch;
											}
											
											JE_showVGA();
											
											if (isNetworkGame)
											{
												/* TODO: NETWORK */
											}
											
											service_wait_delay();
										}
									}
									
									memcpy(VGAScreen->pixels, pic_buffer, sizeof(pic_buffer));
								}
								break;

							case 'V':
								if (!constantPlay)
								{
									/* TODO: NETWORK */
									memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

									tempX = atoi(strnztcpy(buffer, s + 3, 3));
									JE_loadPic(tempX, false);
									memcpy(pic_buffer, VGAScreen->pixels, sizeof(pic_buffer));

									service_SDL_events(true);
									for (z = 0; z <= 199; z++)
									{
										if (!newkey)
										{
											vga = VGAScreen->pixels;
											vga2 = VGAScreen2->pixels;
											pic = pic_buffer;
											
											setjasondelay(1); /* attempting to emulate JE_waitRetrace();*/
											
											for (y = 0; y < 199; y++)
											{
												if (y <= 199 - z)
												{
													memcpy(vga, vga2, VGAScreen->pitch);
													vga2 += VGAScreen->pitch;
												} else {
													memcpy(vga, pic, 320);
													pic += 320;
												}
												vga += VGAScreen->pitch;
											}
											
											JE_showVGA();
											
											if (isNetworkGame)
											{
												/* TODO: NETWORK */
											}
											
											service_wait_delay();
										}
									}
									
									memcpy(VGAScreen->pixels, pic_buffer, sizeof(pic_buffer));
								}
								break;

							case 'R':
								if (!constantPlay)
								{
									/* TODO: NETWORK */
									memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

									tempX = atoi(strnztcpy(buffer, s + 3, 3));
									JE_loadPic(tempX, false);
									memcpy(pic_buffer, VGAScreen->pixels, sizeof(pic_buffer));

									service_SDL_events(true);
									
									for (z = 0; z <= 318; z++)
									{
										if (!newkey)
										{
											vga = VGAScreen->pixels;
											vga2 = VGAScreen2->pixels;
											pic = pic_buffer;
											
											setjasondelay(1); /* attempting to emulate JE_waitRetrace();*/
											
											for(y = 0; y < 200; y++)
											{
												memcpy(vga, vga2 + z, 319 - z);
												vga += 320 - z;
												vga2 += VGAScreen2->pitch;
												memcpy(vga, pic, z + 1);
												vga += z;
												pic += 320;
											}
											
											JE_showVGA();
											
											if (isNetworkGame)
											{
												/* TODO: NETWORK */
											}
											
											service_wait_delay();
										}
									}
									
									memcpy(VGAScreen->pixels, pic_buffer, sizeof(pic_buffer));
								}
								break;

							case 'C':
								if (!isNetworkGame)
								{
									JE_fadeBlack(10);
								}
								JE_clr256();
								JE_showVGA();
								memcpy(colors, palettes[7], sizeof(colors));
								JE_updateColorsFast(colors);
								break;

							case 'B':
								if (!isNetworkGame)
								{
									JE_fadeBlack(10);
								}
								break;
							case 'F':
								if (!isNetworkGame)
								{
									JE_fadeWhite(100);
									JE_fadeBlack(30);
								}
								JE_clr256();
								JE_showVGA();
								break;

							case 'W':
								if (!constantPlay)
								{
									if (!ESCPressed)
									{
										JE_wipeKey();
										warningCol = 14 * 16 + 5;
										warningColChange = 1;
										warningSoundDelay = 0;
										levelWarningDisplay = (s[2] == 'y');
										levelWarningLines = 0;
										frameCountMax = atoi(strnztcpy(buffer, s + 4, 2));
										setjasondelay2(6);
										warningRed = frameCountMax / 10;
										frameCountMax = frameCountMax % 10;

										do
										{
											JE_readCryptLn(lvlFile, s);

											if (s[0] != '#')
											{
												strcpy(levelWarningText[levelWarningLines], s);
												levelWarningLines++;
											}
										} while (!(s[0] == '#'));

										JE_displayText();
										newkey = false;
									}
								}
								break;

							case 'H':
								if (initialDifficulty < 3)
								{
									mainLevel = atoi(strnztcpy(buffer, s + 4, 3));
									jumpSection = true;
								}
								break;

							case 'h':
								if (initialDifficulty > 2)
								{
									JE_readCryptLn(lvlFile, s);
								}
								break;

							case 'S':
								if (isNetworkGame)
								{
									JE_readTextSync();
								}
								break;

							case 'n':
								ESCPressed = false;
								break;

							case 'M':
								temp = atoi(strnztcpy(buffer, s + 3, 3));
								play_song(temp - 1);
								break;
							#ifdef TYRIAN2000
							case 'T':
								/* TODO: Timed Battle ]T[ 43 44 45 46 47 */
								printf("]T[ 43 44 45 46 47 handle timed battle!");
								break;
								
							case 'q':
								/* TODO: Timed Battle end */
								printf("handle timed battle end flag!");
								break;
							#endif
						}
					break;
				}


			} while (!(loadLevelOk || jumpSection));


			fclose(lvlFile);

		} while (!loadLevelOk);
	}
	
	if (play_demo)
		load_next_demo();
	else
		fade_black(50);
	
	JE_resetFile(&lvlFile, levelFile);
	fseek(lvlFile, lvlPos[(lvlFileNum-1) * 2], SEEK_SET);
	
	char_mapFile = fgetc(lvlFile);
	char_shapeFile = fgetc(lvlFile);
	efread(&mapX,  sizeof(JE_word), 1, lvlFile);
	efread(&mapX2, sizeof(JE_word), 1, lvlFile);
	efread(&mapX3, sizeof(JE_word), 1, lvlFile);
	
	efread(&levelEnemyMax, sizeof(JE_word), 1, lvlFile);
	for (x = 0; x < levelEnemyMax; x++)
	{
		efread(&levelEnemy[x], sizeof(JE_word), 1, lvlFile);
	}
	
	efread(&maxEvent, sizeof(JE_word), 1, lvlFile);
	for (x = 0; x < maxEvent; x++)
	{
		efread(&eventRec[x].eventtime, sizeof(JE_word), 1, lvlFile);
		efread(&eventRec[x].eventtype, sizeof(JE_byte), 1, lvlFile);
		efread(&eventRec[x].eventdat,  sizeof(JE_integer), 1, lvlFile);
		efread(&eventRec[x].eventdat2, sizeof(JE_integer), 1, lvlFile);
		efread(&eventRec[x].eventdat3, sizeof(JE_shortint), 1, lvlFile);
		efread(&eventRec[x].eventdat5, sizeof(JE_shortint), 1, lvlFile);
		efread(&eventRec[x].eventdat6, sizeof(JE_shortint), 1, lvlFile);
		efread(&eventRec[x].eventdat4, sizeof(JE_byte), 1, lvlFile);
	}
	eventRec[x].eventtime = 65500;  /*Not needed but just in case*/
	
	/*debuginfo('Level loaded.');*/
	
	/*debuginfo('Loading Map');*/
	
	/* MAP SHAPE LOOKUP TABLE - Each map is directly after level */
	efread(mapSh, sizeof(JE_word), sizeof(mapSh) / sizeof(JE_word), lvlFile);
	for (temp = 0; temp < 3; temp++)
	{
		for (temp2 = 0; temp2 < 128; temp2++)
		{
			mapSh[temp][temp2] = SDL_Swap16(mapSh[temp][temp2]);
		}
	}
	
	/* Read Shapes.DAT */
	sprintf(tempStr, "shapes%c.dat", tolower(char_shapeFile));

	printf("reading %s file tyrian2.c\n", tempStr);

	JE_resetFile(&shpFile, tempStr);
	
	for (z = 0; z < 600; z++)
	{
		shapeBlank = fgetc(shpFile);
		
		if (shapeBlank)
		{
			memset(shape, 0, sizeof(shape));
		} else {
			efread(shape, sizeof(JE_byte), sizeof(shape), shpFile);
		}
		
		/* Match 1 */
		for (x = 0; x <= 71; x++)
		{
			if (mapSh[0][x] == z+1)
			{
				memcpy(megaData1->shapes[x].sh, shape, sizeof(JE_DanCShape));
				
				ref[0][x] = (JE_byte *)megaData1->shapes[x].sh;
			}
		}
		
		/* Match 2 */
		for (x = 0; x <= 71; x++)
		{
			if (mapSh[1][x] == z+1)
			{
				if (x != 71 && !shapeBlank)
				{
					memcpy(megaData2->shapes[x].sh, shape, sizeof(JE_DanCShape));
					
					y = 1;
					for (yy = 0; yy < (24 * 28) >> 1; yy++)
					{
						if (shape[yy] == 0)
						{
							y = 0;
						}
					}
					
					megaData2->shapes[x].fill = y;
					ref[1][x] = (JE_byte *)megaData2->shapes[x].sh;
				} else {
					ref[1][x] = NULL;
				}
			}
		}
		
		/*Match 3*/
		for (x = 0; x <= 71; x++)
		{
			if (mapSh[2][x] == z+1)
			{
				if (x < 70 && !shapeBlank)
				{
					memcpy(megaData3->shapes[x].sh, shape, sizeof(JE_DanCShape));
					
					y = 1;
					for (yy = 0; yy < (24 * 28) >> 1; yy++)
					{
						if (shape[yy] == 0)
						{
							y = 0;
						}
					}
					
					megaData3->shapes[x].fill = y;
					ref[2][x] = (JE_byte *)megaData3->shapes[x].sh;
				} else {
					ref[2][x] = NULL;
				}
			}
		}
	}
	
	fclose(shpFile);
	
	efread(mapBuf, sizeof(JE_byte), 14 * 300, lvlFile);
	bufLoc = 0;              /* MAP NUMBER 1 */
	for (y = 0; y < 300; y++)
	{
		for (x = 0; x < 14; x++)
		{
			megaData1->mainmap[y][x] = ref[0][mapBuf[bufLoc]];
			bufLoc++;
		}
	}
	
	efread(mapBuf, sizeof(JE_byte), 14 * 600, lvlFile);
	bufLoc = 0;              /* MAP NUMBER 2 */
	for (y = 0; y < 600; y++)
	{
		for (x = 0; x < 14; x++)
		{
			megaData2->mainmap[y][x] = ref[1][mapBuf[bufLoc]];
			bufLoc++;
		}
	}
	
	efread(mapBuf, sizeof(JE_byte), 15 * 600, lvlFile);
	bufLoc = 0;              /* MAP NUMBER 3 */
	for (y = 0; y < 600; y++)
	{
		for (x = 0; x < 15; x++)
		{
			megaData3->mainmap[y][x] = ref[2][mapBuf[bufLoc]];
			bufLoc++;
		}
	}
	
	fclose(lvlFile);
	
	/* Note: The map data is automatically calculated with the correct mapsh
	value and then the pointer is calculated using the formula (MAPSH-1)*168.
	Then, we'll automatically add S2Ofs to get the exact offset location into
	the shape table! This makes it VERY FAST! */
	
	/*debuginfo('Map file done.');*/
	/* End of find loop for LEVEL??.DAT */
}

void JE_titleScreen( JE_boolean animate )
{
	JE_boolean quit = 0;

    #ifdef TYRIAN2000
	const int menunum = 6;
	#else
	const int menunum = 7;
	#endif
	
	unsigned int arcade_code_i[SA_ENGAGE] = { 0 };
	
	JE_word waitForDemo;
	JE_byte menu = 0;
	JE_boolean redraw = true,
	           fadeIn = false,
	           first = true;
	JE_char flash;
	JE_word z;

	JE_word temp; /* JE_byte temp; from varz.h will overflow in for loop */

	if (haltGame)
		JE_tyrianHalt(0);

	tempScreenSeg = VGAScreen;

	play_demo = false;
	stopped_demo = false;

	first  = true;
	redraw = true;
	fadeIn = false;

	gameLoaded = false;
	jumpSection = false;

	if (isNetworkGame)
	{
		JE_loadPic(2, false);
		memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);
		JE_dString(JE_fontCenter("Waiting for other player.", SMALL_FONT_SHAPES), 140, "Waiting for other player.", SMALL_FONT_SHAPES);
		JE_showVGA();
		JE_fadeColor(10);
		
		network_connect();
		
		twoPlayerMode = true;
		if (thisPlayerNum == 1)
		{
			JE_fadeBlack(10);
			
			if (select_episode() && select_difficulty())
			{
				initialDifficulty = difficultyLevel;
				
				difficultyLevel++;  /*Make it one step harder for 2-player mode!*/
				
				network_prepare(PACKET_DETAILS);
				SDLNet_Write16(episodeNum,      &packet_out_temp->data[4]);
				SDLNet_Write16(difficultyLevel, &packet_out_temp->data[6]);
				network_send(8);  // PACKET_DETAILS
			}
			else
			{
				network_prepare(PACKET_QUIT);
				network_send(4);  // PACKET QUIT
				
				network_tyrian_halt(0, true);
			}
		}
		else
		{
			memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
			JE_dString(JE_fontCenter(networkText[4-1], SMALL_FONT_SHAPES), 140, networkText[4-1], SMALL_FONT_SHAPES);
			JE_showVGA();
			
			// until opponent sends details packet
			while (true)
			{
				service_SDL_events(false);
				JE_showVGA();
				
				if (packet_in[0] && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_DETAILS)
					break;
				
				network_update();
				network_check();
				
				SDL_Delay(16);
			}
			
			JE_initEpisode(SDLNet_Read16(&packet_in[0]->data[4]));
			difficultyLevel = SDLNet_Read16(&packet_in[0]->data[6]);
			initialDifficulty = difficultyLevel - 1;
			JE_fadeBlack(10);
			
			network_update();
		}
		
		score = 0;
		score2 = 0;
			
		pItems[P_SHIP] = 11;
		
		while (!network_is_sync())
		{
			service_SDL_events(false);
			JE_showVGA();
			
			network_check();
			SDL_Delay(16);
		}
	}
	else
	{
		do
		{
			defaultBrightness = -3;
			
			/* Animate instead of quickly fading in */
			if (redraw)
			{
				play_song(SONG_TITLE);
				
				menu = 0;
				redraw = false;
				if (animate)
				{
					if (fadeIn)
					{
						JE_fadeBlack(10);
						fadeIn = false;
					}
					
					JE_loadPic(4, false);
					
					JE_textShade(2, 192, opentyrian_version, 15, 0, PART_SHADE);
					
					memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);
					
					temp = moveTyrianLogoUp ? 62 : 4;
					
					blit_shape(VGAScreenSeg, 11, temp, PLANET_SHAPES, 146); // tyrian logo
					
					JE_showVGA();
					
					fade_palette(colors, 10, 0, 255 - 16);
					
					if (moveTyrianLogoUp)
					{
						for (temp = 61; temp >= 4; temp -= 2)
						{
							setjasondelay(2);
							
							memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
							
							blit_shape(VGAScreenSeg, 11, temp, PLANET_SHAPES, 146); // tyrian logo
							
							JE_showVGA();
							
							service_wait_delay();
						}
						moveTyrianLogoUp = false;
					}
					
					/* Draw Menu Text on Screen */
					for (temp = 0; temp < menunum; temp++)
					{
						tempX = 104+(temp)*13;
						if (temp == 4) /* OpenTyrian override */
						{
							tempY = JE_fontCenter(opentyrian_str, SMALL_FONT_SHAPES);
							
							JE_outTextAdjust(tempY-1, tempX-1, opentyrian_str, 15, -10, SMALL_FONT_SHAPES, false);
							JE_outTextAdjust(tempY+1, tempX+1, opentyrian_str, 15, -10, SMALL_FONT_SHAPES, false);
							JE_outTextAdjust(tempY+1, tempX-1, opentyrian_str, 15, -10, SMALL_FONT_SHAPES, false);
							JE_outTextAdjust(tempY-1, tempX+1, opentyrian_str, 15, -10, SMALL_FONT_SHAPES, false);
							JE_outTextAdjust(tempY, tempX, opentyrian_str, 15, -3, SMALL_FONT_SHAPES, false);
						}
						else
						{
							tempY = JE_fontCenter(menuText[temp], SMALL_FONT_SHAPES);
							
							JE_outTextAdjust(tempY-1, tempX-1, menuText[temp], 15, -10, SMALL_FONT_SHAPES, false);
							JE_outTextAdjust(tempY+1, tempX+1, menuText[temp], 15, -10, SMALL_FONT_SHAPES, false);
							JE_outTextAdjust(tempY+1, tempX-1, menuText[temp], 15, -10, SMALL_FONT_SHAPES, false);
							JE_outTextAdjust(tempY-1, tempX+1, menuText[temp], 15, -10, SMALL_FONT_SHAPES, false);
							JE_outTextAdjust(tempY, tempX, menuText[temp], 15, -3, SMALL_FONT_SHAPES, false);
						}
					}
					JE_showVGA();
					
					fade_palette(colors, 20, 255 - 16 + 1, 255); // fade in menu items
					
					memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);
				}
			}
			
			memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
			
			for (temp = 0; temp < menunum; temp++)
			{
				if (temp == 4) /* OpenTyrian override */
					JE_outTextAdjust(JE_fontCenter(opentyrian_str, SMALL_FONT_SHAPES), 104+temp*13, opentyrian_str, 15, -3+((temp == menu) * 2), SMALL_FONT_SHAPES, false);
				else
					JE_outTextAdjust(JE_fontCenter(menuText[temp], SMALL_FONT_SHAPES), 104+temp*13, menuText[temp], 15, -3+((temp == menu) * 2), SMALL_FONT_SHAPES, false);
			}
			
			JE_showVGA();
			
			first = false;
			
			if (trentWin)
			{
				quit = true;
				goto trentWinsGame;
			}
			
			waitForDemo = 2000;
			JE_textMenuWait(&waitForDemo, false);
			
			if (waitForDemo == 1)
				play_demo = true;
			
			if (newkey)
			{
				switch (lastkey_sym)
				{
					case SDLK_UP:
						if (menu == 0)
							menu = menunum-1;
						else
							menu--;
						JE_playSampleNum(S_CURSOR);
						break;
					case SDLK_DOWN:
						if (menu == menunum-1)
							menu = 0;
						else
							menu++;
						JE_playSampleNum(S_CURSOR);
						break;
					default:
						break;
				}
			}
			
			for (unsigned int i = 0; i < SA_ENGAGE; i++)
			{
				if (toupper(lastkey_char) == specialName[i][arcade_code_i[i]])
					arcade_code_i[i]++;
				else
					arcade_code_i[i] = 0;
				
				if (arcade_code_i[i] > 0 && arcade_code_i[i] == strlen(specialName[i]))
				{
					if (i+1 == SA_DESTRUCT)
					{
						loadDestruct = true;
					}
					else if (i+1 == SA_ENGAGE)
					{
						/* SuperTyrian */
						
						JE_playSampleNum(V_DATA_CUBE);
						JE_whoa();
						
						initialDifficulty = keysactive[SDLK_SCROLLOCK] ? 6 : 8;
						
						JE_clr256();
						JE_outText(10, 10, "Cheat codes have been disabled.", 15, 4);
						if (initialDifficulty == 8)
							JE_outText(10, 20, "Difficulty level has been set to Lord of Game.", 15, 4);
						else
							JE_outText(10, 20, "Difficulty level has been set to Suicide.", 15, 4);
						JE_outText(10, 30, "It is imperitive that you discover the special codes.", 15, 4);
						if (initialDifficulty == 8)
							JE_outText(10, 40, "(Next time, for an easier challenge hold down SCROLL LOCK.)", 15, 4);
						JE_outText(10, 60, "Prepare to play...", 15, 4);
						
						char buf[10+1+15+1];
						snprintf(buf, sizeof(buf), "%s %s", miscTextB[4], pName[0]);
						JE_dString(JE_fontCenter(buf, FONT_SHAPES), 110, buf, FONT_SHAPES);
						
						play_song(16);
						JE_playSampleNum(V_DANGER);
						JE_showVGA();
						
						wait_input(true, true, true);
						
						JE_initEpisode(1);
						constantDie = false;
						superTyrian = true;
						onePlayerAction = true;
						gameLoaded = true;
						difficultyLevel = initialDifficulty;
						score = 0;
						
						pItems[P_SHIP] = 13;           // The Stalker 21.126
						pItems[P_FRONT] = 39;          // Atomic RailGun
					}
					else
					{
						pItems[P_SHIP] = SAShip[i];
						
						JE_fadeBlack(10);
						if (select_episode() && select_difficulty())
						{
							/* Start special mode! */
							JE_fadeBlack(10);
							JE_loadPic(1, false);
							JE_clr256();
							JE_dString(JE_fontCenter(superShips[0], FONT_SHAPES), 30, superShips[0], FONT_SHAPES);
							JE_dString(JE_fontCenter(superShips[i+1], SMALL_FONT_SHAPES), 100, superShips[i+1], SMALL_FONT_SHAPES);
							tempW = ships[pItems[P_SHIP]].shipgraphic;
							if (tempW != 1)
								JE_drawShape2x2(148, 70, tempW, shapes9);
							
							JE_showVGA();
							JE_fadeColor(50);
							
							wait_input(true, true, true);
							
							twoPlayerMode = false;
							onePlayerAction = true;
							superArcadeMode = i+1;
							gameLoaded = true;
							initialDifficulty = ++difficultyLevel;
							score = 0;
							
							pItems[P_FRONT] = SAWeapon[i][0];
							pItems[P_SPECIAL] = SASpecialWeapon[i];
							if (superArcadeMode == SA_NORTSHIPZ)
							{
								pItems[P_LEFT_SIDEKICK] = 24;   // Companion Ship Quicksilver
								pItems[P_RIGHT_SIDEKICK] = 24;  // Companion Ship Quicksilver
							}
						}
						else
						{
							redraw = true;
							fadeIn = true;
						}
					}
					newkey = false;
				}
			}
			lastkey_char = '\0';
			
			if (newkey)
			{
				switch (lastkey_sym)
				{
					case SDLK_ESCAPE:
						quit = true;
						break;
					case SDLK_RETURN:
						JE_playSampleNum(S_SELECT);
						switch (menu)
						{
							case 0: /* New game */
								JE_fadeBlack(10);
								if (select_gameplay())
								{
									if (select_episode() && select_difficulty())
									{
										gameLoaded = true;
									} else {
										redraw = true;
										fadeIn = true;
									}
									
									initialDifficulty = difficultyLevel;
									
									if (onePlayerAction)
									{
										score = 0;
										pItems[P_SHIP] = 8;
									}
									else if (twoPlayerMode)
									{
										score = 0;
										score2 = 0;
										pItems[P_SHIP] = 11;
										difficultyLevel++;
										inputDevice[0] = 1;
										inputDevice[1] = 2;
									}
									else if (richMode)
									{
										score = 1000000;
									}
									else
									{
										switch (episodeNum)
										{
											case 1:
												score = 10000;
												break;
											case 2:
												score = 15000;
												break;
											case 3:
												score = 20000;
												break;
											case 4:
												score = 30000;
												break;
										}
									}
								}
								fadeIn = true;
								break;
							case 1: /* Load game */
								JE_loadScreen();
								if (!gameLoaded)
									redraw = true;
								fadeIn = true;
								break;
							case 2: /* High scores */
								JE_highScoreScreen();
								fadeIn = true;
								break;
							case 3: /* Instructions */
								JE_helpSystem(1);
								redraw = true;
								fadeIn = true;
								break;
							case 4: /* Ordering info, now OpenTyrian menu */
								opentyrian_menu();
								redraw = true;
								fadeIn = true;
								break;
							#ifdef TYRIAN2000
							case 5: /* Quit */
								quit = true;
								break;
							#else
							case 5: /* Demo */
								play_demo = true;
								break;
							case 6: /* Quit */
								quit = true;
								break;
							#endif
						}
						redraw = true;
						break;
					default:
						break;
				}
			}
		}
		while (!(quit || gameLoaded || jumpSection || play_demo || loadDestruct));
		
	trentWinsGame:
		JE_fadeBlack(15);
		if (quit)
			JE_tyrianHalt(0);
		
	}
}

void intro_logos( void )
{
	SDL_FillRect(VGAScreen, NULL, 0);
	
	SDL_Color white = { 255, 255, 255 };
	fade_solid(&white, 50, 0, 255);
	
	JE_loadPic(10, false);
	JE_showVGA();
	
	fade_palette(colors, 50, 0, 255);
	
	setjasondelay(200);
	wait_delayorinput(true, true, true);
	
	fade_black(10);
	
	JE_loadPic(12, false);
	JE_showVGA();
	
	fade_palette(colors, 10, 0, 255);
	
	setjasondelay(200);
	wait_delayorinput(true, true, true);
	
	fade_black(10);
}

void JE_readTextSync( void )
{
	return;  // this function seems to be unnecessary
	
	JE_clr256();
	JE_showVGA();
	JE_loadPic(1, true);

	JE_barShade(3, 3, 316, 196);
	JE_barShade(1, 1, 318, 198);
	JE_dString(10, 160, "Waiting for other player.", SMALL_FONT_SHAPES);
	JE_showVGA();

	/* TODO: NETWORK */

	do
	{
		setjasondelay(2);

		/* TODO: NETWORK */

		wait_delay();

	} while (0 /* TODO: NETWORK */);
}


void JE_displayText( void )
{
	/* Display Warning Text */
	tempY = 55;
	if (warningRed)
	{
		tempY = 2;
	}
	for (temp = 0; temp < levelWarningLines; temp++)
	{
		if (!ESCPressed)
		{
			JE_outCharGlow(10, tempY, levelWarningText[temp]);

			if (haltGame)
			{
				JE_tyrianHalt(5);
			}

			tempY += 10;
		}
	}
	if (frameCountMax != 0)
	{
		frameCountMax = 6;
		temp = 1;
	} else {
		temp = 0;
	}
	textGlowFont = TINY_FONT;
	tempW = 184;
	if (warningRed)
	{
		tempW = 7 * 16 + 6;
	}

	JE_outCharGlow(JE_fontCenter(miscText[4], TINY_FONT), tempW, miscText[4]);

	do
	{
		if (levelWarningDisplay)
		{
			JE_updateWarning();
		}

		setjasondelay(1);

		NETWORK_KEEP_ALIVE();
		
		wait_delay();
		
	} while (!(JE_anyButton() || (frameCountMax == 0 && temp == 1) || ESCPressed));
	levelWarningDisplay = false;
}

void JE_newEnemy( int enemyOffset )
{
	b = 0; // stupid global
	
	for (int i = enemyOffset; i < enemyOffset + 25; i++)
	{
		if (enemyAvail[i] == 1)
		{
			b = i+1;
			JE_makeEnemy(&enemy[b-1]);
			enemyAvail[b-1] = a;
			break;
		}
	}
}

void JE_makeEnemy( struct JE_SingleEnemyType *enemy ) // tempW, uniqueEnemy, tempI2, b, a
{
	JE_byte temp;
	int t = 0;

	if (superArcadeMode != SA_NONE && tempW == 534)
		tempW = 533;

	enemyShapeTables[5-1] = 21;   /*Coins&Gems*/
	enemyShapeTables[6-1] = 26;   /*Two-Player Stuff*/

	if (uniqueEnemy)
	{
		temp = tempI2;
		uniqueEnemy = false;
	}
	else
	{
		temp = enemyDat[tempW].shapebank;
	}

	for (a = 0; a < 6; a++)
	{
		if (temp == enemyShapeTables[a])
		{
			switch (a)
			{
				case 0:
					enemy->shapeseg = eShapes1;
					break;
				case 1:
					enemy->shapeseg = eShapes2;
					break;
				case 2:
					enemy->shapeseg = eShapes3;
					break;
				case 3:
					enemy->shapeseg = eShapes4;
					break;
				case 4:
					enemy->shapeseg = eShapes5;
					break;
				case 5:
					enemy->shapeseg = eShapes6;
					break;
			}
		}
	}

	enemy->enemydatofs = &enemyDat[tempW];

	enemy->mapoffset = 0;

	for (a = 0; a < 3; a++)
	{
		enemy->eshotmultipos[a] = 0;
	}

	temp4 = enemyDat[tempW].explosiontype;
	enemy->enemyground = ((temp4 & 0x01) == 0);
	enemy->explonum = temp4 / 2;

	enemy->launchfreq = enemyDat[tempW].elaunchfreq;
	enemy->launchwait = enemyDat[tempW].elaunchfreq;
	enemy->launchtype = enemyDat[tempW].elaunchtype % 1000;
	enemy->launchspecial = enemyDat[tempW].elaunchtype / 1000;

	enemy->xaccel = enemyDat[tempW].xaccel;
	enemy->yaccel = enemyDat[tempW].yaccel;

	enemy->xminbounce = -10000;
	enemy->xmaxbounce = 10000;
	enemy->yminbounce = -10000;
	enemy->ymaxbounce = 10000;
	/*Far enough away to be impossible to reach*/

	for (a = 0; a < 3; a++)
	{
		enemy->tur[a] = enemyDat[tempW].tur[a];
	}

	enemy->ani = enemyDat[tempW].ani;
	enemy->animin = 1;

	switch (enemyDat[tempW].animate)
	{
		case 0:
			enemy->enemycycle = 1;
			enemy->aniactive = 0;
			enemy->animax = 0;
			enemy->aniwhenfire = 0;
			break;
		case 1:
			enemy->enemycycle = 0;
			enemy->aniactive = 1;
			enemy->animax = 0;
			enemy->aniwhenfire = 0;
			break;
		case 2:
			enemy->enemycycle = 1;
			enemy->aniactive = 2;
			enemy->animax = enemy->ani;
			enemy->aniwhenfire = 2;
			break;
	}
	
	if (enemyDat[tempW].startxc != 0)
		enemy->ex = enemyDat[tempW].startx + (mt_rand() % (enemyDat[tempW].startxc * 2)) - enemyDat[tempW].startxc + 1;
	else
		enemy->ex = enemyDat[tempW].startx + 1;
	
	if (enemyDat[tempW].startyc != 0)
		enemy->ey = enemyDat[tempW].starty + (mt_rand() % (enemyDat[tempW].startyc * 2)) - enemyDat[tempW].startyc + 1;
	else
		enemy->ey = enemyDat[tempW].starty + 1;
	
	enemy->exc = enemyDat[tempW].xmove;
	enemy->eyc = enemyDat[tempW].ymove;
	enemy->excc = enemyDat[tempW].xcaccel;
	enemy->eycc = enemyDat[tempW].ycaccel;
	enemy->exccw = abs(enemy->excc);
	enemy->exccwmax = enemy->exccw;
	enemy->eyccw = abs(enemy->eycc);
	enemy->eyccwmax = enemy->eyccw;
	enemy->exccadd = (enemy->excc > 0) ? 1 : -1;
	enemy->eyccadd = (enemy->eycc > 0) ? 1 : -1;
	enemy->special = false;
	enemy->iced = 0;

	if (enemyDat[tempW].xrev == 0)
		enemy->exrev = 100;
	else if (enemyDat[tempW].xrev == -99)
		enemy->exrev = 0;
	else
		enemy->exrev = enemyDat[tempW].xrev;
	
	if (enemyDat[tempW].yrev == 0)
		enemy->eyrev = 100;
	else if (enemyDat[tempW].yrev == -99)
		enemy->eyrev = 0;
	else
		enemy->eyrev = enemyDat[tempW].yrev;
	
	enemy->exca = (enemy->xaccel > 0) ? 1 : -1;
	enemy->eyca = (enemy->yaccel > 0) ? 1 : -1;
	
	enemy->enemytype = tempW;
	
	for (a = 0; a < 3; a++)
	{
		if (enemy->tur[a] == 252)
			enemy->eshotwait[a] = 1;
		else if (enemy->tur[a] > 0)
			enemy->eshotwait[a] = 20;
		else
			enemy->eshotwait[a] = 255;
	}
	for (a = 0; a < 20; a++)
		enemy->egr[a] = enemyDat[tempW].egraphic[a];
	enemy->size = enemyDat[tempW].esize;
	enemy->linknum = 0;
	enemy->edamaged = enemyDat[tempW].dani < 0;
	enemy->enemydie = enemyDat[tempW].eenemydie;

	enemy->freq[1-1] = enemyDat[tempW].freq[1-1];
	enemy->freq[2-1] = enemyDat[tempW].freq[2-1];
	enemy->freq[3-1] = enemyDat[tempW].freq[3-1];

	enemy->edani   = enemyDat[tempW].dani;
	enemy->edgr    = enemyDat[tempW].dgr;
	enemy->edlevel = enemyDat[tempW].dlevel;

	enemy->fixedmovey = 0;

	enemy->filter = 0x00;

	if (enemyDat[tempW].value > 1 && enemyDat[tempW].value < 10000)
	{
		switch (difficultyLevel)
		{
			case -1:
			case 0:
				t = enemyDat[tempW].value * 0.75f;
				break;
			case 1:
			case 2:
				t = enemyDat[tempW].value;
				break;
			case 3:
				t = enemyDat[tempW].value * 1.125f;
				break;
			case 4:
				t = enemyDat[tempW].value * 1.5f;
				break;
			case 5:
				t = enemyDat[tempW].value * 2;
				break;
			case 6:
				t = enemyDat[tempW].value * 2.5f;
				break;
			case 7:
			case 8:
				t = enemyDat[tempW].value * 4;
				break;
			case 9:
			case 10:
				t = enemyDat[tempW].value * 8;
				break;
		}
		if (t > 10000)
			t = 10000;
		enemy->evalue = t;
	}
	else
	{
		enemy->evalue = enemyDat[tempW].value;
	}
	
	t = 1;
	if (enemyDat[tempW].armor > 0)
	{
		if (enemyDat[tempW].armor != 255)
		{
			switch (difficultyLevel)
			{
				case -1:
				case 0:
					t = enemyDat[tempW].armor * 0.5f + 1;
					break;
				case 1:
					t = enemyDat[tempW].armor * 0.75f + 1;
					break;
				case 2:
					t = enemyDat[tempW].armor;
					break;
				case 3:
					t = enemyDat[tempW].armor * 1.2f;
					break;
				case 4:
					t = enemyDat[tempW].armor * 1.5f;
					break;
				case 5:
					t = enemyDat[tempW].armor * 1.8f;
					break;
				case 6:
					t = enemyDat[tempW].armor * 2;
					break;
				case 7:
					t = enemyDat[tempW].armor * 3;
					break;
				case 8:
					t = enemyDat[tempW].armor * 4;
					break;
				case 9:
				case 10:
					t = enemyDat[tempW].armor * 8;
					break;
			}
			
			if (t > 254)
			{
				t = 254;
			}
		}
		else
		{
			t = 255;
		}
		
		enemy->armorleft = t;
		
		a = 0;
		enemy->scoreitem = false;
	}
	else
	{
		a = 2;
		enemy->armorleft = 255;
		if (enemy->evalue != 0)
			enemy->scoreitem = true;
	}
	/*The returning A value indicates what to set ENEMYAVAIL to */
	
	if (!enemy->scoreitem)
	{
		totalEnemy++;  /*Destruction ratio*/
	}
}

void JE_createNewEventEnemy( JE_byte enemyTypeOfs, JE_word enemyOffset )
{
	int i;

	b = 0;

	for(i = enemyOffset; i < enemyOffset + 25; i++)
	{
		if (enemyAvail[i] == 1)
		{
			b = i + 1;
			break;
		}
	}

	if (b == 0)
	{
		return;
	}

	tempW = eventRec[eventLoc-1].eventdat + enemyTypeOfs;

	JE_makeEnemy(&enemy[b-1]);

	enemyAvail[b-1] = a;

	if (eventRec[eventLoc-1].eventdat2 != -99)
	{
		switch (enemyOffset)
		{
			case 0:
				enemy[b-1].ex = eventRec[eventLoc-1].eventdat2 - (mapX - 1) * 24;
				enemy[b-1].ey -= backMove2;
				break;
			case 25:
			case 75:
				enemy[b-1].ex = eventRec[eventLoc-1].eventdat2 - (mapX - 1) * 24 - 12;
				enemy[b-1].ey -= backMove;
				break;
			case 50:
				if (background3x1)
				{
					enemy[b-1].ex = eventRec[eventLoc-1].eventdat2 - (mapX - 1) * 24 - 12;
				} else {
					enemy[b-1].ex = eventRec[eventLoc-1].eventdat2 - mapX3 * 24 - 24 * 2 + 6;
				}
				enemy[b-1].ey -= backMove3;

				if (background3x1b)
				{
					enemy[b-1].ex -= 6;
				}
				break;
		}
		enemy[b-1].ey = -28;
		if (background3x1b && enemyOffset == 50)
		{
			enemy[b-1].ey += 4;
		}
	}

	if (smallEnemyAdjust && enemy[b-1].size == 0)
	{
		enemy[b-1].ex -= 10;
		enemy[b-1].ey -= 7;
	}

	enemy[b-1].ey += eventRec[eventLoc-1].eventdat5;
	enemy[b-1].eyc += eventRec[eventLoc-1].eventdat3;
	enemy[b-1].linknum = eventRec[eventLoc-1].eventdat4;
	enemy[b-1].fixedmovey = eventRec[eventLoc-1].eventdat6;
}

void JE_eventJump( JE_word jump )
{
	JE_word tempW;

	if (jump == 65535)
	{
		curLoc = returnLoc;
	}
	else
	{
		returnLoc = curLoc + 1;
		curLoc = jump;
	}
	tempW = 0;
	do
	{
		tempW++;
	}
	while (!(eventRec[tempW-1].eventtime >= curLoc));
	eventLoc = tempW - 1;
}

JE_boolean JE_searchFor/*enemy*/( JE_byte PLType )
{
	JE_boolean tempb = false;
	JE_byte temp;

	for (temp = 0; temp < 100; temp++)
	{
		if (enemyAvail[temp] == 0 && enemy[temp].linknum == PLType)
		{
			temp5 = temp + 1;
			if (galagaMode)
			{
				enemy[temp].evalue += enemy[temp].evalue;
			}
			tempb = true;
		}
	}
	return tempb;
}

void JE_eventSystem( void )
{
	JE_boolean tempb;
	
	switch (eventRec[eventLoc-1].eventtype)
	{
		case 1:
			starY = eventRec[eventLoc-1].eventdat * VGAScreen->pitch;
			break;
		case 2:
			map1YDelay = 1;
			map1YDelayMax = 1;
			map2YDelay = 1;
			map2YDelayMax = 1;

			backMove = eventRec[eventLoc-1].eventdat;
			backMove2 = eventRec[eventLoc-1].eventdat2;
			if (backMove2 > 0)
			{
				explodeMove = backMove2;
			} else {
				explodeMove = backMove;
			}
			backMove3 = eventRec[eventLoc-1].eventdat3;

			if (backMove > 0)
			{
				stopBackgroundNum = 0;
			}
			break;
		case 3:
			backMove = 1;
			map1YDelay = 3;
			map1YDelayMax = 3;
			backMove2 = 1;
			map2YDelay = 2;
			map2YDelayMax = 2;
			backMove3 = 1;
			break;
		case 4:
			stopBackgrounds = true;
			switch (eventRec[eventLoc-1].eventdat)
			{
				case 0:
				case 1:
					stopBackgroundNum = 1;
					break;
				case 2:
					stopBackgroundNum = 2;
					break;
				case 3:
					stopBackgroundNum = 3;
					break;
			}
			break;
		case 5:
			if (enemyShapeTables[1-1] != eventRec[eventLoc-1].eventdat)
			{
				if (eventRec[eventLoc-1].eventdat > 0)
				{
					JE_loadCompShapes(&eShapes1, &eShapes1Size, shapeFile[eventRec[eventLoc-1].eventdat -1]);      /* Enemy Bank 1 */
					enemyShapeTables[1-1] = eventRec[eventLoc-1].eventdat;
				} else if (eShapes1 != NULL) {
					free(eShapes1);
					eShapes1 = NULL;
					enemyShapeTables[1-1] = 0;
				}
			}
			if (enemyShapeTables[2-1] != eventRec[eventLoc-1].eventdat2)
			{
				if (eventRec[eventLoc-1].eventdat2 > 0)
				{
					JE_loadCompShapes(&eShapes2, &eShapes2Size, shapeFile[eventRec[eventLoc-1].eventdat2-1]);      /* Enemy Bank 2 */
					enemyShapeTables[2-1] = eventRec[eventLoc-1].eventdat2;
				} else if (eShapes2 != NULL) {
					free(eShapes2);
					eShapes2 = NULL;
					enemyShapeTables[2-1] = 0;
				}
			}
			if (enemyShapeTables[3-1] != eventRec[eventLoc-1].eventdat3)
			{
				if (eventRec[eventLoc-1].eventdat3 > 0)
				{
					JE_loadCompShapes(&eShapes3, &eShapes3Size, shapeFile[eventRec[eventLoc-1].eventdat3-1]);      /* Enemy Bank 3 */
					enemyShapeTables[3-1] = eventRec[eventLoc-1].eventdat3;
				} else if (eShapes3 != NULL) {
					free(eShapes3);
					eShapes3 = NULL;
					enemyShapeTables[3-1] = 0;
				}
			}
			if (enemyShapeTables[4-1] != eventRec[eventLoc-1].eventdat4)
			{
				if (eventRec[eventLoc-1].eventdat4 > 0)
				{
					JE_loadCompShapes(&eShapes4, &eShapes4Size, shapeFile[eventRec[eventLoc-1].eventdat4-1]);      /* Enemy Bank 4 */
					enemyShapeTables[4-1] = eventRec[eventLoc-1].eventdat4;
					enemyShapeTables[5-1] = 21;
				} else if (eShapes4 != NULL) {
					free(eShapes4);
					eShapes4 = NULL;
					enemyShapeTables[4-1] = 0;
				}
			}
			break;
		case 6: /* Ground Enemy */
			JE_createNewEventEnemy(0, 25);
			break;
		case 7: /* Top Enemy */
			JE_createNewEventEnemy(0, 50);
			break;
		case 8:
			starActive = false;
			break;
		case 9:
			starActive = true;
			break;
		case 10: /* Ground Enemy 2 */
			JE_createNewEventEnemy(0, 75);
			break;
		case 11:
			if (allPlayersGone || eventRec[eventLoc-1].eventdat == 1)
				reallyEndLevel = true;
			else
				if (!endLevel)
				{
					readyToEndLevel = false;
					endLevel = true;
					levelEnd = 40;
				}
			break;
		case 12: /* Custom 4x4 Ground Enemy */
			switch (eventRec[eventLoc-1].eventdat6)
			{
				case 0:
				case 1:
					tempW4 = 25;
					break;
				case 2:
					tempW4 = 0;
					break;
				case 3:
					tempW4 = 50;
					break;
				case 4:
					tempW4 = 75;
					break;
			}
			eventRec[eventLoc-1].eventdat6 = 0;   /* We use EVENTDAT6 for the background */
			JE_createNewEventEnemy(0, tempW4);
			JE_createNewEventEnemy(1, tempW4);
			enemy[b-1].ex += 24;
			JE_createNewEventEnemy(2, tempW4);
			enemy[b-1].ey -= 28;
			JE_createNewEventEnemy(3, tempW4);
			enemy[b-1].ex += 24;
			enemy[b-1].ey -= 28;
			break;
		case 13:
			enemiesActive = false;
			break;
		case 14:
			enemiesActive = true;
			break;
		case 15: /* Sky Enemy */
			JE_createNewEventEnemy(0, 0);
			break;
		case 16:
			if (eventRec[eventLoc-1].eventdat > 9)
			{
				printf("error: event 16: bad event data\n");
			} else {
				JE_drawTextWindow(outputs[eventRec[eventLoc-1].eventdat-1]);
				soundQueue[3] = windowTextSamples[eventRec[eventLoc-1].eventdat-1];
			}
			break;
		case 17: /* Ground Bottom */
			JE_createNewEventEnemy(0, 25);
			if (b > 0)
			{
				enemy[b-1].ey = 190 + eventRec[eventLoc-1].eventdat5;
			}
			break;

		case 18: /* Sky Enemy on Bottom */
			JE_createNewEventEnemy(0, 0);
			if (b > 0)
			{
				enemy[b-1].ey = 190 + eventRec[eventLoc-1].eventdat5;
			}
			break;

		case 19: /* Enemy Global Move */
			if (eventRec[eventLoc-1].eventdat3 > 79 && eventRec[eventLoc-1].eventdat3 < 90)
			{
				temp2 = 1;
				temp3 = 100;
				temp4 = 0;
				eventRec[eventLoc-1].eventdat4 = newPL[eventRec[eventLoc-1].eventdat3 - 80];
			} else {
				switch (eventRec[eventLoc-1].eventdat3)
				{
					case 0:
						temp2 = 1;
						temp3 = 100;
						temp4 = 0;
						break;
					case 2:
						temp2 = 1;
						temp3 = 25;
						temp4 = 1;
						break;
					case 1:
						temp2 = 26;
						temp3 = 50;
						temp4 = 1;
						break;
					case 3:
						temp2 = 51;
						temp3 = 75;
						temp4 = 1;
						break;
					case 99:
						temp2 = 1;
						temp3 = 100;
						temp4 = 1;
						break;
				}
			}

			for (temp = temp2-1; temp < temp3; temp++)
			{
				if (temp4 == 1 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
				{
					if (eventRec[eventLoc-1].eventdat != -99)
					{
						enemy[temp].exc = eventRec[eventLoc-1].eventdat;
					}
					if (eventRec[eventLoc-1].eventdat2 != -99)
					{
						enemy[temp].eyc = eventRec[eventLoc-1].eventdat2;
					}
					if (eventRec[eventLoc-1].eventdat6 != 0)
					{
						enemy[temp].fixedmovey = eventRec[eventLoc-1].eventdat6;
					}
					if (eventRec[eventLoc-1].eventdat6 == -99)
					{
						enemy[temp].fixedmovey = 0;
					}
					if (eventRec[eventLoc-1].eventdat5 > 0)
					{
						enemy[temp].enemycycle = eventRec[eventLoc-1].eventdat5;
					}
				}
			}
			break;
		case 20: /* Enemy Global Accel */
			if (eventRec[eventLoc-1].eventdat3 > 79 && eventRec[eventLoc-1].eventdat3 < 90)
			{
				eventRec[eventLoc-1].eventdat4 = newPL[eventRec[eventLoc-1].eventdat3 - 80];
			}
			for (temp = 0; temp < 100; temp++)
			{
				if (enemyAvail[temp] != 1
				    && (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4 || eventRec[eventLoc-1].eventdat4 == 0))
				{
					if (eventRec[eventLoc-1].eventdat != -99)
					{
						enemy[temp].excc = eventRec[eventLoc-1].eventdat;
						enemy[temp].exccw = abs(eventRec[eventLoc-1].eventdat);
						enemy[temp].exccwmax = abs(eventRec[eventLoc-1].eventdat);
						if (eventRec[eventLoc-1].eventdat > 0)
						{
							enemy[temp].exccadd = 1;
						} else {
							enemy[temp].exccadd = -1;
						}
					}

					if (eventRec[eventLoc-1].eventdat2 != -99)
					{
						enemy[temp].eycc = eventRec[eventLoc-1].eventdat2;
						enemy[temp].eyccw = abs(eventRec[eventLoc-1].eventdat2);
						enemy[temp].eyccwmax = abs(eventRec[eventLoc-1].eventdat2);
						if (eventRec[eventLoc-1].eventdat2 > 0)
						{
							enemy[temp].eyccadd = 1;
						} else {
							enemy[temp].eyccadd = -1;
						}
					}

					if (eventRec[eventLoc-1].eventdat5 > 0)
					{
						enemy[temp].enemycycle = eventRec[eventLoc-1].eventdat5;
					}
					if (eventRec[eventLoc-1].eventdat6 > 0)
					{
						enemy[temp].ani = eventRec[eventLoc-1].eventdat6;
						enemy[temp].animin = eventRec[eventLoc-1].eventdat5;
						enemy[temp].animax = 0;
						enemy[temp].aniactive = 1;
					}
				}
			}
			break;
		case 21:
			background3over = 1;
			break;
		case 22:
			background3over = 0;
			break;
		case 23: /* Sky Enemy on Bottom */
			JE_createNewEventEnemy(0, 50);
			if (b > 0)
			{
				enemy[b-1].ey = 180 + eventRec[eventLoc-1].eventdat5;
			}
			break;

		case 24: /* Enemy Global Animate */
			for (temp = 0; temp < 100; temp++)
			{
				if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
				{
					enemy[temp].aniactive = 1;
					enemy[temp].aniwhenfire = 0;
					if (eventRec[eventLoc-1].eventdat2 > 0)
					{
						enemy[temp].enemycycle = eventRec[eventLoc-1].eventdat2;
						enemy[temp].animin = enemy[temp].enemycycle;
					} else {
						enemy[temp].enemycycle = 0;
					}
					if (eventRec[eventLoc-1].eventdat > 0)
					{
						enemy[temp].ani = eventRec[eventLoc-1].eventdat;
					}
					if (eventRec[eventLoc-1].eventdat3 == 1)
					{
						enemy[temp].animax = enemy[temp].ani;
					} else {
						if (eventRec[eventLoc-1].eventdat3 == 2)
						{
							enemy[temp].aniactive = 2;
							enemy[temp].animax = enemy[temp].ani;
							enemy[temp].aniwhenfire = 2;
						}
					}
				}
			}
			break;
		case 25: /* Enemy Global Damage change */
			for (temp = 0; temp < 100; temp++)
			{
				if (eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
				{
					enemy[temp].armorleft = eventRec[eventLoc-1].eventdat;
					if (galagaMode)
					{
						enemy[temp].armorleft = round(eventRec[eventLoc-1].eventdat * (difficultyLevel / 2));
					}
				}
			}
			break;
		case 26:
			smallEnemyAdjust = eventRec[eventLoc-1].eventdat;
			break;
		case 27: /* Enemy Global AccelRev */
			if (eventRec[eventLoc-1].eventdat3 > 79 && eventRec[eventLoc-1].eventdat3 < 90)
			{
				eventRec[eventLoc-1].eventdat4 = newPL[eventRec[eventLoc-1].eventdat3 - 80];
			}
			for (temp = 0; temp < 100; temp++)
			{
				if (eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
				{
					if (eventRec[eventLoc-1].eventdat != -99)
					{
						enemy[temp].exrev = eventRec[eventLoc-1].eventdat;
					}
					if (eventRec[eventLoc-1].eventdat2 != -99)
					{
						enemy[temp].eyrev = eventRec[eventLoc-1].eventdat2;
					}
					if (eventRec[eventLoc-1].eventdat3 != 0 && eventRec[eventLoc-1].eventdat3 < 17)
					{
						enemy[temp].filter = eventRec[eventLoc-1].eventdat3;
					}
				}
			}
			break;
		case 28:
			topEnemyOver = false;
			break;
		case 29:
			topEnemyOver = true;
			break;
		case 30:
			map1YDelay = 1;
			map1YDelayMax = 1;
			map2YDelay = 1;
			map2YDelayMax = 1;

			backMove = eventRec[eventLoc-1].eventdat;
			backMove2 = eventRec[eventLoc-1].eventdat2;
			explodeMove = backMove2;
			backMove3 = eventRec[eventLoc-1].eventdat3;
			break;
		case 31: /* Enemy Fire Override */
			for (temp = 0; temp < 100; temp++)
			{
				if (eventRec[eventLoc-1].eventdat4 == 99 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
				{
					enemy[temp].freq[1-1] = eventRec[eventLoc-1].eventdat ;
					enemy[temp].freq[2-1] = eventRec[eventLoc-1].eventdat2;
					enemy[temp].freq[3-1] = eventRec[eventLoc-1].eventdat3;
					for (temp2 = 0; temp2 < 3; temp2++)
					{
						enemy[temp].eshotwait[temp2] = 1;
					}
					if (enemy[temp].launchtype > 0)
					{
						enemy[temp].launchfreq = eventRec[eventLoc-1].eventdat5;
						enemy[temp].launchwait = 1;
					}
				}
			}
			break;
		case 32:
			JE_createNewEventEnemy(0, 50);
			if (b > 0)
			{
				enemy[b-1].ey = 190;
			}
			break;
		case 33: /* Enemy From other Enemies */
			if (!((eventRec[eventLoc-1].eventdat == 512 || eventRec[eventLoc-1].eventdat == 513) && (twoPlayerMode || onePlayerAction || superTyrian)))
			{
				if (superArcadeMode != SA_NONE)
				{
					if (eventRec[eventLoc-1].eventdat == 534)
						eventRec[eventLoc-1].eventdat = 827;
				}
				else
				{
					if (eventRec[eventLoc-1].eventdat == 533
					    && (portPower[1-1] == 11 || (mt_rand() % 15) < portPower[1-1])
					    && !superTyrian)
					{
						eventRec[eventLoc-1].eventdat = 829 + (mt_rand() % 6);
					}
				}
				if (eventRec[eventLoc-1].eventdat == 534 && superTyrian)
					eventRec[eventLoc-1].eventdat = 828 + superTyrianSpecials[mt_rand() % 4];

				for (temp = 0; temp < 100; temp++)
				{
					if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
					{
						enemy[temp].enemydie = eventRec[eventLoc-1].eventdat;
					}
				}
			}
			break;
		case 34: /* Start Music Fade */
			if (firstGameOver)
			{
				musicFade = true;
				tempVolume = tyrMusicVolume;
			}
			break;
		case 35: /* Play new song */
			if (firstGameOver)
			{
				play_song(eventRec[eventLoc-1].eventdat - 1);
				set_volume(tyrMusicVolume, fxVolume);
			}
			musicFade = false;
			break;
		case 36:
			readyToEndLevel = true;
			break;
		case 37:
			levelEnemyFrequency = eventRec[eventLoc-1].eventdat;
			break;
		case 38:
			curLoc = eventRec[eventLoc-1].eventdat;
			tempW2 = 1;
			for (tempW = 0; tempW < maxEvent; tempW++)
			{
				if (eventRec[tempW].eventtime <= curLoc)
				{
					tempW2 = tempW+1 - 1;
				}
			}
			eventLoc = tempW2;
			break;
		case 39: /* Enemy Global Linknum Change */
			for (temp = 0; temp < 100; temp++)
			{
				if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat)
				{
					enemy[temp].linknum = eventRec[eventLoc-1].eventdat2;
				}
			}
			break;
		case 40: /* Enemy Continual Damage */
			enemyContinualDamage = true;
			break;
		case 41:
			if (eventRec[eventLoc-1].eventdat == 0)
			{
				memset(enemyAvail, 1, sizeof(enemyAvail));
			} else {
				for (x = 0; x <= 24; x++)
				{
					enemyAvail[x] = 1;
				}
			}
			break;
		case 42:
			background3over = 2;
			break;
		case 43:
			background2over = eventRec[eventLoc-1].eventdat;
			break;
		case 44:
			filterActive       = (eventRec[eventLoc-1].eventdat > 0);
			filterFade         = (eventRec[eventLoc-1].eventdat == 2);
			levelFilter        = eventRec[eventLoc-1].eventdat2;
			levelBrightness    = eventRec[eventLoc-1].eventdat3;
			levelFilterNew     = eventRec[eventLoc-1].eventdat4;
			levelBrightnessChg = eventRec[eventLoc-1].eventdat5;
			filterFadeStart    = (eventRec[eventLoc-1].eventdat6 == 0);
			break;
		case 45: /* Two Player Enemy from other Enemies */
			if (!superTyrian)
			{
				if (eventRec[eventLoc-1].eventdat == 533
				    && (portPower[1-1] == 11 || (mt_rand() % 15) < portPower[1-1]))
				{
					eventRec[eventLoc-1].eventdat = 829 + (mt_rand() % 6);
				}
				if (twoPlayerMode || onePlayerAction)
				{
					for (temp = 0; temp < 100; temp++)
					{
						if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
						{
							enemy[temp].enemydie = eventRec[eventLoc-1].eventdat;
						}
					}
				}
			}
			break;
		case 46:
			if (eventRec[eventLoc-1].eventdat3 != 0)
			{
				damageRate = eventRec[eventLoc-1].eventdat3;
			}
			if (eventRec[eventLoc-1].eventdat2 == 0 || twoPlayerMode || onePlayerAction)
			{
				difficultyLevel += eventRec[eventLoc-1].eventdat;
				if (difficultyLevel < 1)
				{
					difficultyLevel = 1;
				}
				if (difficultyLevel > 10)
				{
					difficultyLevel = 10;
				}
			}
			break;
		case 47: /* Enemy Global AccelRev */
			for (temp = 0; temp < 100; temp++)
			{
				if (eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
				{
					enemy[temp].armorleft = eventRec[eventLoc-1].eventdat;
				}
			}
			break;
		case 48: /* Background 2 Cannot be Transparent */
			background2notTransparent = true;
			break;
		case 49:
		case 50:
		case 51:
		case 52:
			tempDat2 = eventRec[eventLoc-1].eventdat;
			eventRec[eventLoc-1].eventdat = 0;
			tempDat = eventRec[eventLoc-1].eventdat3;
			eventRec[eventLoc-1].eventdat3 = 0;
			tempDat3 = eventRec[eventLoc-1].eventdat6;
			eventRec[eventLoc-1].eventdat6 = 0;
			tempI2 = tempDat;
			enemyDat[0].armor = tempDat3;
			enemyDat[0].egraphic[1-1] = tempDat2;
			switch (eventRec[eventLoc-1].eventtype - 48)
			{
				case 1:
					temp = 25;
					break;
				case 2:
					temp = 0;
					break;
				case 3:
					temp = 50;
					break;
				case 4:
					temp = 75;
					break;
			}
			uniqueEnemy = true;
			JE_createNewEventEnemy(0, temp);
			uniqueEnemy = false;
			eventRec[eventLoc-1].eventdat = tempDat2;
			eventRec[eventLoc-1].eventdat3 = tempDat;
			eventRec[eventLoc-1].eventdat6 = tempDat3;
			break;

		case 53:
			forceEvents = (eventRec[eventLoc-1].eventdat != 99);
			break;
		case 54:
			JE_eventJump(eventRec[eventLoc-1].eventdat);
			break;
		case 55: /* Enemy Global AccelRev */
			if (eventRec[eventLoc-1].eventdat3 > 79 && eventRec[eventLoc-1].eventdat3 < 90)
			{
				eventRec[eventLoc-1].eventdat4 = newPL[eventRec[eventLoc-1].eventdat3 - 80];
			}
			for (temp = 0; temp < 100; temp++)
			{
				if (eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
				{
					if (eventRec[eventLoc-1].eventdat != -99)
					{
						enemy[temp].xaccel = eventRec[eventLoc-1].eventdat;
					}
					if (eventRec[eventLoc-1].eventdat2 != -99)
					{
						enemy[temp].yaccel = eventRec[eventLoc-1].eventdat2;
					}
				}
			}
			break;
		case 56: /* Ground2 Bottom */
			JE_createNewEventEnemy(0, 75);
			if (b > 0)
			{
				enemy[b-1].ey = 190;
			}
			break;
		case 57:
			superEnemy254Jump = eventRec[eventLoc-1].eventdat;
			break;

		case 60: /*Assign Special Enemy*/
			for (temp = 0; temp < 100; temp++)
			{
				if (enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
				{
					enemy[temp].special = true;
					enemy[temp].flagnum = eventRec[eventLoc-1].eventdat;
					enemy[temp].setto  = (eventRec[eventLoc-1].eventdat2 == 1);
				}
			}
			break;

		case 61: /*If Flag then...*/
			if (globalFlags[eventRec[eventLoc-1].eventdat] == eventRec[eventLoc-1].eventdat2)
			{
				eventLoc += eventRec[eventLoc-1].eventdat3;
			}
			break;
		case 62: /*Play sound effect*/
			soundQueue[3] = eventRec[eventLoc-1].eventdat;
			break;

		case 63: /*Skip X events if not in 2-player mode*/
			if (!twoPlayerMode && !onePlayerAction)
			{
				eventLoc += eventRec[eventLoc-1].eventdat;
			}
			break;

		case 64:
			if (!(eventRec[eventLoc-1].eventdat == 6 && twoPlayerMode && difficultyLevel > 2))
			{
				smoothies[eventRec[eventLoc-1].eventdat-1] = eventRec[eventLoc-1].eventdat2;
				temp = eventRec[eventLoc-1].eventdat;
				if (temp == 5)
					temp = 3;
				SDAT[temp-1] = eventRec[eventLoc-1].eventdat3;
			}
			break;

		case 65:
			background3x1 = (eventRec[eventLoc-1].eventdat == 0);
			break;
		case 66: /*If not on this difficulty level or higher then...*/
			if (initialDifficulty <= eventRec[eventLoc-1].eventdat)
				eventLoc += eventRec[eventLoc-1].eventdat2;
			break;
		case 67:
			levelTimer = (eventRec[eventLoc-1].eventdat == 1);
			levelTimerCountdown = eventRec[eventLoc-1].eventdat3 * 100;
			levelTimerJumpTo   = eventRec[eventLoc-1].eventdat2;
			break;
		case 68:
			randomExplosions = (eventRec[eventLoc-1].eventdat == 1);
			break;
		case 69:
			playerInvulnerable1 = eventRec[eventLoc-1].eventdat;
			playerInvulnerable2 = eventRec[eventLoc-1].eventdat;
			break;

		case 70:
			if (eventRec[eventLoc-1].eventdat2 == 0)
			{  /*1-10*/
				tempB = false;
				for (temp = 1; temp <= 19; temp++)
				{
					tempB = tempB | JE_searchFor(temp);
				}
				if (!tempB)
				{
					JE_eventJump(eventRec[eventLoc-1].eventdat);
				}
			} else {
				if (!JE_searchFor(eventRec[eventLoc-1].eventdat2)
				    && (eventRec[eventLoc-1].eventdat3 == 0 || !JE_searchFor(eventRec[eventLoc-1].eventdat3))
				    && (eventRec[eventLoc-1].eventdat4 == 0 || !JE_searchFor(eventRec[eventLoc-1].eventdat4)))
					JE_eventJump(eventRec[eventLoc-1].eventdat);
			}
			break;

		case 71:
			printf("warning: event 71: possibly bad map repositioning\n");
			if (((((intptr_t)mapYPos - (intptr_t)&megaData1->mainmap) / sizeof(JE_byte *)) * 2) <= eventRec[eventLoc-1].eventdat2) /* <MXD> ported correctly? */
			{
				JE_eventJump(eventRec[eventLoc-1].eventdat);
			}
			break;

		case 72:
			background3x1b = (eventRec[eventLoc-1].eventdat == 1);
			break;

		case 73:
			skyEnemyOverAll = (eventRec[eventLoc-1].eventdat == 1);
			break;

		case 74: /* Enemy Global BounceParams */
			for (temp = 0; temp < 100; temp++)
			{
				if (eventRec[eventLoc-1].eventdat4 == 0 || enemy[temp].linknum == eventRec[eventLoc-1].eventdat4)
				{
					if (eventRec[eventLoc-1].eventdat5 != -99)
					{
						enemy[temp].xminbounce = eventRec[eventLoc-1].eventdat5;
					}
					if (eventRec[eventLoc-1].eventdat6 != -99)
					{
						enemy[temp].yminbounce = eventRec[eventLoc-1].eventdat6;
					}
					if (eventRec[eventLoc-1].eventdat != -99)
					{
						enemy[temp].xmaxbounce = eventRec[eventLoc-1].eventdat ;
					}
					if (eventRec[eventLoc-1].eventdat2 != -99)
					{
						enemy[temp].ymaxbounce = eventRec[eventLoc-1].eventdat2;
					}
				}
			}
			break;

		case 75:

			tempB = false;
			for (temp = 0; temp < 100; temp++)
			{
				if (enemyAvail[temp] == 0
				    && enemy[temp].eyc == 0
				    && enemy[temp].linknum >= eventRec[eventLoc-1].eventdat
				    && enemy[temp].linknum <= eventRec[eventLoc-1].eventdat2)
				{
					tempB = true;
				}
			}

			if (tempB)
			{
				do {
					temp = (mt_rand() % (eventRec[eventLoc-1].eventdat2 + 1 - eventRec[eventLoc-1].eventdat)) + eventRec[eventLoc-1].eventdat;
				} while (!(JE_searchFor(temp) && enemy[temp5-1].eyc == 0));

				newPL[eventRec[eventLoc-1].eventdat3 - 80] = temp;
			} else {
				newPL[eventRec[eventLoc-1].eventdat3 - 80] = 255;
				if (eventRec[eventLoc-1].eventdat4 > 0)
				{ /*Skip*/
					curLoc = eventRec[eventLoc-1 + eventRec[eventLoc-1].eventdat4].eventtime - 1;
					eventLoc += eventRec[eventLoc-1].eventdat4 - 1;
				}
			}

			break;

		case 76:
			returnActive = true;
			break;

		case 77:
			printf("warning: event 77: possibly bad map repositioning\n");
			mapYPos = &megaData1->mainmap[0][0];
			mapYPos += eventRec[eventLoc-1].eventdat / 2;
			if (eventRec[eventLoc-1].eventdat2 > 0)
			{
				mapY2Pos = &megaData2->mainmap[0][0];
				mapY2Pos += eventRec[eventLoc-1].eventdat2 / 2;
			} else {
				mapY2Pos = &megaData2->mainmap[0][0];
				mapY2Pos += eventRec[eventLoc-1].eventdat / 2;
			}
			break;

		case 78:
			if (galagaShotFreq < 10)
			{
				galagaShotFreq++;
			}
			break;

		case 79:
			boss_bar[0].link_num = eventRec[eventLoc-1].eventdat;
			boss_bar[1].link_num = eventRec[eventLoc-1].eventdat2;
			break;

		case 80: // skip X events if in 2-player mode
			if (twoPlayerMode)
				eventLoc += eventRec[eventLoc-1].eventdat;
			break;

		case 81: /*WRAP2*/
			printf("warning: event 81: possibly bad map repositioning\n");
			BKwrap2   = &megaData2->mainmap[0][0];
			BKwrap2   += eventRec[eventLoc-1].eventdat / 2;
			BKwrap2to = &megaData2->mainmap[0][0];
			BKwrap2to += eventRec[eventLoc-1].eventdat2 / 2;
			break;

		case 82: /*Give SPECIAL WEAPON*/
			pItems[P_SPECIAL] = eventRec[eventLoc-1].eventdat;
			shotMultiPos[9-1] = 0;
			shotRepeat[9-1] = 0;
			shotMultiPos[11-1] = 0;
			shotRepeat[11-1] = 0;
			break;
		default:
			printf("warning: event %d: unhandled event\n", eventRec[eventLoc-1].eventtype);
			break;
	}

	eventLoc++;
}

void JE_whoa( void )
{
	memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen->h * VGAScreen->pitch);
	memset(VGAScreen->pixels, 0, VGAScreen->h * VGAScreen->pitch);

	service_SDL_events(true);

	tempW3 = 300;

	do
	{
		setjasondelay(1);

		Uint16 di = 640; // pixel pointer

		Uint8 *vga2pixels = VGAScreen2->pixels;
		for (Uint16 dx = 64000 - 1280; dx != 0; dx--)
		{
			Uint16 si = di + (Uint8)((Uint8)(dx >> 8) >> 5) - 4;

			Uint16 ax = vga2pixels[si] * 12;
			ax += vga2pixels[si-320];
			ax += vga2pixels[si-1];
			ax += vga2pixels[si+1];
			ax += vga2pixels[si+320];
			ax >>= 4;

			vga2pixels[di] = ax;

			di++;
		}

		di = 320 * 4;
		for (Uint16 cx = 64000 - 320*7; cx != 0; cx--)
		{
			((Uint8 *)VGAScreen->pixels)[di] = vga2pixels[di];
			di++;
		}

		tempW3--;
		
		JE_showVGA();
		
		wait_delay();
	} while (!(tempW3 == 0 || JE_anyButton()));

	levelWarningLines = 4;
}

void JE_barX( JE_word x1, JE_word y1, JE_word x2, JE_word y2, JE_byte col )
{
	JE_bar(x1, y1,     x2, y1,     col + 1);
	JE_bar(x1, y1 + 1, x2, y2 - 1, col    );
	JE_bar(x1, y2,     x2, y2,     col - 1);
}

void draw_boss_bar( void )
{
	for (unsigned int b = 0; b < COUNTOF(boss_bar); b++)
	{
		if (boss_bar[b].link_num == 0)
			continue;
		
		unsigned int armor = 256;  // higher than armor max
		
		for (unsigned int e = 0; e < COUNTOF(enemy); e++)  // find most damaged
		{
			if (enemyAvail[e] != 1 && enemy[e].linknum == boss_bar[b].link_num)
				if (enemy[e].armorleft < armor)
					armor = enemy[e].armorleft;
		}
		
		if (armor > 255 || armor == 0)  // boss dead?
			boss_bar[b].link_num = 0;
		else
			boss_bar[b].armor = (armor == 255) ? 254 : armor;  // 255 would make the bar too long
	}
	
	unsigned int bars = (boss_bar[0].link_num != 0 ? 1 : 0)
	                  + (boss_bar[1].link_num != 0 ? 1 : 0);
	
	// if only one bar left, make it the first one
	if (bars == 1 && boss_bar[0].link_num == 0)
	{
		memcpy(&boss_bar[0], &boss_bar[1], sizeof(boss_bar_t));
		boss_bar[1].link_num = 0;
	}
	
	for (unsigned int b = 0; b < bars; b++)
	{
		unsigned int x = (bars == 2)
		               ? ((b == 0) ? 125 : 185)
		               : ((levelTimer) ? 250 : 155);  // level timer and boss bar would overlap
		
		JE_barX(x - 25, 7, x + 25, 12, 115);
		JE_barX(x - (boss_bar[b].armor / 10), 7, x + (boss_bar[b].armor + 5) / 10, 12, 118 + boss_bar[b].color);
		
		if (boss_bar[b].color > 0)
			boss_bar[b].color--;
	}
}

// kate: tab-width 4; vim: set noet:
