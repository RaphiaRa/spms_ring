#include "spms_ring.h"
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

#define SPMS_RING_MSG_RING_HEADER_OFFSET 128
#define SPMS_RING_BUF_RING_HEADER_OFFSET 128

typedef struct spms_ring_shmem
{
    char name[256];
    uint8_t created;
    size_t len;
    void *addr;
} spms_ring_shmem;

static int32_t spms_ring_shmem_init(spms_ring_shmem *shmem, const char *name, size_t len, int flags)
{
    int fd = shm_open(name, flags | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1)
        return -1;
    if (flags & O_CREAT)
    {
        if (ftruncate(fd, len) == -1)
        {
            close(fd);
            return -1;
        }
    }
    else
    {
        len = lseek(fd, 0, SEEK_END);
    }
    void *seg = mmap(NULL, len, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    if (seg == MAP_FAILED)
    {
        close(fd);
        return -1;
    }
    close(fd);
    snprintf(shmem->name, sizeof(shmem->name), "%s", name);
    shmem->created = flags & O_CREAT;
    shmem->len = len;
    shmem->addr = seg;
    return 0;
}

static void *spms_ring_shmem_addr(spms_ring_shmem *shmem)
{
    return shmem->addr;
}

static size_t spms_ring_shmem_len(spms_ring_shmem *shmem)
{
    return shmem->len;
}

static void spms_ring_shmem_uninit(spms_ring_shmem *shmem)
{
    munmap(shmem->addr, shmem->len);
    if (shmem->created)
        shm_unlink(shmem->name);
}

struct spms_ring_msg
{
    uint8_t ver;
    uint8_t nil;
    uint32_t len;
    uint64_t ts;
    uint64_t offset;
};

static void spms_ring_msg_update(struct spms_ring_msg *msg)
{
    uint8_t ver = ++msg->ver;
    __atomic_store(&msg->ver, &ver, __ATOMIC_RELEASE);
}

int32_t spms_ring_msg_version(struct spms_ring_msg *msg)
{
    uint8_t ver = 0;
    __atomic_load(&msg->ver, &ver, __ATOMIC_ACQUIRE);
    return (int32_t)ver;
}

struct spms_ring_msg_ring
{
    uint32_t *entries;
    uint32_t *mask;
    uint32_t *head;
    uint32_t *tail;
    struct spms_ring_msg *buf;
    spms_ring_shmem shmem;
};

static int32_t spms_ring_msg_ring_init(struct spms_ring_msg_ring *ring, const char *name, size_t size, int flags)
{
    char shmem_name[256];
    int32_t ret = 0;
    snprintf(shmem_name, sizeof(shmem_name), "%s-msg_ring", name);
    size_t total_size = size > 0 ? size * sizeof(struct spms_ring_msg) + SPMS_RING_MSG_RING_HEADER_OFFSET : 0;
    if ((ret = spms_ring_shmem_init(&ring->shmem, shmem_name, total_size, flags)) < 0)
        return ret;
    void *ptr = spms_ring_shmem_addr(&ring->shmem);
    ring->entries = (uint32_t *)ptr;
    ring->mask = (uint32_t *)ptr + 1;
    ring->head = (uint32_t *)ptr + 2;
    ring->tail = (uint32_t *)ptr + 3;
    ring->buf = (struct spms_ring_msg *)((uint8_t *)ptr + SPMS_RING_MSG_RING_HEADER_OFFSET);
    if (flags & O_CREAT)
    {
        *ring->entries = size;
        *ring->mask = *ring->entries - 1;
        *ring->head = 0;
        *ring->tail = 0;
        memset(ring->buf, 0, size * sizeof(struct spms_ring_msg));
    }
    return 0;
}

static void spms_ring_msg_ring_uninit(struct spms_ring_msg_ring *ring)
{
    spms_ring_shmem_uninit(&ring->shmem);
}

struct spms_ring_buf_ring
{
    uint64_t *length;
    uint64_t *mask;
    uint64_t *head;
    uint64_t *tail;
    void *buf;
    spms_ring_shmem shmem;
};

static int32_t spms_ring_buf_ring_init(struct spms_ring_buf_ring *ring, const char *name, size_t size, int flags)
{
    char shmem_name[256];
    int32_t ret = 0;
    snprintf(shmem_name, sizeof(shmem_name), "%s-buf_ring", name);
    size_t total_size = size > 0 ? size + SPMS_RING_BUF_RING_HEADER_OFFSET : 0;
    if ((ret = spms_ring_shmem_init(&ring->shmem, shmem_name, total_size, flags)) < 0)
        return ret;
    void *ptr = spms_ring_shmem_addr(&ring->shmem);
    ring->length = (uint64_t *)ptr;
    ring->mask = (uint64_t *)ptr + 1;
    ring->head = (uint64_t *)ptr + 2;
    ring->tail = (uint64_t *)ptr + 3;
    ring->buf = (void *)((uint8_t *)ptr + SPMS_RING_BUF_RING_HEADER_OFFSET);
    if (flags & O_CREAT)
    {
        *ring->length = size;
        *ring->mask = *ring->length - 1;
        *ring->head = 0;
        *ring->tail = 0;
    }
    return 0;
}

static void spms_ring_buf_ring_uninit(struct spms_ring_buf_ring *ring)
{
    spms_ring_shmem_uninit(&ring->shmem);
}

/**
static struct spms_ring_msg *spms_ring_msg_ring_pull_write_buf(spms_ring_msg_ring *ring)
{
    uint32_t index = *ring->tail & *ring->tail;
    return &ring->buf[index];
    //__atomic_load(ring->head, &head, __ATOMIC_ACQUIRE);
}

static void spms_ring_msg_ring_flush_write(spms_ring_msg_ring *ring, struct spms_ring_msg *msg)
{
    uint32_t index = *ring->tail & *ring->tail;
    return &ring->buf[index];
    //__atomic_load(ring->head, &head, __ATOMIC_ACQUIRE);
}
**/

struct spms_ring
{
    struct spms_ring_msg_ring msg_ring;
    struct spms_ring_buf_ring buf_ring;
    uint32_t sub_head;
    uint32_t dropped;
    uint32_t safe_zone;
};

int32_t spms_ring_pub_create(spms_ring **out, const char *name, struct spms_ring_config *config, int32_t flags)
{
    int32_t ret = 0;
    spms_ring *p = (spms_ring *)calloc(1, sizeof(spms_ring));
    if (!p)
        return -1;

    size_t msg_entries = 1 << 11;
    if (config && config->msg_entries != 0)
        msg_entries = config->msg_entries;
    if ((ret = spms_ring_msg_ring_init(&p->msg_ring, name, msg_entries, flags | O_CREAT)) < 0)
    {
        free(p);
        return ret;
    }

    size_t buf_length = 1 << 22;
    if (config && config->buf_length != 0)
        buf_length = config->buf_length;
    if ((ret = spms_ring_buf_ring_init(&p->buf_ring, name, buf_length, flags | O_CREAT)) < 0)
    {
        spms_ring_msg_ring_uninit(&p->msg_ring);
        free(p);
        return ret;
    }
    *out = p;
    return 0;
}

static void spms_ring_release_msg(spms_ring *ring, struct spms_ring_msg *msg)
{
    assert((*ring->buf_ring.head & *ring->buf_ring.mask) == msg->offset && "Released buffer must match ring");
    *ring->buf_ring.head += (uint64_t)msg->len;
    msg->offset = 0;
    msg->len = 0;
    spms_ring_msg_update(msg);
}

static uint64_t spms_ring_get_buf(spms_ring *ring, uint32_t *len)
{
    uint64_t avail = *ring->buf_ring.length - (*ring->buf_ring.tail - *ring->buf_ring.head);
    uint64_t index = *ring->buf_ring.tail & *ring->buf_ring.mask;
    uint64_t trail = *ring->buf_ring.length - index;
    uint64_t writeable = avail < trail ? avail : trail;
    *len = (uint32_t)((uint64_t)*len < writeable ? (uint64_t)*len : writeable);
    return index;
}

static void spms_ring_release_msg_from_head(spms_ring *ring)
{
    uint32_t index = *ring->msg_ring.head & *ring->msg_ring.mask;
    spms_ring_release_msg(ring, &ring->msg_ring.buf[index]);
    ++(*ring->msg_ring.head);
}

static void spms_ring_ensure_avail(spms_ring *ring, size_t len)
{
    uint32_t n = 0;
    uint64_t capacity = *ring->buf_ring.length;
    while (capacity - (*ring->buf_ring.tail - *ring->buf_ring.head) < len)
    {
        spms_ring_release_msg_from_head(ring);
    }
}

static void spms_ring_flush_write_buf_ex(spms_ring *ring, uint64_t offset, size_t len, uint64_t ts, uint8_t nil)
{
    uint32_t tail = *ring->msg_ring.tail;
    uint32_t idx = tail & *ring->msg_ring.mask;
    struct spms_ring_msg *msg = &ring->msg_ring.buf[idx];
    if (msg->len)
    {
        spms_ring_release_msg(ring, msg);
    }
    assert((*ring->buf_ring.tail & *ring->buf_ring.mask) == offset && "Flushed buffer must match ring");
    *ring->buf_ring.tail += (uint64_t)len;
    msg->offset = offset;
    msg->len = (uint32_t)len;
    msg->ts = ts;
    msg->nil = nil;
    ++tail;
    __atomic_store(ring->msg_ring.tail, &tail, __ATOMIC_RELEASE);
}

int32_t spms_ring_get_write_buf(spms_ring *ring, void **addr, size_t len)
{
    while (1) // try until we get a suitable buffer
    {
        spms_ring_ensure_avail(ring, len);
        uint32_t buf_len = (uint32_t)len;
        uint64_t offset = spms_ring_get_buf(ring, &buf_len);
        if (buf_len >= len) // got a suitable buffer
        {
            *addr = (uint8_t *)ring->buf_ring.buf + offset;
            return 0;
        }
        spms_ring_flush_write_buf_ex(ring, offset, buf_len, 0, 1);
    }
}

int32_t spms_ring_flush_write_buf(spms_ring *ring, void *addr, size_t len, uint64_t key)
{
    spms_ring_flush_write_buf_ex(ring, (uint8_t *)addr - (uint8_t *)ring->buf_ring.buf, len, key, 0);
    return 0;
}

int32_t spms_ring_write_msg(spms_ring *ring, const void *addr, size_t len)
{
    return spms_ring_write_msg_with_ts(ring, addr, len, 0);
}

int32_t spms_ring_write_msg_with_ts(spms_ring *ring, const void *addr, size_t len, uint64_t key)
{
    int32_t ret = 0;
    void *ptr = NULL;
    if ((ret = spms_ring_get_write_buf(ring, &ptr, len)) < 0)
        return ret;
    memcpy(ptr, addr, len);
    spms_ring_flush_write_buf(ring, ptr, len, key);
    return 0;
}

void spms_ring_free(spms_ring *ring)
{
    spms_ring_buf_ring_uninit(&ring->buf_ring);
    spms_ring_msg_ring_uninit(&ring->msg_ring);
    free(ring);
}

int32_t spms_ring_sub_create(spms_ring **out, const char *name)
{
    int32_t ret = 0;
    spms_ring *p = (spms_ring *)calloc(1, sizeof(spms_ring));
    if (!p)
        return -1;

    if ((ret = spms_ring_msg_ring_init(&p->msg_ring, name, 0, 0)) < 0)
    {
        free(p);
        return ret;
    }
    p->safe_zone = *p->msg_ring.entries - *p->msg_ring.entries / 16;

    if ((ret = spms_ring_buf_ring_init(&p->buf_ring, name, 0, 0)) < 0)
    {
        spms_ring_msg_ring_uninit(&p->msg_ring);
        free(p);
        return ret;
    }
    spms_ring_pos_rewind(p);
    *out = p;
    return 0;
}

int32_t spms_ring_pos_rewind(spms_ring *ring)
{
    uint32_t tail;
    __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_ACQUIRE);
    ring->sub_head = tail;
    return 0;
}

