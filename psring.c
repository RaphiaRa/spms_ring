#include "psring.h"
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

#define PSRING_MSG_RING_HEADER_OFFSET 128
#define PSRING_BUF_RING_HEADER_OFFSET 128

typedef struct psring_shmem
{
    char name[256];
    uint8_t created;
    size_t len;
    void *addr;
} psring_shmem;

static int32_t psring_shmem_init(psring_shmem *shmem, const char *name, size_t len, int flags)
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

static void *psring_shmem_addr(psring_shmem *shmem)
{
    return shmem->addr;
}

static size_t psring_shmem_len(psring_shmem *shmem)
{
    return shmem->len;
}

static void psring_shmem_uninit(psring_shmem *shmem)
{
    munmap(shmem->addr, shmem->len);
    if (shmem->created)
        shm_unlink(shmem->name);
}

struct psring_msg
{
    uint8_t ver;
    uint8_t nil;
    uint32_t len;
    uint64_t key;
    void *addr;
};

static void psring_msg_update(struct psring_msg *msg)
{
    uint8_t ver = ++msg->ver;
    __atomic_store(&msg->ver, &ver, __ATOMIC_RELEASE);
}

int32_t psring_msg_version(struct psring_msg *msg)
{
    uint8_t ver = 0;
    __atomic_load(&msg->ver, &ver, __ATOMIC_ACQUIRE);
    return (int32_t)ver;
}

struct psring_msg_ring
{
    uint32_t *entries;
    uint32_t *mask;
    uint32_t *head;
    uint32_t *tail;
    struct psring_msg *buf;
    psring_shmem shmem;
};

static int32_t psring_msg_ring_init(struct psring_msg_ring *ring, const char *name, size_t size, int flags)
{
    char shmem_name[256];
    int32_t ret = 0;
    snprintf(shmem_name, sizeof(shmem_name), "%s-msg_ring", name);
    if ((ret = psring_shmem_init(&ring->shmem, shmem_name, size * sizeof(struct psring_msg) + PSRING_MSG_RING_HEADER_OFFSET, flags)) < 0)
        return ret;
    void *ptr = psring_shmem_addr(&ring->shmem);
    ring->entries = (uint32_t *)ptr;
    ring->mask = (uint32_t *)ptr + 1;
    ring->head = (uint32_t *)ptr + 2;
    ring->tail = (uint32_t *)ptr + 3;
    ring->buf = (struct psring_msg *)((uint8_t *)ptr + PSRING_MSG_RING_HEADER_OFFSET);
    if (flags & O_CREAT)
    {
        *ring->entries = size;
        *ring->mask = (*ring->entries << 1) - 1;
        *ring->head = 0;
        *ring->tail = 0;
        memset(ring->buf, 0, size * sizeof(struct psring_msg));
    }
    return 0;
}

static void psring_msg_ring_uninit(struct psring_msg_ring *ring)
{
    psring_shmem_uninit(&ring->shmem);
}

struct psring_buf_ring
{
    uint64_t *length;
    uint64_t *mask;
    uint64_t *head;
    uint64_t *tail;
    psring_shmem shmem;
};

static int32_t psring_buf_ring_init(struct psring_buf_ring *ring, const char *name, size_t size, int flags)
{
    char shmem_name[256];
    int32_t ret = 0;
    snprintf(shmem_name, sizeof(shmem_name), "%s-buf_ring", name);
    if ((ret = psring_shmem_init(&ring->shmem, shmem_name, size + PSRING_BUF_RING_HEADER_OFFSET, flags)) < 0)
        return ret;
    void *ptr = psring_shmem_addr(&ring->shmem);
    ring->length = (uint64_t *)ptr;
    ring->mask = (uint64_t *)ptr + 1;
    ring->head = (uint64_t *)ptr + 2;
    ring->tail = (uint64_t *)ptr + 3;
    if (flags & O_CREAT)
    {
        *ring->length = size;
        *ring->mask = (*ring->length << 1) - 1;
        *ring->head = 0;
        *ring->tail = 0;
    }
    return 0;
}

static void psring_buf_ring_uninit(struct psring_buf_ring *ring)
{
    psring_shmem_uninit(&ring->shmem);
}

/**
static struct psring_msg *psring_msg_ring_pull_write_buf(psring_msg_ring *ring)
{
    uint32_t index = *ring->tail & *ring->tail;
    return &ring->buf[index];
    //__atomic_load(ring->head, &head, __ATOMIC_ACQUIRE);
}

static void psring_msg_ring_flush_write(psring_msg_ring *ring, struct psring_msg *msg)
{
    uint32_t index = *ring->tail & *ring->tail;
    return &ring->buf[index];
    //__atomic_load(ring->head, &head, __ATOMIC_ACQUIRE);
}
**/

struct psring
{
    struct psring_msg_ring msg_ring;
    struct psring_buf_ring buf_ring;
};

