/***************************************************************************
* File:  main.c
* Author:  SkibbleBip
* Procedures:
* getPIDlocation        -Function that generates the location the PID file is
*                               stored
* shutdown              -Signal handler to process the shutdown procedures
* failedShutdown        -Function that concludes the shutdown procedures in the
*                                event that an error were to occur
* main                  -The main function
* setup                 -Function that prepares the ALSA PCM handle and
*                               properties for playback
* playSound             -Plays sound in accordance to the inputted byte array,
*                               data size, and PCM device as params
* pollEvent             -Function that polls the pipe for new information on
*                               the status of the keyboard dings
***************************************************************************/

#include <alsa/asoundlib.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <pwd.h>
#include <dirent.h>
#include <utmp.h>


#include "CapsOn.h"
#include "CapsOff.h"
#include "../main.h"

#define         PCM_DEVICE      "default"
#define         RATE            44100
#define         CHANNELS        1


/*Struct to contain the properties of the ALSA API PCM handles*/
typedef struct {
        snd_pcm_t *pcm_Handle;
        snd_pcm_hw_params_t *params;
        snd_pcm_uframes_t frames;
        uint periodTime;
        uint buff_size;
} Sound_Device;


/*Global variables to handles and parameters*/
int g_pidfile;
volatile int g_pipeLocation;
snd_pcm_t *g_pcmHandle;


int setup(Sound_Device *dev);
void playSound(const unsigned char* sound, const long int size, Sound_Device *dev);
void pollEvent(Sound_Device *dev/*, bool *stuck*/);
int checkLoggedIn(void);


/***************************************************************************
* void getPIDlocation(char* in)
* Author: SkibbleBip
* Date: 05/25/2021      v1: Initial
* Date: 06/07/2021      v2: sets input to NULL if invalid directory
* Date: 06/08/2021      v3: Instead of setting to NULL, it now changes the
*                               string to 0-length
* Description: Function that generates the location the PID file is stored.
*       Returns a valid string if the directory is valid, or 0-length if the
*       directory is invalid.
*
* Parameters:
*        in     I/O     char*   Input char pointer to be changed
**************************************************************************/
void getPIDlocation(char* in)
{

        memcpy(in, "/var/run/user/", 14);
        /*Copy the string into the input*/
        char tmp[50];
        snprintf(tmp, 50, "%d", getuid());
        /*get the UID of the current user*/
        memcpy(in+14, tmp, strlen(tmp)+1);
        /*Copy UID (and NULL terminator)into the input*/

        /*check if the folder exists*/
        DIR* dir = opendir(in);

        if(NULL==dir){
        /*if the directory doesn't exist, then set the input as blank*/
                syslog(LOG_NOTICE, "%s does not exist\n", in);
                in[0] = '\000';
                return;
        }

        if(0 != closedir(dir)){
                syslog(LOG_ALERT,"Failed to close %s: %m", in);
        }


        memcpy(in+14+strlen(tmp), "/CapsLockClient.pid", 20);
        /*terminate the string with the name of the PID file*/
        syslog(LOG_NOTICE, "PID file is %s\n", in);
}

