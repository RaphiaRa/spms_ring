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

/** Publisher Constructor Test **/

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
        TEST(spms_pub_create(&pub, buffer, &config) == SPMS_ERR_OK);
        spms_pub_free(pub);
    }
    return 0;
}

/** Subscriber Constructor Test **/

static int test_spms_sub_ctor_dtor()
{
    {
        spms_sub *sub;
        uint8_t buffer[1024 * 1024] ALIGNED = {0};
        TEST(spms_sub_create(&sub, buffer) == SPMS_ERR_INVALID_ARG);
    }
    {
        spms_sub *sub;
        spms_pub *pub;
        uint8_t buffer[1024 * 1024] ALIGNED = {0};
        TEST(spms_pub_create(&pub, buffer, NULL) == SPMS_ERR_OK);
        TEST(spms_sub_create(&sub, buffer) == SPMS_ERR_OK);
        spms_sub_free(sub);
        spms_pub_free(pub);
    }
    return 0;
}

/** Read-Write Thread Test **/

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
        TEST(spms_pub_get_write_buf(pub, &addr, blocks * sizeof(test_sequence)) == SPMS_ERR_OK);
        for (size_t j = 0; j < blocks; j++)
            memcpy(addr + j * sizeof(test_sequence), test_sequence, sizeof(test_sequence));
        TEST(spms_pub_flush_write_buf(pub, addr, blocks * sizeof(test_sequence), NULL) == SPMS_ERR_OK);
        usleep(50);
    }
    spms_pub_free(pub);
    return 0;
}

static int test_read(void *buf)
{
    spms_sub *sub;
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);
    for (size_t i = 0; i < test_packet_count; i++)
    {
        char buf[128 * 1024];
        size_t len = sizeof(buf);
        int ret = 0;
        if ((ret = spms_sub_read_msg(sub, buf, &len, NULL, 1000)) == SPMS_ERR_TIMEOUT)
            return 0;
        if (ret != 0)
            return ret;

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
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);

    // start readers
    for (int i = 0; i < 4; i++)
    {
        args[i].buf = buf;
        pthread_create(&read_threads[i], NULL, test_read_thread, &args[i]);
    }

    TEST(test_write(pub) == SPMS_ERR_OK);

    for (int i = 0; i < 4; i++)
        pthread_join(read_threads[i], NULL);
    for (int i = 0; i < 4; i++)
        TEST(args[i].result == SPMS_ERR_OK);
    free(buf);
    return 0;
}

static int test_spms_readv_writev()
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    // write
    {
        struct spms_ovec iov[2] = {{.addr = "test1", .len = 4}, {.addr = "test2", .len = 4}};
        TEST(spms_pub_writev_msg(pub, iov, 2, NULL) == SPMS_ERR_OK);
    }

    // read
    {
        char buf1[4], buf2[4];
        struct spms_ivec iov[2] = {{.addr = buf1, .len = 4}, {.addr = buf2, .len = 4}};
        size_t len = 2;
        TEST(spms_sub_readv_msg(sub, iov, &len, NULL, 1000) == SPMS_ERR_OK);
        TEST(len == 2);
        TEST(iov[0].len == 4);
        TEST(iov[1].len == 4);
        TEST(memcmp(iov[0].addr, "test1", 4) == 0);
        TEST(memcmp(iov[1].addr, "test2", 4) == 0);
    }
    return 0;
}

static int test_spms_empty_read()
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    // read
    {
        char buffer[128];
        size_t len = sizeof(buffer);
        TEST(spms_sub_read_msg(sub, buffer, &len, NULL, 0) == SPMS_ERR_TIMEOUT);
    }
    return 0;
}

static int test_spms_empty_readv()
{
    spms_pub *pub;
    spms_sub *sub;
    uint8_t buf[256 * 1024] ALIGNED = {0};
    struct spms_config config = {.buf_length = 1024, .msg_entries = 128, .nonblocking = 0};
    TEST(spms_pub_create(&pub, buf, &config) == SPMS_ERR_OK);
    TEST(spms_sub_create(&sub, buf) == SPMS_ERR_OK);

    // read
    {
        char buffer[128];
        struct spms_ivec iov[2] = {{.addr = buffer, .len = sizeof(buffer)}};
        size_t len = 1;
        TEST(spms_sub_readv_msg(sub, iov, &len, NULL, 0) == SPMS_ERR_TIMEOUT);
    }
    return 0;
}

int main()
{
    TEST(test_spms_pub_ctor_dtor() == 0);
    TEST(test_spms_sub_ctor_dtor() == 0);
    TEST(test_spms_read_write_consistency() == 0);
    TEST(test_spms_readv_writev() == 0);
    TEST(test_spms_empty_read() == 0);
    TEST(test_spms_empty_readv() == 0);
    printf("All tests passed\n");
    return 0;
}
