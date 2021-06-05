/***************************************************************************
* File:  main.c
* Author:  SkibbleBip
* Procedures:
* failedShutdown        -Handle to complete closing any open pipes and files
*                               and cleanly eit with status -1 on occurance of
*                               an error
* shutdown              -Signal handler to process the shutdown procedures
* fixPipe               -Function that handles broken pipes
* main                  -The main function
***************************************************************************/


#include <unistd.h>
#include <string.h>
#include <sys/file.h>
#include <linux/input.h>//handle input events
#include <linux/input-event-codes.h> //event codes
#include <sys/stat.h>


#include "Keyboard.h"
#include "../main.h"


#define         HIGH    1
#define         LOW     0


/*Global variables to handles and parameters*/
volatile int g_pipeLocation;
/*client-server pipe*/
int g_fd;
/*keyboard file descriptor*/
int g_pidfile;
/*PID file location*/



/***************************************************************************
* void failedShutdown(void)
* Author: SkibbleBip
* Date: 05/27/2021
* Description: Handle to complete closing any open pipes and cleanly exit
*       with a value of -1 on an error occurance.
*
* Parameters: N/A
**************************************************************************/
void failedShutdown(void)
{
        close(g_pipeLocation);
        close(g_pidfile);
        close(g_fd);
        /*close all the open files (the pid file is unlocked on closing)*/
        unlink(CAPS_FILE_DESC);
        /*Unlink the caps file pipe*/

        if(remove("/var/run/CapsLockServer.pid") != 0){
        /*remove the PID file*/
                syslog(LOG_ERR,
                "Failed to remove PID file: %s!\n",
                strerror(errno)
                );
        }

        exit(-1);
}
/***************************************************************************
* void shutdown(int sig)
* Author: SkibbleBip
* Date: 05/27/2021
* Description: Signal handler to process the shutdown procedures
*
* Parameters:
*        sig    I/P     int     Signal value
**************************************************************************/
void shutdown(int sig)
{
        syslog(LOG_NOTICE, "Received signal %d to exit\n", sig);
        close(g_pipeLocation);
        close(g_pidfile);
        close(g_fd);
        /*close all the open files (the pid file is unlocked on closing)*/

        /*Unlink the caps file pipe*/
        unlink(CAPS_FILE_DESC);
        if(remove("/var/run/CapsLockServer.pid") != 0){
                syslog(LOG_ERR, "Failed to remove PID file: %s!\n", strerror(errno));
        }

	syslog(LOG_NOTICE, "Closed. Goodbye!");
	exit(0);

}