int32_t spms_ring_get_pos_by_ts(spms_ring *ring, uint32_t *pos, uint64_t ts)
{
    uint32_t head, tail;
    __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_ACQUIRE);
    head = tail - *ring->msg_ring.entries;
    while (head != tail)
    {
        struct spms_ring_msg *a = &ring->msg_ring.buf[(tail - 2) & *ring->msg_ring.mask];
        struct spms_ring_msg *b = &ring->msg_ring.buf[(tail - 1) & *ring->msg_ring.mask];

        if (a->ts <= ts && b->ts >= ts)
        {
            *pos = tail;
            return 0;
        }
        --tail;
    }
    return -1;
}

int32_t spms_ring_get_back_ts(spms_ring *ring, uint64_t *ts)
{
    uint32_t tail;
    __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_ACQUIRE);
    struct spms_ring_msg *msg = &ring->msg_ring.buf[(tail - 1) & *ring->msg_ring.mask];
    *ts = msg->ts;
    return 0;
}

int32_t spms_ring_get_back_pos(spms_ring *ring, uint32_t *pos)
{
    uint32_t tail;
    __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_ACQUIRE);
    *pos = (tail - 1);
    return 0;
}

int32_t spms_ring_get_front_pos(spms_ring *ring, uint32_t *pos)
{
    *pos = ring->sub_head;
    return 0;
}

