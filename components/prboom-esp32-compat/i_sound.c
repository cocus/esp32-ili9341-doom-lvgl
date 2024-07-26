/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *  System interface for sound.
 *
 *-----------------------------------------------------------------------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "z_zone.h"

#include "m_swap.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "lprintf.h"
#include "s_sound.h"

#include "doomdef.h"
#include "doomstat.h"
#include "doomtype.h"

#include "d_main.h"

#include "m_fixed.h"

#include "sndhw.h"

// #include "dbopl.h"

extern int sound_inited;

int snd_card = 0;
int mus_card = 1;
#define RATE (22050)
int snd_samplerate = RATE;

typedef int32_t fixed_pt_t; // 24.8 fixed point format
#define TO_FIXED(x) ((x) << 8)
#define FROM_FIXED(x) ((x) >> 8)

typedef struct
{
    uint8_t *samp; // NULL if slot is disabled
    int vol;
    fixed_pt_t rate_inc; // every output sample, pos is increased by this
    fixed_pt_t len;
    fixed_pt_t pos;
} snd_slot_t;

#define NO_SLOT 8

snd_slot_t slot[NO_SLOT];
/*
typedef struct {
    uint8_t reg;
    uint8_t data;
    uint16_t delay;
} imf_packet_t;

//imf is 280, 560 or 700Hz tick
#define IMF_RATE 560

typedef struct {
    imf_packet_t *imf;
    int pos;
    int len;
    int delay_to_go;
    Chip opl;
} imf_player_t;

static imf_player_t imfplayer;


static void imf_player_tick(int samps) {
    if (!imfplayer.imf) return;
    while (imfplayer.delay_to_go < samps) {
        //handle next imf packet
        imfplayer.pos++;
        if (imfplayer.pos==imfplayer.len) imfplayer.pos=0;
        Chip__WriteReg(&imfplayer.opl, imfplayer.imf[imfplayer.pos].reg, imfplayer.imf[imfplayer.pos].data);
        samps-=imfplayer.delay_to_go;
        imfplayer.delay_to_go=(imfplayer.imf[imfplayer.pos].delay*RATE)/IMF_RATE;
    }
    imfplayer.delay_to_go-=samps;
}
*/

static void snd_cb(int16_t *buf, int len)
{
    static int32_t *oplblk = NULL;
    if (!oplblk)
    {
        oplblk = calloc(len, sizeof(int32_t));
        assert(oplblk);
    }
    /*	imf_player_tick(len);
        Chip__GenerateBlock2(&imfplayer.opl, len, oplblk);*/
    for (int p = 0; p < len; p++)
    {
        int samp = oplblk[p] * 4; // mix in music
        for (int i = 0; i < NO_SLOT; i++)
        {
            if (slot[i].samp)
            {
                // mix in sound fx slot
                samp += (slot[i].samp[FROM_FIXED(slot[i].pos)]) * 128;
                // increase, unload if end
                slot[i].pos += slot[i].rate_inc;
                if (slot[i].pos > slot[i].len)
                {
                    //					printf("Slot %d done\n", i);
                    slot[i].samp = NULL;
                }
            }
        }
#if 0
		samp=samp/NO_SLOT;
#else
        if (samp < -32768)
            samp = 32768;
        if (samp > 32767)
            samp = 32767;
#endif
        buf[p] = samp;
    }
}

typedef struct
{
    uint16_t format_no;
    uint16_t samp_rate;
    uint32_t samp_ct;
    uint8_t pad[16];
    uint8_t samples[0];
} dmx_samp_t;

static void namebuf_upper(char *namebuf)
{
    for (int i = 0; i < 8; i++)
    {
        if (namebuf[i] >= 'a' && namebuf[i] <= 'z')
        {
            namebuf[i] -= 32;
        }
    }
}

int lumpnum_for_sndid(int id)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", S_sfx[id].name);
    namebuf_upper(namebuf);
    int r = W_GetNumForName(namebuf);
    //	printf("lumpnum_for_sndid: id %d is %s -> lump %d\n", id, namebuf, r);
    return r;
}

int I_StartSound(int id, int channel, int vol, int sep, int pitch, int prio)
{
    if ((channel < 0) || (channel >= NO_SLOT))
        return -1;

    dmx_samp_t *snd = (dmx_samp_t *)W_CacheLumpNum(lumpnum_for_sndid(id));
    if (snd->format_no != 3)
    {
        printf("I_StartSound: unknown format %d\n", snd->format_no);
        return -1;
    }
    sndhw_lock();
    slot[channel].samp = NULL;
    slot[channel].vol = vol;
    slot[channel].rate_inc = TO_FIXED(snd->samp_rate) / RATE;
    slot[channel].len = TO_FIXED(snd->samp_ct);
    slot[channel].pos = 0;
    slot[channel].samp = &snd->samples[0];
    sndhw_unlock();
    return channel;
}

void I_ShutdownSound(void)
{
    if (sound_inited)
    {
        lprintf(LO_INFO, "I_ShutdownSound: ");
        lprintf(LO_INFO, "\n");
        sound_inited = false;
    }
}

// static SDL_AudioSpec audio;

void I_InitSound(void)
{
    I_InitMusic();
    sndhw_init(RATE, snd_cb);

    // Finished initialization.
    lprintf(LO_INFO, "I_InitSound: sound ready\n");
}

