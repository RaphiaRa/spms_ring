#include "spms.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

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
    spms *ring;
    if (spms_sub_create(&ring, "test_ring") != 0)
    {
        printf("spms_sub_create failed\n");
        return -1;
    }
    uint64_t ts;
    spms_get_back_ts(ring, &ts);
    uint32_t pos;
    spms_get_pos_by_ts(ring, &pos, ts - 10);
    spms_set_front_pos(ring, pos);
    while(!stop)
    {
        char buf[1024];
        int64_t len = 0;
        if ((len = spms_read_msg(ring, buf, sizeof(buf))) > 0)
            printf("Msg: %.*s\n", (int)len, buf);
        usleep(1000);
    }
    printf("Stopping...\n");
    spms_free(ring);
    return 0;
}
