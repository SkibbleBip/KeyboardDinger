/**
    Minecraft Server List Protocol API.
    Copyright (C) 2021  SkibbleBip

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/


//libasound2-dev

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <alsa/asoundlib.h>

#include "CapsOn.h"
#include "CapsOff.h"
#include "poop.h"
//data that contains the on and off sounds


/**     WAV FILE FORMAT
PCM signed 16-bit little endian
Encoding: Microsoft PCM
Num channels: 1
Sample rate: 44100
Avg Bytes per sec: 88200
Bits per sample: 16
Duration: .75 seconds
**/

#define DEVICE "default"
#define SAMPLE_RATE 44100
#define CHANNELS 1
#define DURATION .75
void beeper(void);
bool setupSpeaker(snd_pcm_t* h, snd_pcm_hw_params_t* p);

volatile bool g_beep;
uint g_pcm;
uint g_rate = SAMPLE_RATE;


int main()
{
    snd_pcm_t* pcm_handle; /*CARD'S PCM HANDLE*/
    snd_pcm_hw_params_t* param;
    snd_pcm_uframes_t frames;
    unsigned char* buffer;
    int loops;


    if (    (g_pcm = snd_pcm_open(&pcm_handle, DEVICE,	SND_PCM_STREAM_PLAYBACK, 0))     < 0){
            fprintf(stderr, "ERROR: Can't open \"%s\" PCM device. %s\n",	DEVICE, snd_strerror(g_pcm));
            return -1;
        }
        snd_pcm_hw_params_alloca(&param);

        snd_pcm_hw_params_any(pcm_handle, param);
        	/* Set parameters */
        if (    g_pcm = snd_pcm_hw_params_set_access(pcm_handle, param, SND_PCM_ACCESS_RW_INTERLEAVED)      < 0){
            fprintf(stderr, "ERROR: Can't set interleaved mode. %s\n", snd_strerror(g_pcm));
            return -1;
            }

        if (    g_pcm = snd_pcm_hw_params_set_format(pcm_handle, param, SND_PCM_FORMAT_S16_LE) < 0){
            fprintf(stderr, "ERROR: Can't set format. %s\n", snd_strerror(g_pcm));
            return -1;
            }

        if (    g_pcm = snd_pcm_hw_params_set_channels(pcm_handle, param, CHANNELS)     < 0){
            fprintf(stderr, "ERROR: Can't set channels number. %s\n", snd_strerror(g_pcm));
            return -1;
            }

        if (    g_pcm = snd_pcm_hw_params_set_rate_near(pcm_handle, param, &g_rate, 0)      < 0){
            printf("ERROR: Can't set rate. %s\n", snd_strerror(g_pcm));
            return -1;
            }

        /* Write parameters */
        if (    g_pcm = snd_pcm_hw_params(pcm_handle, param)       < 0){
            printf("ERROR: Can't set harware parameters. %s\n", snd_strerror(g_pcm));
            return -1;
            }

    fprintf(stdout, "PCM name: '%s'\n", snd_pcm_name(pcm_handle));
    fprintf(stdout, "PCM state: %s\n", snd_pcm_state_name(snd_pcm_state(pcm_handle)));



	snd_pcm_hw_params_get_period_size(param, &frames, 0);
	//get framesize to allocate buffer

	int buffer_size = frames * CHANNELS * 2;
    buffer = (unsigned char*)malloc(buffer_size);
    uint temp;
    snd_pcm_hw_params_get_period_time(param, &temp, NULL);

    unsigned int c = 0;
	for (loops = (.75 * 1000000) / temp; loops > 0; loops--) {

		memcpy(buffer, Caps_On_wav+c, buffer_size);

		if (g_pcm = snd_pcm_writei(pcm_handle, buffer, frames) == -EPIPE) {
			printf("XRUN.\n");
			snd_pcm_prepare(pcm_handle);
		} else if (g_pcm < 0) {
			printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(g_pcm));
		}
		c+=buffer_size;

	}

    /*while(1){
        if(g_beep == true){

        }
    }*/

    return 0;
}


