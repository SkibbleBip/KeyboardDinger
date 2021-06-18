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
* checkLoggedIn         -Function that polls the utmp file until the user has
*                               logged in
* getUserDir            -Function that returns through a referenced parameter
*                               the location of the User directory
***************************************************************************/

#include <alsa/asoundlib.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <pwd.h>
#include <dirent.h>
#include <utmp.h>
#include <sys/inotify.h>


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

/*Definitions of functions*/
int setup(Sound_Device *dev);
void playSound(const unsigned char* sound, const long int size, Sound_Device *dev);
void pollEvent(Sound_Device *dev/*, bool *stuck*/);
int checkLoggedIn(void);
void getUserDir(char* location);
void getPIDlocation(char* in);
void shutdown(int sig);
void failedShutdown(void);
int blockUntilLoggedIn(void);
int blockForPA_PID(char* pidLocation);

int main(void);


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
        if(getenv("XDG_RUNTIME_DIR")==NULL){
        /*Check if the rutime directory is defined in the environment, as ALSA
        * API needs it defined in order to initialize properly
        */
                char userDir[100];
                getUserDir(userDir);
                /*obtain the runtime directory*/

                syslog(LOG_NOTICE,
                "XDG_RUNTIME_DIR was not defined, defining it ourselves..."
                );

                if(setenv("XDG_RUNTIME_DIR", userDir, 1 ) == -1){
                /*if it's not defined, define it ourself. If it STILL wont
                *define, display an error and exit
                */
                        syslog(LOG_ERR, "Failed to set runtime directory: %m");
                        exit(-1);
                }
        }



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


        if(0 == blockUntilLoggedIn()){
        /*block until user has logged in*/
                syslog(LOG_ERR, "Failed to wait for login: %m");
                failedShutdown();

        }



        getPIDlocation(pid_location);
        /*Obtain the PID location*/
        //pid_location[0] = '\000';

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

        char pulse_pid[100];
        getUserDir(pulse_pid);
        /*obtain the location of the pulseaudio pid file*/

        memcpy(pulse_pid+strlen(pulse_pid), "/pulse/pid", 11);
        /*Complete the path of the PID file*/

        //if(blockForPA_PID(pulse_pid) == 0){
        /*wait for the creation of the PulseAudio PID file*/
        //        syslog(LOG_ERR, "Failed waiting for PulseAudio PID: %m");
        //        failedShutdown();
        //}


        while(open(pulse_pid, O_EXCL) == -1)
                ;
        /**A HACK: this polls until the pulseaudio pid exists. This could
        * be done better using blocking inotify's, but due to race conditions
        * and overcomplicating things it might be easier to wait until the pid
        * file exists. It doesn't take long anyway for pulseaudio to start
        **/

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


        ///TODO: blocking waiting and not polling waiting for caps FIFO
        while(open(CAPS_FILE_DESC, O_EXCL) == -1)
                ;
        /*while the FIFO file does not exist, continue waiting*/


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
        //unsigned char received;
        Status_t received;
        /*The byte of the received data from the pipe*/
        int size;
        /*The length of bytes read*/


        size = read(g_pipeLocation, &received, sizeof(Status_t));
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

                if(received < TOGGLE_AXIS){
                /*If the received data is a caps on enum, then play the rising
                * ding
                */
                        playSound(Caps_On_wav, Caps_On_wav_size, dev);

                }
                else /*if(received == CAPS_OFF)*/{
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
//utmp location: /run/utmp

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
/***************************************************************************
* int blockUntilLoggedIn(void)
* Author: SkibbleBip
* Date: 06/13/2021
* Description: Uses inotify syscall to block until the user has logged in.
*
* Parameters: N/A
**************************************************************************/
int blockUntilLoggedIn(void)
{

        if(checkLoggedIn())
        /*if user is already logged in, then return true*/
                return 1;


        const size_t buff_size = sizeof(struct inotify_event) + NAME_MAX + 1;
        /*obtain the size of the inotify event*/
        struct inotify_event* event = (struct inotify_event*)malloc(buff_size);
        /*dynamically allocate the inotify event*/
        int fd, wd;
        /*file decriptors of the initializer and watchdogs*/
        fd = inotify_init();
        /*Initialize the Inotify instance*/

        if(fd < 0){
        /*if failed to initialize, return false*/
                return 0;
        }

        wd = inotify_add_watch(fd, "/run/utmp", IN_MODIFY);
        /*add a watchdog for when the utmp file is modified*/

        if(wd < 0)
        /*if it failed to add the watch, return false*/
                return 0;

        u_char triggered = 0;
        /*flag for if it detected the logged in user*/

        do{
                int ret = read(fd, event, buff_size);
                /*block until an inotify event is read*/
                if(ret < 0)
                        return 0;

                if(checkLoggedIn() == 1){
                /*check if the user was logged in during the event. if they did,
                * set the flag to true
                */
                        triggered = 1;
                        //free(event);
                }

        }while(triggered == 0);
        /*continue looping until the user has been verified that they are
        * logged in
        */


        free(event);
        /*free the event buffer*/
        inotify_rm_watch(fd, wd);
        /*remove the watchdog*/
        close(fd);
        /*close the inotify file descriptor*/

        return 1;
}
/***************************************************************************
* int blockForPA_PID(char* pidLocation)
* Author: SkibbleBip
* Date: 06/13/2021
* Description: Function that blocks until the PulseAudio PID file is created,
*               using inotify
*
* Parameters:
*        pidLocation    I/P     char*   string of the location of the PID file
**************************************************************************/
int blockForPA_PID(char* pidLocation)
///TODO: It may be to complicated to use this, might be easier to wait by
///polling the PID file. Need to fix this to avoid race conditions and over-
///engineering
{
        char dir[100];
        strcpy(dir, pidLocation);
        dir[strlen(dir) - 4] = '\000';
        /*obtain just the "pulse" folder*/


        const size_t buff_size = sizeof(struct inotify_event) + NAME_MAX + 1;
        /*obtain the size of the inotify event*/
        struct inotify_event* event = (struct inotify_event*)malloc(buff_size);
        /*dynamically allocate the inotify event*/
        int fd, wd;
        /*file decriptors of the initializer and watchdogs*/
        fd = inotify_init();
        /*Initialize the Inotify instance*/

        if(fd < 0){
        /*if failed to initialize, return false*/
                return 0;
        }

        wd = inotify_add_watch(fd, dir, IN_CREATE);
        /*add a watchdog for when the pulseaaudio PID file is created*/

        if(wd < 0)
        /*if it failed to add the watch, return false*/
                return 0;


        do{

                int ret = read(fd, event, buff_size);
                /*block until an inotify event is read*/
                if(ret < 0)
                        return 0;

        }while(0 != strcmp("pid", event->name));

        free(event);
        /*free the event buffer*/
        inotify_rm_watch(fd, wd);
        /*remove the watchdog*/
        close(fd);
        /*close the inotify file descriptor*/

        return 1;

}


/*INCOMPLETE*/
int wait_for_dir_creation(int fd, char* path, char* objName)
{

        const size_t buff_size = sizeof(struct inotify_event) + NAME_MAX + 1;
        /*obtain the size of the inotify event*/
        struct inotify_event* event = (struct inotify_event*)malloc(buff_size);
        /*dynamically allocate the inotify event*/

        _Atomic int wd = inotify_add_watch(fd, path, IN_CREATE);

        if(wd < 0 && errno == ENOENT){
        /*if it failed to create watchdog because the parent dir does not
        * exist, then create a new watchdog for the parent
        */
                char tmp[50];
                char tmpObj[50];
                strcpy(tmp, path);

                ///TODO: verify that this is correct formatting
                int q = strlen(tmp);
                while(tmp[q]!='/')
                        q--;

                memcpy(tmpObj, tmp+q+1, strlen(tmp)-q);
                tmp[q] = '\000';
                /*Attempt to split the waiting dir/file up by name and location
                * path
                */

                int rep = wait_for_dir_creation(fd, tmp, tmpObj);
                /*if it had failed to wait for file/dir, then get the parent
                * dir and pass it as a child to the waiting function
                */

                return rep;

        }
        else if(wd < 0){
        /*otherwise, it failed for other reasons*/
                return 0;
        }

        do{


        int reply = read(fd, event, buff_size);
        ///TODO: Possible race condition, need to garuntee that the dir/file
        ///hasn't been created between the time of checking and waiting

        if(reply < 0)
                return 0;

        }while(0 != strcmp(event->name, objName));

        free(event);
        /*free the event buffer*/
        inotify_rm_watch(fd, wd);
        /*remove the watchdog*/
        close(fd);
        /*close the inotify file descriptor*/

        return 1;


}



/***************************************************************************
* void getUserDir(char* location)
* Author: SkibbleBip
* Date: 06/12/2021
* Description: Function that generates the current user's runtime directory ie
*       /var/run/user/X, where X is the user's UID
*
* Parameters:
*        location       I/O     char*   pointer to string to change into the
*                                               directory path and returned out
*                                               through reference
**************************************************************************/
void getUserDir(char* location)
{
        memcpy(location, "/run/user/", 10);
        /*Copy the string into the input*/
        char tmp[50];
        snprintf(tmp, 50, "%d", getuid());
        /*get the UID of the current user*/
        memcpy(location+10, tmp, strlen(tmp)+1);
        /*Copy UID (and NULL terminator)into the input*/

}

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

        getUserDir(in);
        /*Obtain the location of the User directory*/

        DIR* dir = opendir(in);
        /*check if the directory exists*/

        if(NULL==dir){
        /*if the directory doesn't exist, then set the input as blank*/
                syslog(LOG_NOTICE, "%s does not exist\n", in);
                in[0] = '\000';
                return;
        }

        if(0 != closedir(dir)){
                syslog(LOG_ALERT,"Failed to close %s: %m", in);
        }

        //char tmp[100];
        memcpy(in+strlen(in), "/CapsLockClient.pid", 20);
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





