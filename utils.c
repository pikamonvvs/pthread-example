#include "utils.h"

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>

#define MAX_CMD 128

static pthread_mutex_t printMutex = PTHREAD_MUTEX_INITIALIZER;

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

void printMessage(const char *format, ...)
{
    pthread_mutex_lock(&printMutex);
    
    char timeStr[20];
    getCurrentTime(timeStr);
    printf("[%s] ", timeStr);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
    fflush(stdout);
    
    pthread_mutex_unlock(&printMutex);
}
