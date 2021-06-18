/***************************************************************************
* File:  main.c
* Author:  SkibbleBip
* Procedures:
* failedShutdown        -Handle to complete closing any open pipes and files
*                               and cleanly eit with status -1 on occurance of
*                               an error
* shutdown              -Signal handler to process the shutdown procedures
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
int g_pipeLocation;
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
                syslog(LOG_ERR, "Failed to remove PID file: %m");
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
                syslog(LOG_ERR, "Failed to remove PID file: %m");
        }

	syslog(LOG_NOTICE, "Closed. Goodbye!");
	exit(0);

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

        if(getuid() != 0){
                printf("Must be run as root\n");
                syslog(LOG_ERR, "Must be run as root\n");
                exit(0);
        }


        pid_t pid = fork();
        /*Fork the first time*/
        if(pid < 0){
                syslog(LOG_ERR, "Failed to fork: %m");
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
                syslog(LOG_ERR, "Failed to setsid: %m");
                exit(-1);
        }

        pid = fork();


        if(pid < 0){
                syslog(LOG_ERR, "Failed to fork: %m");
                exit(-1);
        }
        if(pid>0){
        /*If the fork was successful, exit cleanly*/
                syslog(LOG_NOTICE, "Successfully forked second time\n");
                exit(0);
        }
        if(chdir("/") < 0){
        /*Change the working directory to root*/
                syslog(LOG_ERR, "Failed to change to root: %m");
                failedShutdown();
        }

        /*Signal for closing application*/
        signal(SIGQUIT, shutdown);
        /*Signal for if and when the pipe breaks*/
        signal(SIGPIPE, SIG_IGN);


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
                syslog(LOG_ERR, "Failure to create PID file: %m");
                failedShutdown();

        }
        else{
                char toWrite[20];
                //itoa(getpid(), toWrite, 10);
                snprintf(toWrite, 20, "%d", getpid());
                if(0 > write(g_pidfile, toWrite, strlen(toWrite))){
                /*Write PID to PID file*/
                        syslog(LOG_ERR, "Failed to write to PID file: %m");
                        failedShutdown();
                }


        }



	mkfifo(CAPS_FILE_DESC, 0666);
	/*create the server-client FIFO pipe*/
	syslog(LOG_NOTICE, "Waiting for connection...");
	g_pipeLocation = open(CAPS_FILE_DESC, O_WRONLY);
	/*Open the pipe and wait until there is a connection to it*/
	if(g_pipeLocation <0){
                syslog(LOG_ERR, "Failed to open Program File Descriptor: %m");
                failedShutdown();
	}

        syslog(LOG_NOTICE, "Client was found!");

        g_fd = getKeyboardInputDescriptor();
        /*obtain the keyboard event file descriptor*/


        /*Every time the OS updates the state of the lock key, it sends an
        * event to the attached keyboards to inform them that the LED to the
        * partaining lock key needs to light up. We can catch these events and
        * decode them to figure out which LEDS are being set to what state.
        * This makes it easy to handle different keyboards as each keyboard is
        * sent the same struct when the LED has it's state changed.
        *
        * The signals are as follows:
        *
        * Caps on:
        *       [EV_LED]  [LED_CAPSL]  [1]
        *
        * Caps off:
        *       [EV_LED]  [LED_CAPSL]  [0]
        *
        * Num on:
        *       [EV_LED]  [LED_NUML]   [1]
        *
        * Num off:
        *       [EV_LED]  [LED_NUML]   [0]
        *
        * Sroll on:
        *       [EV_LED] [LED_SCROLLL] [1]
        *
        * Scroll off:
        *       [EV_LED] [LED_SCROLLL] [0]
        */

        while(1){
                struct input_event event;

                Status_t status;
                if(read(g_fd, &event, sizeof(struct input_event)) < 1){
                /*Read an event */
                        syslog(LOG_ERR, "Failed to read event file: %m");
                        failedShutdown();

                }
                /*check if the event mathces one of the states where an LED
                *changes
                */
                if(cmpEventVals(event, EV_LED, LED_CAPSL, HIGH)){
                        status = CAPS_ON;
                }
                else if(cmpEventVals(event, EV_LED, LED_CAPSL, LOW)){
                        status = CAPS_OFF;
                }
                else if(cmpEventVals(event, EV_LED, LED_NUML, HIGH)){
                        status = NUM_ON;
                }
                else if(cmpEventVals(event, EV_LED, LED_NUML, LOW)){
                        status = NUM_OFF;
                }
                else if(cmpEventVals(event, EV_LED, LED_SCROLLL, HIGH)){
                        status = SCROLL_ON;
                }
                else if(cmpEventVals(event, EV_LED, LED_SCROLLL, LOW)){
                        status = SCROLL_OFF;
                }
                else{
                /*if no LED state is detected, then simply continue*/
                        continue;
                }
                ///TODO: Need someone to check if scroll lock works, none of my
                ///keyboards actually have a scroll lock key apparently
                int rep = write(g_pipeLocation, &status, sizeof(Status_t));

                if(rep == -1 && errno != EPIPE){
                        /*if the write failed because the pipe is
                        *broken, don't do anything, just scream into the
                        *void. otherwise, display error and exit.
                        */
                        syslog(LOG_ERR, "Failed to write to pipe: %m");
                        failedShutdown();
                }

        }


	return 0;
}