/***************************************************************************
* void fixPipe(int sig)
* Author: SkibbleBip
* Date: 05/23/2021
* Description: Function that handles broken pipes
*
* Parameters:
*        sig    I/P     int     Signal value that was triggered
**************************************************************************/
void fixPipe(int sig)
{

        syslog(LOG_ALERT, "Signal %d: Broken pipe detected!", sig);
        unlink(CAPS_FILE_DESC);
        mkfifo(CAPS_FILE_DESC, 0666);
        /*Unlink and recreate the FIFO named pipe*/
        g_pipeLocation = open(CAPS_FILE_DESC, O_WRONLY);
        /*Reopen the FIFO pipe. This will wait until the pipe is re-connected*/

        syslog(LOG_ALERT, "Pipe repaired!");


}
/***************************************************************************
* int main(void)
* Author: SkibbleBip
* Date: 05/23/2021
* Description: The main function
*
* Parameters:
*        main   O/P     int     The return value
**************************************************************************/
int main(void)
{


        //g_sid = 0;
        pid_t pid = fork();
        /*Fork the first time*/
        if(pid < 0){
                syslog(LOG_ERR, "Failed to fork%s\n", strerror(errno));
                exit(-1);
                /*If there was a problem forking, then display error and exit*/
        }
        if(pid>0){
                syslog(LOG_NOTICE, "Successfully forked daemon\n");
                exit(0);
                /*sucessfully forked, we can now cleanly exit*/
        }
        if(setsid() <0){
                /*Otherwise, display error and exit*/
                syslog(LOG_ERR, "Failed to setsid%s\n", strerror(errno));
                exit(-1);
        }


        /*Signal for closing application*/
        signal(SIGQUIT, shutdown);
        /*Signal for if and when the pipe breaks*/
        signal(SIGPIPE, fixPipe);
        /*Fork the second time*/
        pid = fork();


        if(pid < 0){
                syslog(LOG_ERR, "Failed to fork%s\n", strerror(errno));
                exit(-1);
        }
        if(pid>0){
        /*If the fork was successful, exit cleanly*/
                syslog(LOG_NOTICE, "Successfully forked second time\n");
                exit(0);
        }
        if(chdir("/") < 0){
        /*Change the working directory to root*/
                syslog(LOG_ERR, "Failed to change to root %s\n", strerror(errno));
                failedShutdown();
        }
        umask(0);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        /*close all IO files*/

        for(int i= sysconf(_SC_OPEN_MAX); i>=0; i--){
                close(i);
        }
        /*close any other open handles*/

        if(!PID_Lock("/var/run/CapsLockServer.pid", &g_pidfile)){
        /*if the PID file is failed to be created, then the daemon is already
        * running
        */
                syslog(LOG_ERR, "Failure to create PID file\n");
                failedShutdown();

        }
        else{
                char toWrite[20];
                //itoa(getpid(), toWrite, 10);
                snprintf(toWrite, 20, "%d", getpid());
                if(0 > write(g_pidfile, toWrite, strlen(toWrite))){
                /*Write PID to PID file*/
                        syslog(LOG_ERR,
                        "Failed to write to PID file%s\n",
                        strerror(errno)
                        );
                        failedShutdown();
                }


        }


	mkfifo(CAPS_FILE_DESC, 0666);
	/*create the server-client FIFO pipe*/
	syslog(LOG_NOTICE, "Waiting for connection...");
	g_pipeLocation = open(CAPS_FILE_DESC, O_WRONLY);
	/*Open the pipe and wait until there is a connection to it*/
	if(g_pipeLocation <0){
                syslog(LOG_ERR,
                "Failed to open Program File Descriptor %s",
                strerror(errno)
                );
                failedShutdown();
	}

        syslog(LOG_NOTICE, "Client was found!");

        g_fd = getKeyboardInputDescriptor();
        /*obtain the keyboard event file descriptor*/

        while(1){
                //pollKeyboard(g_fd);
                struct input_event event;
                /*The order the events occur in the event file is as such:
                *
                *Caps On:
                *       [EV_MSC, MSC_SCAN, KEY_CAPSLOCK]  capslock is changed
                *       [EV_KEY,  KEY_CAPSLOCK,     1  ]  capslock is depressed
                *       [EV_SYN,     EV_SYN,        0  ]  Sync event
                *       [EV_LED,  MSC_PULSELED      1  ]  LED is High
                *       [EV_SYN,     EV_SYN,        0  ]  Sync event
                *       [EV_MSC, MSC_SCAN, KEY_CAPSLOCK]  capslock is changed
                *       [EV_KEY,  KEY_CAPSLOCK,     0  ]  capslock is released
                *       [EV_SYN,     EV_SYN,        0  ]  Sync event
                *
                *Caps Off:
                *       [EV_MSC, MSC_SCAN, KEY_CAPSLOCK]  capslock is changed
                *       [EV_KEY,  KEY_CAPSLOCK,     1  ]  capslock is depressed
                *       [EV_SYN,     EV_SYN,        0  ]  Sync event
                *       [EV_MSC, MSC_SCAN, KEY_CAPSLOCK]  capslock is changed
                *       [EV_KEY,  KEY_CAPSLOCK,     0  ]  capslock is released
                *       [EV_SYN,     EV_SYN,        0  ]  Sync event
                *       [EV_LED,  MSC_PULSELED      0  ]  LED is Low
                *       [EV_SYN,     EV_SYN,        0  ]  Sync event
                *
                */
                /*For each key press, the depress action and release action are
                *both logged. Because the Caps Lock also triggers the keyboard
                *LEDs, on caps on mid-stroke the LED is triggered on and post-
                *release the LED is triggered off for caps off.*/


                Status_t status;
                if(read(g_fd, &event, sizeof(struct input_event)) < 1){
                /*Read an event */
                        syslog(LOG_ERR,
                                "Failed to read event file %s",
                                strerror(errno)
                                );
                        failedShutdown();

                }
                if(cmpEventVals(event, EV_KEY, KEY_CAPSLOCK, HIGH)){
                /*on Caps lock depressed*/
                        //There's probably a better way to do this,
                        //but at least this gives some kind of protection
                        //in the event the event file cant be read
                        if(read(g_fd, &event, sizeof(struct input_event)) < 1){
                                syslog(LOG_ERR,
                                        "Failed to read event file %s",
                                        strerror(errno)
                                        );
                                failedShutdown();
                        }
                        //read(g_fd, &event, sizeof(struct input_event));
                        if(read(g_fd, &event, sizeof(struct input_event)) < 1){
                                syslog(LOG_ERR,
                                        "Failed to read event file %s",
                                        strerror(errno)
                                        );
                                failedShutdown();
                        }
                        /*For some reason the event pipe only gives off a single
                        *event at one time. so we need to burn an event
                        */

                        if(cmpEventVals(event, EV_LED, MSC_PULSELED, HIGH)){
                                /*if LED was set on*/
                                status = CAPS_ON;
                                if(write(g_pipeLocation, &status, 1) < 0){
                                /*Write the caps lock state to the pipe*/
                                        syslog(LOG_ERR,
                                                "Failed to write to pipe: %s",
                                                strerror(errno)
                                                );
                                        failedShutdown();
                                }
                        }
                }
                if(cmpEventVals(event, EV_KEY, KEY_CAPSLOCK, LOW)){
                /*On Caps Lock released*/
                        if(read(g_fd, &event, sizeof(struct input_event)) < 1){
                                syslog(LOG_ERR,
                                        "Failed to read event file %s",
                                        strerror(errno)
                                        );
                                failedShutdown();
                        }
                        //read(g_fd, &event, sizeof(struct input_event));
                        if(read(g_fd, &event, sizeof(struct input_event)) < 1){
                                syslog(LOG_ERR,
                                        "Failed to read event file %s",
                                        strerror(errno)
                                        );
                                failedShutdown();
                        }
                        /*burn an unwanted event*/
                        if(cmpEventVals(event, EV_LED, MSC_PULSELED, LOW)){
                        /*if LED was set off*/
                                status = CAPS_OFF;
                                if(write(g_pipeLocation, &status, 1) < 0){
                                /*write the caps lock status to the pipe*/
                                        syslog(LOG_ERR,
                                                "Failed to write to pipe: %s",
                                                strerror(errno)
                                                );
                                        failedShutdown();
                                }
                        }
                }


        }


	return 0;
}






