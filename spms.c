#include "spms.h"
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <sys/syscall.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#ifdef __linux__
#define CV_USE_FUTEX 1
#elif __APPLE__
#define CV_USE_ULOCK 1
#endif

#ifdef CV_USE_FUTEX
#include <linux/futex.h>
#elif CV_USE_ULOCK
extern int __ulock_wait(uint32_t operation, void *addr, uint64_t value,
                        uint32_t timeout); /* timeout is specified in microseconds */
extern int __ulock_wait2(uint32_t operation, void *addr, uint64_t value,
                         uint64_t timeout, uint64_t value2);
extern int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value);
#endif

#define SPMS_MSG_RING_HEADER_OFFSET 128
#define SPMS_BUF_RING_HEADER_OFFSET 128

#define SPMS_SHMEM_FLAG_CREATE 1
#define SPMS_SHMEM_FLAG_PERSISTENT 2

/** spms_shmem **/

typedef struct spms_shmem
{
    char name[256];
    int32_t flags;
    int32_t fd;
    size_t len;
    void *addr;
} spms_shmem;

static int32_t spms_shmem_init(spms_shmem *shmem, const char *name, size_t len, int32_t flags)
{
    int32_t shm_flags = 0;

    if (flags & SPMS_SHMEM_FLAG_CREATE)
    {
        shm_flags |= O_CREAT;
        shm_flags |= O_EXCL;
    }
    int32_t fd = shm_open(name, shm_flags | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd != -1 && (flags & SPMS_SHMEM_FLAG_CREATE))
    {
        if (ftruncate(fd, len) == -1)
            goto err;
    }
    if (fd == -1)
    {
        if (errno != EEXIST)
            goto err;
        shm_flags &= ~O_CREAT;
        fd = shm_open(name, shm_flags | O_RDWR, S_IRUSR | S_IWUSR);
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

    snprintf(shmem->name, sizeof(shmem->name), "%s", name);
    shmem->flags = flags;
    shmem->len = len;
    shmem->addr = seg;
    shmem->fd = fd;
    return (shm_flags & O_CREAT ? 1 : 0);
err:
    if (fd != -1)
        close(fd);
    return -1;
}

static void *spms_shmem_addr(spms_shmem *shmem)
{
    return shmem->addr;
}

static size_t spms_shmem_len(spms_shmem *shmem)
{
    return shmem->len;
}

static void spms_shmem_uninit(spms_shmem *shmem)
{
    close(shmem->fd);
    munmap(shmem->addr, shmem->len);
    if (shmem->flags & SPMS_SHMEM_FLAG_CREATE && !(shmem->flags & SPMS_SHMEM_FLAG_PERSISTENT))
        shm_unlink(shmem->name);
}

/** spms_msg **/

struct spms_msg
{
    int8_t is_nil;
    int8_t is_key;
    uint8_t ver;
    uint32_t len;
    uint64_t ts;
    uint64_t offset;
};

static void spms_msg_update(struct spms_msg *msg)
{
    uint8_t ver = ++msg->ver;
    __atomic_store(&msg->ver, &ver, __ATOMIC_RELAXED);
}

static void spms_msg_release(struct spms_msg *msg)
{
    int8_t is_nil = 0;
    int8_t is_key = 0;
    uint32_t len = 0;
    uint64_t ts = 0;
    uint64_t offset = 0;
    __atomic_store(&msg->is_nil, &is_nil, __ATOMIC_RELAXED);
    __atomic_store(&msg->is_key, &is_key, __ATOMIC_RELAXED);
    __atomic_store(&msg->len, &len, __ATOMIC_RELAXED);
    __atomic_store(&msg->ts, &ts, __ATOMIC_RELAXED);
    __atomic_store(&msg->offset, &offset, __ATOMIC_RELAXED);
    spms_msg_update(msg);
}

static uint8_t spms_msg_version(struct spms_msg *msg)
{
    uint8_t ver = 0;
    __atomic_load(&msg->ver, &ver, __ATOMIC_RELAXED);
    return ver;
}

/** spms_msg_ring **/

typedef struct spms_msg_ring
{
    uint32_t *entries;
    uint32_t *mask;
    uint32_t *head;
    uint32_t *tail;
    uint32_t *last_key;
    struct spms_msg *buf;
    spms_shmem shmem;
} spms_msg_ring;

static int32_t spms_msg_ring_init(spms_msg_ring *ring, const char *name, size_t size, int32_t flags)
{
    char shmem_name[256];
    int32_t ret = 0;
    snprintf(shmem_name, sizeof(shmem_name), "%s-msg_ring", name);
    size_t total_size = size > 0 ? size * sizeof(struct spms_msg) + SPMS_MSG_RING_HEADER_OFFSET : 0;
    if ((ret = spms_shmem_init(&ring->shmem, shmem_name, total_size, flags)) < 0)
        return ret;
    void *ptr = spms_shmem_addr(&ring->shmem);
    ring->entries = (uint32_t *)ptr;
    ring->mask = (uint32_t *)ptr + 1;
    ring->head = (uint32_t *)ptr + 2;
    ring->tail = (uint32_t *)ptr + 3;
    ring->last_key = (uint32_t *)ptr + 4;
    ring->buf = (struct spms_msg *)((uint8_t *)ptr + SPMS_MSG_RING_HEADER_OFFSET);
    if (ret == 1) // created
    {
        *ring->entries = size;
        *ring->mask = *ring->entries - 1;
        *ring->head = 0;
        *ring->tail = 0;
        *ring->last_key = 0;
        memset(ring->buf, 0, size * sizeof(struct spms_msg));
    }
    return 0;
}

static void spms_msg_ring_uninit(spms_msg_ring *ring)
{
    spms_shmem_uninit(&ring->shmem);
}

/** spms_buf_ring **/

typedef struct spms_buf_ring
{
    uint64_t *length;
    uint64_t *mask;
    uint64_t *head;
    uint64_t *tail;
    void *buf;
    spms_shmem shmem;
} spms_buf_ring;

static int32_t spms_buf_ring_init(spms_buf_ring *ring, const char *name, size_t size, int32_t flags)
{
    char shmem_name[256];
    int32_t ret = 0;
    snprintf(shmem_name, sizeof(shmem_name), "%s-buf_ring", name);
    size_t total_size = size > 0 ? size + SPMS_BUF_RING_HEADER_OFFSET : 0;
    if ((ret = spms_shmem_init(&ring->shmem, shmem_name, total_size, flags)) < 0)
        return ret;
    void *ptr = spms_shmem_addr(&ring->shmem);
    ring->length = (uint64_t *)ptr;
    ring->mask = (uint64_t *)ptr + 1;
    ring->head = (uint64_t *)ptr + 2;
    ring->tail = (uint64_t *)ptr + 3;
    ring->buf = (void *)((uint8_t *)ptr + SPMS_BUF_RING_HEADER_OFFSET);
    if (ret == 1) // created
    {
        *ring->length = size;
        *ring->mask = *ring->length - 1;
        *ring->head = 0;
        *ring->tail = 0;
    }
    return 0;
}

static void spms_buf_ring_uninit(struct spms_buf_ring *ring)
{
    spms_shmem_uninit(&ring->shmem);
}

static void spms_buf_ring_release_buffer(struct spms_buf_ring *ring, uint64_t offset, uint32_t len)
{
    assert((*ring->head & *ring->mask) == offset && "Released buffer must match ring");
    *ring->head += (uint64_t)len;
}

static uint64_t spms_buf_ring_aquire_buffer(struct spms_buf_ring *ring, uint32_t *len)
{
    uint64_t avail = *ring->length - (*ring->tail - *ring->head);
    uint64_t index = *ring->tail & *ring->mask;
    uint64_t trail = *ring->length - index;
    uint64_t writeable = avail < trail ? avail : trail;
    *len = (uint32_t)((uint64_t)*len < writeable ? (uint64_t)*len : writeable);
    return index;
}

/**
 * @brief spms publisher, writes messages to the ring
 */

struct spms_pub
{
    struct spms_msg_ring msg_ring;
    struct spms_buf_ring buf_ring;
    int8_t nonblocking;
};

/** spms_pub private functions */

static void spms_pub_release_msg(spms_pub *ring, struct spms_msg *msg)
{
    spms_buf_ring_release_buffer(&ring->buf_ring, msg->offset, msg->len);
    spms_msg_release(msg);
}

static void spms_pub_release_msg_from_head(spms_pub *ring)
{
    // don't need to use atomics here, we're the only ones modifying head
    uint32_t index = *ring->msg_ring.head & *ring->msg_ring.mask;
    spms_pub_release_msg(ring, &ring->msg_ring.buf[index]);
    ++(*ring->msg_ring.head);
}

static void spms_pub_ensure_buffer_space(spms_pub *ring, size_t len)
{
    uint32_t n = 0;
    uint64_t capacity = *ring->buf_ring.length;
    while (capacity - (*ring->buf_ring.tail - *ring->buf_ring.head) < len)
    {
        spms_pub_release_msg_from_head(ring);
    }
}

static void spms_pub_flush_write_buffer_ex(spms_pub *ring, uint64_t offset, size_t len, const struct spms_msg_info *info)
{
    uint32_t tail = *ring->msg_ring.tail;
    uint32_t idx = tail & *ring->msg_ring.mask;
    struct spms_msg *msg = &ring->msg_ring.buf[idx];
    if (msg->len)
    {
        spms_pub_release_msg(ring, msg);
    }
    assert((*ring->buf_ring.tail & *ring->buf_ring.mask) == offset && "Flushed buffer must match ring");
    *ring->buf_ring.tail += (uint64_t)len;
    msg->offset = offset;
    msg->len = (uint32_t)len;
    msg->ts = info->ts;
    msg->is_nil = info->is_nil;
    msg->is_key = info->is_key;
    if (info->is_key)
        __atomic_store(ring->msg_ring.last_key, &tail, __ATOMIC_RELAXED);
    ++tail;
    __atomic_store(ring->msg_ring.tail, &tail, __ATOMIC_RELEASE);
    if (!ring->nonblocking)
    {
#ifdef CV_USE_FUTEX
        syscall(SYS_futex, ring->msg_ring.tail, FUTEX_WAKE, INT_MAX, NULL);
#elif CV_USE_ULOCK
        __ulock_wake(0x00000100, ring->msg_ring.tail, 0);
#endif
    }
}

/** spms_pub public interface **/

int32_t spms_pub_create(spms_pub **out, const char *name, struct spms_config *config, int32_t flags)
{
    int32_t ret = 0;
    spms_pub *p = (spms_pub *)calloc(1, sizeof(spms_pub));
    if (!p)
        return -1;
    p->nonblocking = flags & SPMS_FLAG_NONBLOCKING;
    size_t msg_entries = 1 << 11;
    if (config && config->msg_entries != 0)
        msg_entries = config->msg_entries;
    int32_t shmem_flags = SPMS_SHMEM_FLAG_CREATE;
    if (flags & SPMS_FLAG_PERSISTENT)
        shmem_flags |= SPMS_SHMEM_FLAG_PERSISTENT;
    if ((ret = spms_msg_ring_init(&p->msg_ring, name, msg_entries, shmem_flags)) < 0)
    {
        free(p);
        return ret;
    }

    size_t buf_length = 1 << 22;
    if (config && config->buf_length != 0)
        buf_length = config->buf_length;
    if ((ret = spms_buf_ring_init(&p->buf_ring, name, buf_length, shmem_flags)) < 0)
    {
        spms_msg_ring_uninit(&p->msg_ring);
        free(p);
        return ret;
    }
    *out = p;
    return 0;
}

int32_t spms_pub_get_write_buf(spms_pub *ring, void **addr, size_t len)
{
    while (1) // try until we get a suitable buffer
    {
        spms_pub_ensure_buffer_space(ring, len);
        uint32_t buf_len = (uint32_t)len;
        uint64_t offset = spms_buf_ring_aquire_buffer(&ring->buf_ring, &buf_len);
        if (buf_len >= len) // got a suitable buffer
        {
            *addr = (uint8_t *)ring->buf_ring.buf + offset;
            return 0;
        }
        struct spms_msg_info nil_info = {0, 1, 0};
        spms_pub_flush_write_buffer_ex(ring, offset, buf_len, &nil_info);
    }
}

int32_t spms_pub_flush_write_buf_with_info(spms_pub *ring, void *addr, size_t len, struct spms_msg_info *info)
{
    struct spms_msg_info default_info = {0, 0, 0};
    if (!info)
        info = &default_info;
    spms_pub_flush_write_buffer_ex(ring, (uint8_t *)addr - (uint8_t *)ring->buf_ring.buf, len, info);
    return 0;
}

int32_t spms_pub_write_msg(spms_pub *ring, const void *addr, size_t len)
{
    return spms_pub_write_msg_with_info(ring, addr, len, NULL);
}

int32_t spms_pub_write_msg_with_info(spms_pub *ring, const void *addr, size_t len, struct spms_msg_info *info)
{
    int32_t ret = 0;
    void *ptr = NULL;
    if ((ret = spms_pub_get_write_buf(ring, &ptr, len)) < 0)
        return ret;
    memcpy(ptr, addr, len);
    spms_pub_flush_write_buf_with_info(ring, ptr, len, info);
    return 0;
}

void spms_pub_free(spms_pub *ring)
{
    spms_buf_ring_uninit(&ring->buf_ring);
    spms_msg_ring_uninit(&ring->msg_ring);
    free(ring);
}

/**
 * @brief spms subscriber, reads messages from the ring
 */

struct spms_sub
{
    struct spms_msg_ring msg_ring;
    struct spms_buf_ring buf_ring;
    uint32_t head;
    uint32_t dropped;
    uint32_t safe_zone;
    int8_t nonblocking;
};

int32_t spms_sub_create(spms_sub **out, const char *name)
{
    int32_t ret = 0;
    spms_sub *p = (spms_sub *)calloc(1, sizeof(spms_sub));
    if (!p)
        return -1;

    if ((ret = spms_msg_ring_init(&p->msg_ring, name, 0, 0)) < 0)
    {
        free(p);
        return ret;
    }
    p->safe_zone = *p->msg_ring.entries - *p->msg_ring.entries / 16;

    if ((ret = spms_buf_ring_init(&p->buf_ring, name, 0, 0)) < 0)
    {
        spms_msg_ring_uninit(&p->msg_ring);
        free(p);
        return ret;
    }
    spms_sub_pos_rewind(p);
    *out = p;
    return 0;
}

void spms_sub_free(spms_sub *ring)
{
    spms_buf_ring_uninit(&ring->buf_ring);
    spms_msg_ring_uninit(&ring->msg_ring);
    free(ring);
}

int32_t spms_sub_set_nonblocking(spms_sub *sub, int8_t nonblocking)
{
    sub->nonblocking = nonblocking;
    return 0;
}

int32_t spms_sub_get_dropped_count(spms_sub *sub, uint64_t *count)
{
    *count = sub->dropped;
    return 0;
}

int32_t spms_sub_pos_rewind(spms_sub *ring)
{
    uint32_t tail;
    __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_RELAXED);
    ring->head = tail;
    return 0;
}

