#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <poll.h>

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
    int event_fd;
    pthread_mutex_t mutex;
} SHARED_DATA_T;

static SHARED_DATA_T _vptcTxQueue;

void queueInit(void)
{
    _vptcTxQueue.buffer.head = 0;
    _vptcTxQueue.buffer.tail = 0;
    _vptcTxQueue.buffer.count = 0;
    _vptcTxQueue.event_fd = eventfd(0, 0);
    if (_vptcTxQueue.event_fd == -1)
    {
        printMessage("Failed to create eventfd");
    }
}

int vptc_comm_write(unsigned char *cmdData, unsigned int len)
{
    int ret = -1;
    uint64_t value = 1;

    if (cmdData == NULL || len > MAX_CMD)
    {
        printMessage("Write Error: Invalid input parameters (cmdData=%p, len=%u)", cmdData, len);
        return -1;
    }

    pthread_mutex_lock(&_vptcTxQueue.mutex);

    if (_vptcTxQueue.buffer.count >= MAX_QUEUE)
    {
        printMessage("Write Error: Queue is full (count=%d)", _vptcTxQueue.buffer.count);
        goto exit;
    }

    memcpy(_vptcTxQueue.buffer.data[_vptcTxQueue.buffer.tail].cmd, cmdData, len);
    _vptcTxQueue.buffer.data[_vptcTxQueue.buffer.tail].len = len;
    _vptcTxQueue.buffer.tail = (_vptcTxQueue.buffer.tail + 1) % MAX_QUEUE;
    _vptcTxQueue.buffer.count++;

    if (write(_vptcTxQueue.event_fd, &value, sizeof(value)) < 0)
    {
        printMessage("Write Error: Failed to signal event");
        ret = -1;
        goto exit;
    }
    ret = len;

exit:
    pthread_mutex_unlock(&_vptcTxQueue.mutex);

    usleep(1000);

    return ret;
}

int s_tlcp_comm_vptc_read(unsigned char *cmdData)
{
    int ret = -1;
    unsigned int readLen = 0;

    pthread_mutex_lock(&_vptcTxQueue.mutex);

    if (_vptcTxQueue.buffer.count <= 0)
    {
        printMessage("Read Error: Queue is empty");
        goto exit;
    }

    readLen = _vptcTxQueue.buffer.data[_vptcTxQueue.buffer.head].len;
    memcpy(cmdData, _vptcTxQueue.buffer.data[_vptcTxQueue.buffer.head].cmd, readLen);
    _vptcTxQueue.buffer.head = (_vptcTxQueue.buffer.head + 1) % MAX_QUEUE;
    _vptcTxQueue.buffer.count--;

    ret = readLen;

exit:
    pthread_mutex_unlock(&_vptcTxQueue.mutex);
    return ret;
}

int s_tlcp_comm_vptc_poll(int timeout)
{
    int ret = -1;
    uint64_t value;
    struct pollfd pfd;

    pthread_mutex_lock(&_vptcTxQueue.mutex);

    if (_vptcTxQueue.buffer.count > 0)
    {
        ret = 0;
        goto exit;
    }

    pthread_mutex_unlock(&_vptcTxQueue.mutex);

    pfd.fd = _vptcTxQueue.event_fd;
    pfd.events = POLLIN;

    ret = poll(&pfd, 1, timeout);
    if (ret > 0)
    {
        if (read(_vptcTxQueue.event_fd, &value, sizeof(value)) < 0)
        {
            printMessage("Poll Error: Failed to read event");
            return -1;
        }
        ret = 0;
    }
    else if (ret == 0)
    {
        printMessage("Poll Error: Timeout occurred after %d ms", timeout);
        return -1;
    }

    pthread_mutex_lock(&_vptcTxQueue.mutex);
    return ret;

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
    totalSends = 1000; // for overrun test

    printMessage("TX task: Start");
    printMessage("TX task will send %d commands", totalSends);

    for (int i = 0; i < totalSends; i++)
    {
        char hexStr[MAX_CMD * 3 + 1] = {0};
        printMessage("TX task: Try #%d", i + 1);

        unsigned int cmdLen = (rand() % 13 + 4);

        for (unsigned int j = 0; j < cmdLen; j++)
        {
            cmd[j] = rand() % 256;
        }

        writtenBytes = vptc_comm_write(cmd, cmdLen);

        if (writtenBytes > 0)
        {
            for (unsigned int i = 0; i < writtenBytes; i++)
            {
                sprintf(hexStr + i * 3, "%02X ", cmd[i]);
            }
            printMessage("TX task: Sent command (len=%d): %s", writtenBytes, hexStr);
        }
        else
        {
            printMessage("TX task: Queue is full");
        }
    }

    printMessage("TX task: Completed all sends");
    return NULL;
}

void *tlcpSideCommTask(void *arg)
{
    unsigned char cmd[MAX_CMD];
    int cmdLen;

    printMessage("RX task: Start");

    while (1)
    {
        if (s_tlcp_comm_vptc_poll(-1) < 0)
        {
            printMessage("RX task: Timeout occurred");
            continue;
        }

        cmdLen = s_tlcp_comm_vptc_read(cmd);
        if (cmdLen > 0)
        {
            char hexStr[MAX_CMD * 3 + 1] = {0};
            for (unsigned int i = 0; i < cmdLen; i++)
            {
                sprintf(hexStr + i * 3, "%02X ", cmd[i]);
            }
            printMessage("RX task: Received command (len=%d): %s", cmdLen, hexStr);
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
    close(_vptcTxQueue.event_fd);

    return ret;
}