/***************************************************************************
* void shutdown(int sig)
* Author: SkibbleBip
* Date: 05/23/2021
* Description: Signal handler to process the shutdown procedures
*
* Parameters:
*        sig    I/P     int     Signal value
**************************************************************************/
void shutdown(int sig){

        syslog(LOG_NOTICE, "Received signal %d to exit\n", sig);
        close(g_pipeLocation);
        close(g_pidfile);
        /*Close the pipe and PID file*/
        snd_pcm_drain(g_pcmHandle);
        snd_pcm_close(g_pcmHandle);
        /*drain and close the PCM handle*/

        char buff[100];
        getPIDlocation(buff);
        /*Obtain the ID location of the client service*/
        if(remove(buff) != 0){
        /*attempt to remove PID file*/
                syslog(LOG_ERR, "Failed to remove PID file: %m");
        }

	syslog(LOG_NOTICE, "Closed. Goodbye!");
	exit(0);


}
/***************************************************************************
* void failedShutdown(void)
* Author: SkibbleBip
* Date: 06/03/2021
* Description: Function that concludes the shutdown procedures in the event
*       that an error were to occur
*
* Parameters:
**************************************************************************/
void failedShutdown(void)
{
        syslog(LOG_CRIT, "Requesting shutdown due to failure\n");
        close(g_pipeLocation);
        /*close the pipe*/
        close(g_pidfile);
        /*close the PID file (automatically unlocked)*/
        snd_pcm_drain(g_pcmHandle);
        snd_pcm_close(g_pcmHandle);
        /*drain and close the PCM handle*/

        char buff[100];
        getPIDlocation(buff);
        /*obtain the PID location*/
        if(remove(buff) != 0){
        /*remove the PID file. If failed, syslog the error and exit*/
                syslog(LOG_ERR,
                        "Failed to remove PID file: %m!\n"//,
                        //strerror(errno)
                );
        }
        exit(-1);

}
/***************************************************************************
* int main(void)
* Author: SkibbleBip
* Date: 05/23/2021
* Description: The main function
*
* Parameters:
*        main   O/P     int     return value
**************************************************************************/
int main(void)
{

        Sound_Device device;
        /*Struct of ALSA properties*/
        char pid_location[50];
        /*Buffer to hold the location of the PID file*/

        pid_t pid = fork();
        /*Fork for the first time*/

        /*close the parent*/
        if(pid < 0){
                syslog(LOG_ERR, "Failed to fork: %m");
                exit(-1);
        }if(pid>0){
                syslog(LOG_NOTICE, "Successfully forked daemon\n");
                exit(0);
        }if(setsid() <0){
                syslog(LOG_ERR, "Failed to setsid: %m");
                exit(-1);
        }
        pid = fork();
        /*Fork second time*/
        if(pid < 0){
                syslog(LOG_ERR, "Failed to fork: %m");
                exit(-1);
        }if(pid>0){
                syslog(LOG_NOTICE, "Successfully forked second time\n");
                exit(0);
        }


        if(chdir("/") < 0){
        /*Change the directory to root*/
                syslog(LOG_ERR, "Failed to change to root: %m");
                failedShutdown();
        }
        umask(0);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        /*close the IO handles*/

        for(int i= sysconf(_SC_OPEN_MAX); i>=0; i--){
        /*close any open handles*/
                close(i);
        }

        while(!checkLoggedIn())
                ;
        /*wait until user has fully logged in*/


        getPIDlocation(pid_location);
        /*Obtain the PID location*/
        pid_location[0] = '\000';

        if(pid_location[0] == '\000'){
        /*If the default PID location is invalid, then fall back to backup
        *PID location in the tmp folder.
        */
                memcpy(pid_location, "/tmp/CapsLockClient.pid", 24);
                syslog(LOG_ALERT,
                "Could not write PID to default location, defaulting to %s\n",
                pid_location
                        );

        }



        if(!PID_Lock(pid_location, &g_pidfile)){
        /*if the PID file is failed to be created, then the daemon is already
        * running
        */
                syslog(LOG_ERR, "Failure to create PID file\n");
                failedShutdown();
        }
        else{
                char toWrite[20];
                snprintf(toWrite, 20, "%d", getpid());
                if(0 > write(g_pidfile, toWrite, strlen(toWrite))){
                /*Write PID to PID file*/
                        syslog(LOG_ERR, "Failed to write to PID file: %m");
                        failedShutdown();
                }

        }
        if(setup(&device) == 0){
        /*Set up the sound PCM device*/
                syslog(LOG_ERR, "Failed to set up sound devices: %m");
                failedShutdown();
        }
        /*Signal for closing application*/
        signal(SIGQUIT, shutdown);
        signal(SIGTERM, shutdown);


        syslog(LOG_NOTICE,
                "Opening Caps Lock detection client for %s\n",
                getlogin()
                );

        while(open(CAPS_FILE_DESC, O_EXCL) == -1)
                ;
        /*while the file does not exist, continue waiting*/
        g_pipeLocation = open(CAPS_FILE_DESC, O_RDONLY);
        /*Open the FIFO*/
        if(g_pipeLocation == -1){
        /*If invalid FIFO, then display error*/
                syslog(LOG_ERR, "Failed to open Caps File FIFO: %m");
                failedShutdown();
        }

        uid_t uid = getuid();
        /*Get the UID of the client's */
        struct passwd *pwd = getpwuid(uid);
        syslog(LOG_NOTICE, "Client connected to server for %s\n", pwd->pw_name);

        while(1){
        /*loop until told to stop*/
                pollEvent(&device/*, &isStuck*/);
                snd_pcm_prepare(device.pcm_Handle);
        }



        //snd_pcm_close(device.pcm_Handle);

        return 0;
}
/***************************************************************************
* int setup(Sound_Device *dev)
* Author: Skibblebip
* Date: 05/20/2021
* Description: Function that prepares the ALSA PCM handle and properties for
*              playback
*
* Parameters:
*        dev    I/O     Sound_Device*   The struct containing ALSA PCM handle
*                                       and properties
*        setup  O/P     int             Bool-type return value of whether there
*                                       was a failure
**************************************************************************/
int setup(Sound_Device *dev){
        uint rate = RATE;
        /*obtain the temporary value of the rate*/
        if(snd_pcm_open(
        /*open the default playback device and return it to the pcm handle*/
                &(dev->pcm_Handle),
                PCM_DEVICE,
                SND_PCM_STREAM_PLAYBACK,
                0
                ) < 0)
                return 0;

        snd_pcm_hw_params_alloca(&(dev->params));
        snd_pcm_hw_params_any(dev->pcm_Handle, dev->params);
        /*Allocate parameters and apply them to the pcm device*/
        if(snd_pcm_hw_params_set_access(
        /*Set access mode for the PCM device*/
                dev->pcm_Handle,
                dev->params,
                SND_PCM_ACCESS_RW_INTERLEAVED
                ) < 0)
		return 0;

	if(snd_pcm_hw_params_set_format(
	/*set format of the playback*/
                dev->pcm_Handle,
                dev->params,
                SND_PCM_FORMAT_S16_LE
                ) < 0)
		return 0;

	if(snd_pcm_hw_params_set_channels(
	/*Set the channel number of the playback*/
                dev->pcm_Handle,
                dev->params,
                CHANNELS
                ) < 0)
		return 0;

	if(snd_pcm_hw_params_set_rate_near(
	/*Set the rate of the playback*/
                dev->pcm_Handle,
                dev->params,
                &rate,
                0
                ) < 0)
		return 0;


	if(snd_pcm_hw_params(dev->pcm_Handle, dev->params) < 0)
	/*Write parameters*/
		return 0;


        /*Allocate the buffer to hold a single period time length*/
	snd_pcm_hw_params_get_period_size(dev->params, &(dev->frames), 0);

	dev->buff_size = dev->frames * CHANNELS *2;
	/*define the buffer size in accordance to the frames and channels size*/

	g_pcmHandle = dev->pcm_Handle;
	/*set a global pointer to the PCM handle, this will be cleared */


        return 1;
}

