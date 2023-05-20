#ifdef __linux__
#define _GNU_SOURCE
#endif

#include "spms.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <stdalign.h>

#define TEST(x)                                                        \
    if (x == 0)                                                        \
    {                                                                  \
        printf("Test failed: %s, at %s:%d\n", #x, __FILE__, __LINE__); \
        return -1;                                                     \
    }

#define ALIGNED __attribute__((aligned(alignof(max_align_t))))

static int test_spms_pub_ctor_dtor()
{
    {
        spms_pub *pub;
        uint8_t buffer[1024 * 1024] ALIGNED = {0};
        TEST(spms_pub_create(&pub, buffer, NULL) == 0);
        spms_pub_free(pub);
    }
    {
        spms_pub *pub;
        struct spms_config config = {.buf_length = 1024, .msg_entries = 1024, .nonblocking = 0};
        uint8_t buffer[1024 * 1024] ALIGNED = {0};
        TEST(spms_pub_create(&pub, buffer, &config) == 0);
        spms_pub_free(pub);
    }
    return 0;
}

static int test_spms_sub_ctor_dtor()
{
    {
        spms_sub *sub;
        uint8_t buffer[1024 * 1024] ALIGNED = {0};
        TEST(spms_sub_create(&sub, buffer) == SPMS_ERROR_INVALID_ARG);
    }
    {
        spms_sub *sub;
        spms_pub *pub;
        uint8_t buffer[1024 * 1024] ALIGNED = {0};
        TEST(spms_pub_create(&pub, buffer, NULL) == 0);
        TEST(spms_sub_create(&sub, buffer) == 0);
        spms_sub_free(sub);
        spms_pub_free(pub);
    }
    return 0;
}

struct read_thread_args
{
    void *buf;
    int result;
};

static const unsigned char test_sequence[] = {'t', 'e', 's', 't', 's', 'e', 'q', 'u', 'e', 'n', 'c', 'e'};
static const size_t test_blocks_per_msg[] = {1, 20, 256, 490, 1024, 2040, 5500};
static const size_t test_packet_count = 10 * 1000;

static int test_write(spms_pub *pub)
{
    for (size_t i = 0; i < test_packet_count; i++)
    {
        void *addr;
        size_t blocks = test_blocks_per_msg[i % (sizeof(test_blocks_per_msg) / sizeof(test_blocks_per_msg[0]))];
        TEST(spms_pub_get_write_buf(pub, &addr, blocks * sizeof(test_sequence)) == 0);
        for (size_t j = 0; j < blocks; j++)
            memcpy(addr + j * sizeof(test_sequence), test_sequence, sizeof(test_sequence));
        TEST(spms_pub_flush_write_buf(pub, addr, blocks * sizeof(test_sequence), NULL) == 0);
        usleep(50);
    }
    spms_pub_free(pub);
    return 0;
}

static int test_read(void *buf)
{
    spms_sub *sub;
    TEST(spms_sub_create(&sub, buf) == 0);
    for (size_t i = 0; i < test_packet_count; i++)
    {
        char buf[128 * 1024];
        size_t len = sizeof(buf);
        while (spms_sub_read_msg(sub, buf, &len, NULL, 1000) == -1)
            return 0;
        for (size_t j = 0; j < len; j++)
            TEST(buf[j] == test_sequence[j % sizeof(test_sequence)]);
    }
    spms_sub_free(sub);
    return 0;
}

static void *test_read_thread(void *args)
{
    struct read_thread_args *a = args;
    a->result = test_read(a->buf);
    return NULL;
}

static int test_spms_read_write_consistency()
{
    pthread_t read_threads[4];
    struct read_thread_args args[4] = {0};

    spms_pub *pub;
    void *buf = calloc(1, 4 * 1024 * 1024);
    struct spms_config config = {.buf_length = 2 * 1024 * 1024, .msg_entries = 1024, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == 0);

    // start readers
    for (int i = 0; i < 4; i++)
    {
        args[i].buf = buf;
        pthread_create(&read_threads[i], NULL, test_read_thread, &args[i]);
    }

    TEST(test_write(pub) == 0);

    for (int i = 0; i < 4; i++)
        pthread_join(read_threads[i], NULL);
    for (int i = 0; i < 4; i++)
        TEST(args[i].result == 0);
    free(buf);
    return 0;
}

int main()
{
    TEST(test_spms_pub_ctor_dtor() == 0);
    TEST(test_spms_sub_ctor_dtor() == 0);
    TEST(test_spms_read_write_consistency() == 0);
    printf("All tests passed\n");
    return 0;
}