int32_t spms_sub_get_pos_by_ts(spms_sub *ring, uint32_t *pos, uint64_t ts)
{
    uint32_t head, tail;
    __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_RELAXED);
    head = tail - *ring->msg_ring.entries;
    while (head != tail)
    {
        struct spms_msg *a = &ring->msg_ring.buf[(tail - 2) & *ring->msg_ring.mask];
        struct spms_msg *b = &ring->msg_ring.buf[(tail - 1) & *ring->msg_ring.mask];

        if (a->ts <= ts && b->ts >= ts)
        {
            *pos = tail;
            return 0;
        }
        --tail;
    }
    return -1;
}

int32_t spms_sub_get_latest_ts(spms_sub *ring, uint64_t *ts)
{
    uint32_t tail;
    __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_ACQUIRE);
    struct spms_msg *msg = &ring->msg_ring.buf[(tail - 1) & *ring->msg_ring.mask];
    __atomic_load(&msg->ts, ts, __ATOMIC_RELAXED);
    return 0;
}

int32_t spms_sub_get_latest_pos(spms_sub *ring, uint32_t *pos)
{
    uint32_t tail;
    __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_RELAXED);
    *pos = (tail - 1);
    return 0;
}

int32_t spms_sub_get_latest_key_pos(spms_sub *sub, uint32_t *pos)
{
    uint32_t tail;
    __atomic_load(sub->msg_ring.tail, &tail, __ATOMIC_ACQUIRE);
    __atomic_load(sub->msg_ring.last_key, pos, __ATOMIC_RELAXED);
    if (*pos >= tail)
        return -1;
    return 0;
}