/***************************************************************************
* void playSound(const unsigned char* sound, const long int size,Sound_Device *dev)
* Author: SkibbleBip
* Date: 06/01/2021
* Description: Plays sound in accordance to the inputted byte array, data size,
*       and PCM device as params
*
* Parameters:
*        sound  I/P     const unsigned char*    Sound data char array
*        size   I/P     const long int          Size of the data array
*        dev    I/O     Sound_Device*           Struct containing the ALSA PCM
*                                               handle and properties
**************************************************************************/
void playSound(const unsigned char* sound, const long int size,Sound_Device *dev){
        wavByte_t* buffer = (wavByte_t*) malloc(dev->buff_size);
	long int c = 0;
	uint bSize = dev->buff_size;
	uint frms = dev->frames;
	/*frame and buffer size must change as near the end of the audio stream
	*the remaining WAV data will be too small to fit in the buffer, so the
	*frame size and buffer size will be changed so the remaining audio isnt
	*disorted
	*/
	while(size > c){
                if((size-c) < bSize){
                /*If the remaining buffer size is smaller than the default,
                * then change the frame size and buffer size*/
                        bSize = size - c;
                        frms = bSize / (CHANNELS * 2);
                }
                memcpy(buffer, sound+c, bSize);
                /*copy the sound data into the buffer*/
                snd_pcm_writei(dev->pcm_Handle, buffer, frms);
                c+=bSize;
                ///TODO: Make this look less like crap

	}
        free(buffer);
        //free the dynamic buffer



}
/***************************************************************************
* void pollEvent(Sound_Device *dev)
* Author: SkibbleBip
* Date: 06/03/2021
* Description: Function that polls the pipe for new information on the status
*               of the keyboard dings
*
* Parameters:
*        dev    I/O     Sound_Device*   The struct of ALSA PCM handle and
*                                       properties
**************************************************************************/
void pollEvent(Sound_Device *dev/*, bool *stuck*/){
        unsigned char received;
        /*The byte of the received data from the pipe*/
        int size;
        /*The length of bytes read*/


        size = read(g_pipeLocation, &received, 1);
        ///TODO: Make it so the client daemon can continue running idle when
        ///the server daemon or pipe goes offline, and can start back up when
        ///the server goes back online
        /*if(size == 0){


                if(*stuck == false){
                        *stuck = true;
                        syslog(LOG_ERR, "Pipe is disconnected!\n");

                }

                printf("one\n");
                close(g_pipeLocation);
                //g_pipeLocation = -1;
                do{
                        g_pipeLocation = open(CAPS_FILE_DESC, O_RDONLY);
                        //Crack cocaine hack that detects when the server goes
                        //offline
                }while(g_pipeLocation <0);


        }*/
        if(size == 0){
        /*If 0 bytes were read, then the server is no longer writing to the
        * pipe, so close out of the client daemon
        */
                syslog(LOG_ERR, "Server Daemon has gone offline\n");
                failedShutdown();
        }
        else if(size < 0){
        /*If the pipe was not able to be read, then a failure has occured,
        * close out of the daemon
        */
                syslog(LOG_ERR, "Failed to read the Caps Lock Pipe: %m");
                failedShutdown();

        }
        else{
                //*stuck = false;

                if(received==CAPS_ON){
                /*If the received data is a caps on enum, then play the rising
                * ding
                */
                        playSound(Caps_On_wav, Caps_On_wav_size, dev);

                }
                else if(received == CAPS_OFF){
                /*If the received data is a caps off enum, then play the
                * falling dong
                */
                        playSound(Caps_Off_wav, Caps_Off_wav_size, dev);

                }
                snd_pcm_drain(dev->pcm_Handle);
                /*Drain the pcm handle*/

        }



}

/***************************************************************************
* int checkLoggedIn(void)
* Author: Skibblebip
* Date: 06/09/2021
* Description: Checks if the user running the client has logged into a TTY or
*               other terminal
*
* Parameters:
*        checkLoggedIn  O/P     int     Boolean int that determines if user was
*                                               logged in or not yet
**************************************************************************/
int checkLoggedIn(void){

        struct utmp *tmp;
        setutent();
        /*set the utmp file pointer to the front*/

        tmp = getutent();
        /*obtain the first utmp struct*/

        while(tmp != NULL){
        /*while the end of the utmp has not been reached, check each obtained
        * struct if they are a user_process
        */
                if(tmp->ut_type == USER_PROCESS){
                        if(0 == strcmp(tmp->ut_user, getlogin())){
                        /*if the username of the process is the same as the
                        *client's log-in name, then we know the user has logged
                        *in fully.
                        */
                                return 1;
                        }
                }
                tmp = getutent();
                /*get next utmp struct*/

        }

        return 0;
        /*The user was found to be not logged in yet*/
}