int32_t psring_init_pub(psring **out, const char *name, struct psring_config *config, int32_t flags)
{
    int32_t ret = 0;
    psring *p = (psring *)calloc(1, sizeof(psring));
    if (!p)
        return -1;

    size_t msg_entries = 1 << 11;
    if (config && config->msg_entries != 0)
        msg_entries = config->msg_entries;
    if ((ret = psring_msg_ring_init(&p->msg_ring, name, msg_entries, flags | O_CREAT)) < 0)
    {
        free(p);
        return ret;
    }

    size_t buf_length = 1 << 22;
    if (config && config->buf_length != 0)
        buf_length = config->buf_length;
    if ((ret = psring_buf_ring_init(&p->buf_ring, name, buf_length, flags | O_CREAT)) < 0)
    {
        psring_msg_ring_uninit(&p->msg_ring);
        free(p);
        return ret;
    }
    *out = p;
    return 0;
}

static void psring_release_msg(psring *ring, struct psring_msg *msg)
{
    psring_msg_update(msg);
    assert((uint8_t *)ring->buf_ring.shmem.addr + (*ring->msg_ring.head & *ring->msg_ring.mask) == (uint8_t *)msg->addr && "Released buffer must match ring ");
    ring->buf_ring.head += (uint64_t)msg->len;
    msg->addr = NULL;
    msg->len = 0;
}

static void *psring_get_buf(psring *ring, uint32_t *len)
{
    uint64_t avail = *ring->buf_ring.length - (*ring->buf_ring.tail - *ring->buf_ring.head);
    uint64_t index = *ring->buf_ring.tail & *ring->buf_ring.mask;
    uint64_t trail = *ring->buf_ring.length - index;
    uint64_t writeable = avail < trail ? avail : trail;
    *len = (uint32_t)((uint64_t)*len < writeable ? (uint64_t)*len : writeable);
    return (uint8_t *)ring->buf_ring.shmem.addr + index;
}

static void psring_release_msg_from_head(psring *ring, uint32_t offset)
{
    uint32_t index = (*ring->msg_ring.head + offset) & *ring->msg_ring.mask;
    psring_release_msg(ring, &ring->msg_ring.buf[index]);
}

static void psring_ensure_avail(psring *ring, size_t len)
{
    uint32_t n = 0;
    uint64_t capacity = *ring->buf_ring.length;
    while (capacity - (*ring->buf_ring.tail - *ring->buf_ring.head) < len)
    {
        psring_release_msg_from_head(ring, n++);
    }
}

static void psring_flush_write_buf_ex(psring *ring, void *addr, size_t len, uint64_t key, uint8_t nil)
{
    uint32_t tail = *ring->msg_ring.tail;
    uint32_t idx = tail & *ring->msg_ring.mask;
    struct psring_msg *msg = &ring->msg_ring.buf[idx];
    if (msg->addr)
    {
        psring_release_msg(ring, msg);
    }
    msg->addr = addr;
    msg->len = (uint32_t)len;
    msg->key = key;
    msg->nil = nil;
    ++tail;
    __atomic_store(ring->msg_ring.tail, &tail, __ATOMIC_RELEASE);
}

int32_t psring_get_write_buf(psring *ring, void **addr, size_t len)
{
    while (1) // try until we get a suitable buffer
    {
        psring_ensure_avail(ring, len);
        uint32_t buf_len = (uint32_t)len;
        void *buf = psring_get_buf(ring, &buf_len);
        if (buf_len >= len) // got a suitable buffer
        {
            *addr = buf;
            return 0;
        }
        psring_flush_write_buf_ex(ring, buf, 0, 0, 1);
    }
}

int32_t psring_flush_write_buf(psring *ring, void *addr, size_t len, uint64_t key)
{
    uint32_t msg_idx = *ring->msg_ring.head & *ring->msg_ring.mask;
    struct psring_msg *msg = &ring->msg_ring.buf[msg_idx];
    if (msg->addr)
    {
        psring_release_msg(ring, msg);
    }
    psring_flush_write_buf_ex(ring, addr, len, key, 0);
    return 0;
}

int32_t psring_write(psring *ring, const void *addr, size_t len)
{
    return psring_write_with_key(ring, addr, len, 0);
}

int32_t psring_write_with_key(psring *ring, const void *addr, size_t len, uint64_t key)
{
    int32_t ret = 0;
    void *ptr = NULL;
    if ((ret = psring_get_write_buf(ring, &ptr, len)) < 0)
        return ret;
    memcpy(ptr, addr, len);
    psring_flush_write_buf(ring, ptr, len, key);
    return 0;
}

void psring_free(psring *ring)
{
    psring_buf_ring_uninit(&ring->buf_ring);
    psring_msg_ring_uninit(&ring->msg_ring);
    free(ring);
}