int32_t spms_sub_get_pos(spms_sub *ring, uint32_t *pos)
{
    *pos = ring->head;
    return 0;
}

int32_t spms_sub_set_pos(spms_sub *ring, uint32_t pos)
{
    ring->head = pos;
    return 0;
}

int32_t spms_sub_get_read_buf(spms_sub *ring, const void **out_addr, size_t *out_len, uint32_t timeout_ms)
{
    while (1)
    {
        uint32_t tail, idx, len;
        uint8_t ver;
        uint64_t offset;
        __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_ACQUIRE);
        if (tail - ring->head > ring->safe_zone) // ensure safe zone
        {
            uint32_t shift = (tail - ring->head) - ring->safe_zone;
            ring->dropped += shift;
            ring->head += shift;
        }
        struct spms_msg *msg = NULL;

        // skip nil packets
        while (ring->head < tail && (msg = &ring->msg_ring.buf[ring->head & *ring->msg_ring.mask])->is_nil != 0)
        {
            ++ring->head;
        }
        if (ring->head == tail)
        {
            if (ring->nonblocking || timeout_ms == 0)
                return -1;
#ifdef CV_USE_FUTEX
            struct timespec ts;
            uint32_t seconds = timeout_ms / 1000;
            ts.tv_nsec = (timeout_ms - seconds * 1000) * 1000000;
            ts.tv_sec = seconds;
            syscall(SYS_futex, ring->msg_ring.tail, FUTEX_WAIT, tail, &ts);
#elif CV_USE_ULOCK
            __ulock_wait(1, ring->msg_ring.tail, tail, timeout_ms * 1000);
#endif
            timeout_ms = 0; // don't wait again
            continue;
        }

        __atomic_load(&msg->ver, &ver, __ATOMIC_RELAXED);
        __atomic_load(&msg->offset, &offset, __ATOMIC_RELAXED);
        __atomic_load(&msg->len, &len, __ATOMIC_RELAXED);
        *out_addr = (uint8_t *)ring->buf_ring.buf + offset;
        *out_len = len;
        return ver;
    }
}

int32_t spms_sub_finalize_read_buf(spms_sub *ring, int32_t version)
{
    struct spms_msg *msg = &ring->msg_ring.buf[ring->head & *ring->msg_ring.mask];
    uint8_t ver = 0;
    uint32_t tail = 0;

    // we don't really need the tail, but we need to sync with the writer
    __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_ACQUIRE);
    __atomic_load(&msg->ver, &ver, __ATOMIC_RELAXED);
    ++ring->head;
    return (int32_t)ver == version ? 0 : -1;
}

int32_t spms_sub_read_msg(spms_sub *ring, void *addr, size_t *len, uint32_t timeout_ms)
{
    while (1)
    {
        const void *ptr = NULL;
        size_t ptr_len = 0;
        int32_t ver = spms_sub_get_read_buf(ring, &ptr, &ptr_len, timeout_ms);
        if (ver < 0 || ptr_len > *len)
            return -1;

        memcpy(addr, ptr, ptr_len);
        if (spms_sub_finalize_read_buf(ring, ver) == 0)
        {
            *len = ptr_len;
            return 0;
        }
    }
}
