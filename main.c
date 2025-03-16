#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>

#include "utils.h"

#define MAX_QUEUE 100
#define MAX_CMD 128

typedef struct
{
    unsigned char cmd[MAX_CMD];
    unsigned int len;
} DATA_T;

typedef struct
{
    DATA_T data[MAX_QUEUE];
    int head;
    int tail;
    int count;
} BUFFER_T;

typedef struct
{
    BUFFER_T buffer;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} SHARED_DATA_T;

static SHARED_DATA_T _vptcTxQueue;

void queueInit(void)
{
    _vptcTxQueue.buffer.head = 0;
    _vptcTxQueue.buffer.tail = 0;
    _vptcTxQueue.buffer.count = 0;
}

int vptc_comm_write(unsigned char *cmdData, unsigned int len)
{
    int ret = -1;
    struct timespec req = {0, 1};

    if (cmdData == NULL || len > MAX_CMD)
        return -1;

    pthread_mutex_lock(&_vptcTxQueue.mutex);

    if (_vptcTxQueue.buffer.count < MAX_QUEUE)
    {
        memcpy(_vptcTxQueue.buffer.data[_vptcTxQueue.buffer.tail].cmd, cmdData, len);
        _vptcTxQueue.buffer.data[_vptcTxQueue.buffer.tail].len = len;
        _vptcTxQueue.buffer.tail = (_vptcTxQueue.buffer.tail + 1) % MAX_QUEUE;
        _vptcTxQueue.buffer.count++;

        pthread_cond_signal(&_vptcTxQueue.cond);
        ret = len;
    }

    pthread_mutex_unlock(&_vptcTxQueue.mutex);

    nanosleep(&req, NULL);
    return ret;
}

int s_tlcp_comm_vptc_read(unsigned char *cmdData)
{
    int ret = -1;

    pthread_mutex_lock(&_vptcTxQueue.mutex);

    if (_vptcTxQueue.buffer.count > 0)
    {
        ret = _vptcTxQueue.buffer.data[_vptcTxQueue.buffer.head].len;
        memcpy(cmdData, _vptcTxQueue.buffer.data[_vptcTxQueue.buffer.head].cmd, ret);
        _vptcTxQueue.buffer.head = (_vptcTxQueue.buffer.head + 1) % MAX_QUEUE;
        _vptcTxQueue.buffer.count--;
    }

    pthread_mutex_unlock(&_vptcTxQueue.mutex);
    return ret;
}

int s_tlcp_comm_vptc_poll(int timeout)
{
    if (timeout < 0)
    {
        pthread_mutex_lock(&_vptcTxQueue.mutex);
        while (_vptcTxQueue.buffer.count == 0)
        {
            pthread_cond_wait(&_vptcTxQueue.cond, &_vptcTxQueue.mutex);
        }
        pthread_mutex_unlock(&_vptcTxQueue.mutex);
        return 0;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout / 1000;
    ts.tv_nsec += (timeout % 1000) * 1000000;

    pthread_mutex_lock(&_vptcTxQueue.mutex);
    int ret = pthread_cond_timedwait(&_vptcTxQueue.cond, &_vptcTxQueue.mutex, &ts);
    pthread_mutex_unlock(&_vptcTxQueue.mutex);

    return (ret == 0) ? 0 : -1;
}

void *vptcSideCommTask(void *arg)
{
    unsigned char cmd[MAX_CMD];
    int writtenBytes;
    int totalSends = 1000;
    int successCount = 0;
    int failCount = 0;

    printMessage("TX task: Start");

    for (int i = 0; i < totalSends; i++)
    {
        unsigned int cmdLen = 8;
        cmd[0] = (unsigned char)(i & 0xFF);
        for (unsigned int j = 1; j < cmdLen; j++)
        {
            cmd[j] = (unsigned char)j;
        }

        writtenBytes = vptc_comm_write(cmd, cmdLen);
        if (writtenBytes <= 0)
        {
            failCount++;
        }
        else
        {
            successCount++;
        }
    }

    printMessage("TX task: Completed - Success: %d, Failed: %d", successCount, failCount);
    return NULL;
}

void *tlcpSideCommTask(void *arg)
{
    unsigned char cmd[MAX_CMD];
    int cmdLen;
    int receivedCount = 0;
    int lastSeq = -1;
    int outOfOrderCount = 0;

    printMessage("RX task: Start");

    while (1)
    {
        if (s_tlcp_comm_vptc_poll(-1) < 0)
        {
            continue;
        }

        cmdLen = s_tlcp_comm_vptc_read(cmd);
        if (cmdLen <= 0)
        {
            continue;
        }

        receivedCount++;
        if (lastSeq >= 0 && (cmd[0] != ((lastSeq + 1) & 0xFF)))
        {
            outOfOrderCount++;
        }
        lastSeq = cmd[0];

        // 매 100개 패킷마다 통계 출력
        if (receivedCount % 100 == 0)
        {
            printMessage("RX task: Received %d packets, Out of order: %d",
                         receivedCount, outOfOrderCount);
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
