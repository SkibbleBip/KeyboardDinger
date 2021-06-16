/***************************************************************************
* File:  Keyboard.h
* Author:  SkibbleBip
* Procedures:
* getKeyboardInputDescriptor     -Returns the file descriptor integer of the
*                                       currently used keyboard event file.
* cmpEventVals                   -Compares an input event struct to parameters
*                                       of type, code and values and returns 1
*                                       if matching and 0 if not
***************************************************************************/


#ifndef KEYBOARD_H_INCLUDED
#define KEYBOARD_H_INCLUDED


#include <errno.h>


#include "../main.h"





/***************************************************************************
* int getKeyboardInputDescriptor(void)
* Author: SkibbleBip
* Date: 05/23/2021
* Description: returns the file descriptor integer of the currently used
*                       keyboard device event file
*
* Parameters:
*        getKeyboardInputDescriptor     O/P     int     return value of the
*                                                       file descriptor
**************************************************************************/
int getKeyboardInputDescriptor(void)
{
        //returns pre-opened file descriptor
        FILE* deviceList;
        char* fileName = "/proc/bus/input/devices";
        char buffer[512];
        char* result = NULL;
        //char* event_handle;

        deviceList = fopen(fileName, "r");

        if(deviceList == NULL){
                //fprintf(stderr, "Error, failed to read %s: %d\n",fileName, errno);
                syslog(LOG_ERR, "Failed to read %s %m", fileName);
                exit(-1);
        }
        //uint c = 0;
        while(!feof(deviceList)){
                if((result = fgets(buffer, 512, deviceList)) != NULL)
                    if(strstr(result, "keyboard")!=NULL){

                        syslog(LOG_NOTICE, "Keyboard device found: %s", result);
                        if(fgets(buffer, 512, deviceList) == NULL){
                                syslog(LOG_ERR,
                                "Failed to read devices list: %s",
                                strerror(errno)); exit(-1);
                        }
                        if(fgets(buffer, 512, deviceList) == NULL){
                                syslog(LOG_ERR,
                                        "Failed to read devices list: %m"
                                        );
                                exit(-1);
                        }
                        if(fgets(buffer, 512, deviceList) == NULL){
                                syslog(LOG_ERR,
                                "Failed to read devices list: %m"
                                        );
                                exit(-1);
                        }
                        result = fgets(buffer, 512, deviceList);
                        if(result == NULL){
                                syslog(LOG_ERR,
                                        "Failed to read devices list: %m"
                                        );
                                exit(-1);
                        }
                        result = strstr(result, "event");
                        strtok(result, " ");
                        fclose(deviceList);
                        break;
                }
        }

            //result now contains the eventX
        char toCat[32] = "/dev/input/";
        char* full_path = strcat(toCat, result);
        int fd = open(full_path, O_RDONLY );
        //perror("opening");
        if(fd == -1){
                syslog(LOG_ERR, "Error opening %s: %m", full_path);
                exit(-1);
        }

        return fd;
}
/***************************************************************************
* int cmpEventVals(struct input_event e, ushort t, ushort c, int v)
* Author: SkibbleBip
* Date: 06/03/2021
* Description: Function that compares an input event struct with inputted types,
*        codes, and values. Returns 1 if the event contains the parameters and
*        0 if not
*
* Parameters:
*        e      I/P     struct input_event      The input event to compare
*        y      I/P     ushort                  the type to test the event with
*        c      I/P     ushort                  The code to test the event with
*        v      I/P     int                     the value to test the event with
*        cmpEventVals   O/P     int     Bool return of whether the comparison
*                                       succedes or not
**************************************************************************/
int cmpEventVals(struct input_event e, ushort t, ushort c, int v)
{
        if(e.type == t && e.code == c && e.value == v)
                return 1;
        return 0;
}


#endif // KEYBOARD_H_INCLUDED