int32_t spms_ring_set_front_pos(spms_ring *ring, uint32_t pos)
{
    ring->sub_head = pos;
    return 0;
}

int32_t spms_ring_get_read_buf(spms_ring *ring, const void **out_addr, size_t *out_len)
{
    uint32_t tail, idx, len;
    uint8_t ver;
    uint64_t offset;
    __atomic_load(ring->msg_ring.tail, &tail, __ATOMIC_ACQUIRE);
    if (tail - ring->sub_head > ring->safe_zone) // ensure safe zone
    {
        ring->dropped += tail - ring->sub_head - ring->safe_zone > 0 ? tail - ring->sub_head - ring->safe_zone : 0;
        ring->sub_head = tail - ring->safe_zone;
    }
    struct spms_ring_msg *msg = NULL;
    while (ring->sub_head < tail && (msg = &ring->msg_ring.buf[ring->sub_head & *ring->msg_ring.mask])->nil != 0)
    {
        ++ring->sub_head;
        ++ring->dropped;
    }
    if (ring->sub_head == tail)
        return -1;
    __atomic_load(&msg->ver, &ver, __ATOMIC_ACQUIRE);
    __atomic_load(&msg->offset, &offset, __ATOMIC_RELAXED);
    __atomic_load(&msg->len, &len, __ATOMIC_RELAXED);
    *out_addr = (uint8_t *)ring->buf_ring.buf + offset;
    *out_len = len;
    return ver;
}

int32_t spms_ring_finalize_read_buf(spms_ring *ring, int32_t version)
{
    struct spms_ring_msg *msg = &ring->msg_ring.buf[ring->sub_head & *ring->msg_ring.mask];
    uint8_t ver;
    __atomic_load(&msg->ver, &ver, __ATOMIC_ACQUIRE);
    ++ring->sub_head;
    return ver == version ? 0 : -1;
}

int64_t spms_ring_read_msg(spms_ring *ring, void *addr, size_t len)
{
    while (1)
    {
        const void *ptr = NULL;
        size_t ptr_len = 0;
        int32_t ver = spms_ring_get_read_buf(ring, &ptr, &ptr_len);
        if (ver < 0 || ptr_len > len)
            return -1;

        memcpy(addr, ptr, ptr_len);
        if (spms_ring_finalize_read_buf(ring, ver) == 0)
            return ptr_len;
    }
}
