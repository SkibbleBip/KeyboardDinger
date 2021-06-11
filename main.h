/***************************************************************************
* File:  main.h
* Author:  SkibbleBip
* Procedures:
* PID_Lock      -Function that attempts to lock a PID file and returns the
*                       status of whether it was successful or not.
***************************************************************************/
#include <signal.h>
#include <syslog.h>
/*Handle signals and printing to syslog*/
#include <stdio.h>
#include <stdlib.h>
/*Standard IO and libraries*/
#include <errno.h>
#include <unistd.h>
#include <string.h>

#ifndef MAIN_H_INCLUDED
#define MAIN_H_INCLUDED

typedef unsigned char wavByte_t;
//define a byte as an unsigned character

const char* CAPS_FILE_DESC = "/tmp/caps_lock";
/*define the named FIFO that communicates server to client*/

/*Defines the key status for the inputted key and the status its in
 (Currently only caps lock is in use)
 */
typedef enum {  CAPS_ON,
                CAPS_OFF,
                NUM_ON,
                NUM_OFF,
                SCROLL_ON,
                SCROLL_OFF
        } Status_t;

/***************************************************************************
* int PID_Lock(char* path, int *pidfile)
* Author: SkibbleBip
* Date: 05/28/2021
* Description: Function to attempt to lock a PID file and return if successful
*               or not
*
* Parameters:
*        path   I/P     char*   The path of the PID file
*        pidfile        I/O     int*    The file descriptor of the PID file
*        PID_Lock       O/P     int     Return boolean status of if the process
*                                       was successful
**************************************************************************/
int PID_Lock(char* path, int *pidfile)
{
        *pidfile = open(path, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR | S_IWUSR);
        /*Open the PID file*/
        if(*pidfile < 0){
        /*if failed to open, return with error*/
                syslog(LOG_ERR, "Failed to open PID file %m");
                return 0;
        }
        int lock = flock(*pidfile, LOCK_EX|LOCK_NB);
        /*Lock the PID file*/
        if(lock < 0){
        /*if the lock is negative, it's already locked*/
                syslog(LOG_ERR, "PID is locked, Application is already running\n");
                return 0;
        }
        return 1;
}



#endif // MAIN_H_INCLUDED
