#ifdef __linux__
#define _GNU_SOURCE
#endif

#include "spms.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

/** helpers */
static int32_t create_shm(void **out, const char *name, size_t len, int32_t flags);
static uint64_t get_time(char *s, size_t max);
static void sigint_handler(int sig);

static int stop = 0;

/** Publisher Example **/

static int32_t pub()
{
    signal(SIGINT, sigint_handler);
    void *mem = NULL;
    int fd = create_shm(&mem, "test_ring", spms_ring_mem_needed_size(NULL), O_CREAT | O_EXCL);
    if (fd < 0)
    {
        printf("Failed to create shared memory\n");
        return -1;
    }

    spms_pub *pub = NULL;
    if (spms_pub_create(&pub, mem, NULL) != 0)
    {
        printf("Failed to create publisher\n");
        return -1;
    }

    int idx = 0;
    while (!stop)
    {
        char time_buf[64];
        uint64_t ts = get_time(time_buf, sizeof(time_buf));
        char buf[1024];
        int8_t is_key = (++idx % 10 == 0);
        if (is_key)
            sprintf(buf, "This is a key message");
        else
            sprintf(buf, "Msg: %s", time_buf);
        struct spms_msg_info info = {is_key, ts};
        spms_pub_write_msg(pub, buf, strlen(buf), &info);
        sleep(1);
    }
    printf("Stopping...\n");
    spms_pub_free(pub);
    close(fd);
    return 0;
}

/** Subscriber Example **/

static int32_t sub()
{
    signal(SIGINT, sigint_handler);
    void *mem = NULL;
    int fd = create_shm(&mem, "test_ring", 0, 0);
    if (fd < 0)
    {
        printf("Failed to create shared memory\n");
        return -1;
    }

    spms_sub *sub = NULL;
    if (spms_sub_create(&sub, mem) != 0)
    {
        printf("Failed to create publisher\n");
        return -1;
    }

    uint64_t ts = 0;
    spms_sub_get_latest_ts(sub, &ts);
    uint32_t pos = 0;
    if (spms_sub_get_latest_key_pos(sub, &pos) == -1)
    {
        printf("Failed to get lates key position\n");
        return -1;
    }
    spms_sub_set_pos(sub, pos);
    while (!stop)
    {
        char buf[1024];
        size_t len = sizeof(buf);
        if (spms_sub_read_msg(sub, buf, &len, NULL, 1000) == 0)
            printf("Msg: %.*s\n", (int)len, buf);
    }
    printf("Stopping...\n");
    spms_sub_free(sub);
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage: %s [pub|sub]\n", argv[0]);
        return -1;
    }
    if (strcmp(argv[1], "pub") == 0)
        return pub();
    else if (strcmp(argv[1], "sub") == 0)
        return sub();
    else
    {
        printf("Usage: %s [pub|sub]\n", argv[0]);
        return -1;
    }
}

static int32_t create_shm(void **out, const char *name, size_t len, int32_t flags)
{
    int32_t fd = shm_open(name, flags | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd != -1 && (flags & O_CREAT))
    {
        if (ftruncate(fd, len) == -1)
            goto err;
    }
    if (fd == -1)
    {
        if (errno != EEXIST)
            goto err;
        flags &= ~O_CREAT;
        fd = shm_open(name, flags | O_RDWR, S_IRUSR | S_IWUSR);
        if (fd == -1)
            goto err;
    }
    else
    {
        struct stat st;
        if (fstat(fd, &st) == -1)
            goto err;
        len = st.st_size;
    }
    void *seg = mmap(NULL, len, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    if (seg == MAP_FAILED)
        goto err;
    *out = seg;
    return fd;
err:
    if (fd != -1)
        close(fd);
    return -1;
}

static uint64_t get_time(char *s, size_t max)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(s, max, "%c", tm);
    return (uint64_t)t;
}

static void sigint_handler(int sig)
{
    (void)sig;
    stop = 1;
}
