#include "utils.h"

#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#define MAX_CMD 128

void getCurrentTime(char *buffer)
{
    struct timeval tv;
    struct tm *tmInfo;
    time_t currentTime;

    gettimeofday(&tv, NULL);
    currentTime = tv.tv_sec;
    tmInfo = localtime(&currentTime);

    sprintf(buffer, "%02d:%02d:%02d.%06ld",
            tmInfo->tm_hour,
            tmInfo->tm_min,
            tmInfo->tm_sec,
            tv.tv_usec);
}

void printMessage(const char *taskName, const char *messageType, const unsigned char *data, unsigned int len)
{
    char timeStr[20];
    getCurrentTime(timeStr);

    if (data != NULL)
    {
        char hexStr[MAX_CMD * 3 + 1] = {0};
        for (unsigned int i = 0; i < len; i++)
        {
            sprintf(hexStr + i * 3, "%02X ", data[i]);
        }
        printf("[%s] %s: %s (len=%u): %s\n", timeStr, taskName, messageType, len, hexStr);
    }
    else
    {
        printf("[%s] %s: %s\n", timeStr, taskName, messageType);
    }
    fflush(stdout);
}
