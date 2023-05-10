#include "spms.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

// Get current time as string
uint64_t get_time(char *s, size_t max)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(s, max, "%c", tm);
    return (uint64_t)t;
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
    spms_pub *pub;
    if (spms_pub_create(&pub, "test_ring", NULL, SPMS_FLAG_PERSISTENT) != 0)
    {
        printf("spms_pub_create failed\n");
        return -1;
    }
    while(!stop)
    {
        char buf[1024];
        uint64_t ts = get_time(buf, sizeof(buf));
        spms_pub_write_msg_with_ts(pub, buf, strlen(buf), ts);
        sleep(1);
    }
    printf("Stopping...\n");
    spms_pub_free(pub);
    return 0;
}
