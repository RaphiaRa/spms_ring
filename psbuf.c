#include "psbuf.h"
#include <stdlib.h>

struct psbuf_msg
{
    uint8_t ver1;
    uint8_t ver2;
    uint32_t capacity;
    uint32_t len;
    uint64_t key;
    uint8_t *ptr;
};

typedef struct psbuf_msg_ring
{
    uint32_t capacity;
    uint32_t mask;
    uint32_t head;
    uint32_t tail;
    struct psbuf_msg *buf;
} psbuf_msg_ring;

static int32_t psbuf_msg_ring_init(struct psbuf_msg_ring *ring)
{
}

struct psbuf
{
};

int32_t psbuf_init_pub(psbuf **out, const char *name, struct psbuf_config *config)
{
    sizeof(struct psbuf_msg);
    psbuf *p = (psbuf *)calloc(1, sizeof(psbuf));
    if (!p)
        return -1;
}