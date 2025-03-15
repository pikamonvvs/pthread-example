#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "utils.h"

#define MAX_QUEUE 100
#define MAX_CMD 128

// Command structure
typedef struct
{
    unsigned char cmd[MAX_CMD];
    unsigned int len;
} DATA_T;

// Circular buffer structure
typedef struct
{
    DATA_T data[MAX_QUEUE];
    int head;
    int tail;
    int count;
} BUFFER_T;

// Shared data structure
typedef struct
{
    BUFFER_T buffer;
    volatile int eventOccurred;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} SHARED_DATA_T;

// 전역 변수로 선언
static SHARED_DATA_T _vptcTxQueue;

// Queue operations
void queueInit(void)
{
    _vptcTxQueue.buffer.head = 0;
    _vptcTxQueue.buffer.tail = 0;
    _vptcTxQueue.buffer.count = 0;
}

int vptc_comm_write(unsigned char *cmdData, unsigned int len)
{
    int ret = -1;

    if (cmdData == NULL || len > MAX_CMD)
    {
        return -1;
    }

    pthread_mutex_lock(&_vptcTxQueue.mutex);

    if (_vptcTxQueue.buffer.count >= MAX_QUEUE)
    {
        goto exit;
    }

    memcpy(_vptcTxQueue.buffer.data[_vptcTxQueue.buffer.tail].cmd, cmdData, len);
    _vptcTxQueue.buffer.data[_vptcTxQueue.buffer.tail].len = len;
    _vptcTxQueue.buffer.tail = (_vptcTxQueue.buffer.tail + 1) % MAX_QUEUE;
    _vptcTxQueue.buffer.count++;
    _vptcTxQueue.eventOccurred = 1;
    if (pthread_cond_signal(&_vptcTxQueue.cond) != 0)
    {
        ret = -1;
        goto exit;
    }
    ret = len;

exit:
    pthread_mutex_unlock(&_vptcTxQueue.mutex);
    return ret;
}

int s_tlcp_comm_vptc_read(unsigned char *cmdData)
{
    int ret = -1;
    unsigned int readLen = 0;

    pthread_mutex_lock(&_vptcTxQueue.mutex);

    if (_vptcTxQueue.buffer.count <= 0)
    {
        goto exit;
    }

    readLen = _vptcTxQueue.buffer.data[_vptcTxQueue.buffer.head].len;
    memcpy(cmdData, _vptcTxQueue.buffer.data[_vptcTxQueue.buffer.head].cmd, readLen);
    _vptcTxQueue.buffer.head = (_vptcTxQueue.buffer.head + 1) % MAX_QUEUE;
    _vptcTxQueue.buffer.count--;
    _vptcTxQueue.eventOccurred = 0;
    ret = readLen;

exit:
    pthread_mutex_unlock(&_vptcTxQueue.mutex);
    return ret;
}

int s_tlcp_comm_vptc_poll(int timeout_ms)
{
    int ret = -1;

    pthread_mutex_lock(&_vptcTxQueue.mutex);

    // timeout이 -1이면 무한 대기
    if (timeout_ms < 0)
    {
        while (!_vptcTxQueue.eventOccurred)
        {
            pthread_cond_wait(&_vptcTxQueue.cond, &_vptcTxQueue.mutex);
        }
    }
    else
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;

        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }

        while (!_vptcTxQueue.eventOccurred)
        {
            ret = pthread_cond_timedwait(&_vptcTxQueue.cond, &_vptcTxQueue.mutex, &ts);
            if (ret == ETIMEDOUT)
            {
                goto exit;
            }
        }
    }

    ret = 0;

exit:
    pthread_mutex_unlock(&_vptcTxQueue.mutex);
    return ret;
}

void *vptcSideCommTask(void *arg)
{
    unsigned char cmd[MAX_CMD];
    int writtenBytes;
    int totalSends;

    srand(time(NULL));
    totalSends = rand() % 11 + 5;

    printMessage("TX task", "Start", NULL, 0);
    printf("TX task will send %d commands\n", totalSends);

    for (int i = 0; i < totalSends; i++)
    {
        unsigned int cmdLen = (rand() % 13 + 4);

        for (unsigned int j = 0; j < cmdLen; j++)
        {
            cmd[j] = rand() % 256;
        }

        writtenBytes = vptc_comm_write(cmd, cmdLen);

        if (writtenBytes > 0)
        {
            printMessage("TX task", "Sent command", cmd, writtenBytes);
        }
        else
        {
            printMessage("TX task", "Queue is full", NULL, 0);
        }

        sleep(1);
    }

    printMessage("TX task", "Completed all sends", NULL, 0);
    return NULL;
}

void *tlcpSideCommTask(void *arg)
{
    unsigned char cmd[MAX_CMD];
    int cmdLen;

    printMessage("RX task", "Start", NULL, 0);

    while (1)
    {
        if (s_tlcp_comm_vptc_poll(-1) < 0)
        {
            printMessage("RX task", "Timeout occurred", NULL, 0);
            continue;
        }

        cmdLen = s_tlcp_comm_vptc_read(cmd);
        if (cmdLen > 0)
        {
            printMessage("RX task", "Received command", cmd, cmdLen);
        }
    }
    return NULL;
}

int main()
{
    pthread_t tlcpThread, vptcThread;
    int ret;

    if ((ret = pthread_mutex_init(&_vptcTxQueue.mutex, NULL)) != 0)
    {
        fprintf(stderr, "Failed to initialize mutex: %s\n", strerror(ret));
        return -1;
    }

    if ((ret = pthread_cond_init(&_vptcTxQueue.cond, NULL)) != 0)
    {
        fprintf(stderr, "Failed to initialize condition variable: %s\n", strerror(ret));
        pthread_mutex_destroy(&_vptcTxQueue.mutex);
        return -1;
    }

    _vptcTxQueue.eventOccurred = 0;
    queueInit();

    if ((ret = pthread_create(&vptcThread, NULL, vptcSideCommTask, NULL)) != 0)
    {
        fprintf(stderr, "Failed to create VPTC thread: %s\n", strerror(ret));
        goto cleanup;
    }

    if ((ret = pthread_create(&tlcpThread, NULL, tlcpSideCommTask, NULL)) != 0)
    {
        fprintf(stderr, "Failed to create TLCP thread: %s\n", strerror(ret));
        pthread_cancel(vptcThread);
        pthread_join(vptcThread, NULL);
        goto cleanup;
    }

    pthread_join(vptcThread, NULL);
    pthread_cancel(tlcpThread);
    pthread_join(tlcpThread, NULL);

cleanup:
    pthread_mutex_destroy(&_vptcTxQueue.mutex);
    pthread_cond_destroy(&_vptcTxQueue.cond);

    return ret;
}
