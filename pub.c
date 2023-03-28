#include "spms_ring.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

// Get current time as string
void get_time_str(char *s, size_t max)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(s, max, "%c", tm);
}

static int stop = 0;
// Catch SIGINT
void sigint_handler(int sig)
{
    (void)sig;
    stop = 1;
}

int main()
{
    signal(SIGINT, sigint_handler);
    spms_ring *ring;
    if (spms_ring_pub_create(&ring, "test_ring", NULL, 0) != 0)
    {
        printf("spms_ring_pub_create failed\n");
        return -1;
    }
    while(!stop)
    {
        char buf[1024];
        get_time_str(buf, sizeof(buf));
        spms_ring_write_msg(ring, buf, strlen(buf));
        usleep(1000);
    }
    printf("Stopping...\n");
    spms_ring_free(ring);
}