void I_InitMusic(void)
{
    /*DBOPL_InitTables();
    Chip__Chip(&imfplayer.opl);
    Chip__Setup(&imfplayer.opl, RATE);
    imfplayer.imf=NULL;*/
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    return 1;
}
void I_StopSound(int handle)
{
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
}

int I_AnySoundStillPlaying(void)
{
    return false;
}

int I_SoundIsPlaying(int handle)
{
    return 0;
}
void I_PauseSong(int handle)
{
    //	printf("STUB: I_PauseSong %d\n", handle);
}

void I_ResumeSong(int handle)
{
    //	printf("STUB: I_ResumeSong %d\n", handle);
}

void I_StopSong(int handle)
{
    //	printf("STUB: I_StopSong %d\n", handle);
}

void I_SetMusicVolume(int volume)
{
    //	printf("STUB: I_SetMusicVolume %d\n", volume);
}

// Most of this comes from chocolate-doom's MIDI implementation. I'm still missing a MIDI2IMF or a MIDI player.
// https://github.com/chocolate-doom/chocolate-doom/blob/master/src/i_oplmusic.c#L1610
#include "midifile.h"
#include "memio.h"
#include "mus2mid.h"

#define MAXMIDLENGTH (96 * 1024)

static boolean IsMid(const byte *mem, int len)
{
    return len > 4 && !memcmp(mem, "MThd", 4);
}

struct
{
    boolean used;
    midi_file_t *midi;
    MEMFILE *stream;
    boolean need_dealloc;
} song_handles[] = {
    {false, NULL, NULL, false},
    {false, NULL, NULL, false},
    {false, NULL, NULL, false},
    {false, NULL, NULL, false},
    {false, NULL, NULL, false}};

void I_UnRegisterSong(int handle)
{
    lprintf(LO_INFO, "!!!I_UnRegisterSong: handle %d, used %d, midi %p, stream %p\n",
            handle, song_handles[handle].used, song_handles[handle].midi, song_handles[handle].stream);
    if (song_handles[handle].used)
    {
        if (song_handles[handle].midi)
        {
            MIDI_FreeFile(song_handles[handle].midi);
            song_handles[handle].midi = NULL;
        }
        if (song_handles[handle].stream)
        {
            if (song_handles[handle].need_dealloc)
            {
                song_handles[handle].need_dealloc = false;
                void *buf;
                size_t buflen;
                mem_get_buf(song_handles[handle].stream, &buf, &buflen);
                Z_Free(buf);
            }
            mem_fclose(song_handles[handle].stream);
            song_handles[handle].stream = NULL;
        }
        song_handles[handle].used = false;
    }
}

int I_RegisterSong(const void *data, size_t len)
{
    lprintf(LO_INFO, "!!!I_RegisterSong: data %p, len %d\n", data, len);
    MEMFILE *stream = mem_fopen_read(data, len);
    boolean need_dealloc = false;
    if (IsMid(data, len) && len < MAXMIDLENGTH)
    {
        lprintf(LO_INFO, "I_RegisterSong: is Midi!\n");
    }
    else
    {
        lprintf(LO_INFO, "I_RegisterSong: is MUS, needs convert!\n");
        // ConvertMus(data, len, "");
        MEMFILE *outstream = mem_fopen_write();
        if (mus2mid(stream, outstream) == 0)
        {
            mem_fclose(stream);
            void *buf;
            size_t buflen;
            mem_get_buf(outstream, &buf, &buflen);
            stream = mem_fopen_read(buf, buflen);
            Z_Free(outstream);
            need_dealloc = true;
            lprintf(LO_INFO, "I_RegisterSong: mus2mid success!!!!!\n");
        }
        else
        {
            lprintf(LO_INFO, "I_RegisterSong: failed mus2mid\n");
        }
    }

    for (int i = 0; i < sizeof(song_handles) / sizeof(song_handles[0]); i++)
    {
        if (song_handles[i].used == false)
        {
            midi_file_t *result = MIDI_LoadFile(stream);

            if (result == NULL)
            {
                fprintf(stderr, "I_RegisterSong: Failed to load MID.\n");
                goto cleanup;
            }
            else
            {
                lprintf(LO_INFO, "I_RegisterSong: assigned hdl %d, stream %p, midi %p\n", i, stream, result);
                song_handles[i].used = true;
                song_handles[i].midi = result;
                song_handles[i].stream = stream;
                song_handles[i].need_dealloc = need_dealloc;
            }

            return i;
        }
    }

cleanup:
    mem_fclose(stream);
    return 0;
}

void I_PlaySong(int handle, int looping)
{
    lprintf(LO_INFO, "!!!I_PlaySong: handle %d, looping %d\n", handle, looping);

    /*
    if(!data) return;
    sndhw_lock();
    imfplayer.imf=(imf_packet_t*)data;
    imfplayer.pos=0;
    imfplayer.len=len/sizeof(imf_packet_t);
    imfplayer.delay_to_go=0;
    sndhw_unlock();*/
}

void I_SetChannels(void)
{
}

int I_RegisterMusic(const char *filename, musicinfo_t *song)
{
    if (!song)
    {
        lprintf(LO_INFO, "!!!I_RegisterMusic: play '%s', NO SONG\n", filename ? filename : "null");

        return -1; // failed
    }
    lprintf(LO_INFO, "!!!I_RegisterMusic: play '%s', name '%s', lumpnum %d, handle %d\n",
            filename ? filename : "null", song->name, song->lumpnum, song->handle);
    return -1; // failed
}
