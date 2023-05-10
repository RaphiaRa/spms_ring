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
    spms_sub *sub;
    if (spms_sub_create(&sub, "test_ring") != 0)
    {
        printf("spms_sub_create failed\n");
        return -1;
    }
    uint64_t ts;
    spms_sub_get_latest_ts(sub, &ts);
    uint32_t pos;
    spms_sub_get_pos_by_ts(sub, &pos, ts - 10);
    spms_sub_set_pos(sub, pos);
    while(!stop)
    {
        char buf[1024];
        int64_t len = 0;
        if ((len = spms_sub_read_msg(sub, buf, sizeof(buf))) > 0)
            printf("Msg: %.*s\n", (int)len, buf);
        usleep(1000);
    }
    printf("Stopping...\n");
    spms_sub_free(sub);
    return 0;
}
