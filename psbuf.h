#ifndef PSBUF_H
#define PSBUF_H

#include <stddef.h>
#include <stdint.h>

typedef struct psbuf psbuf;

struct psbuf_write_buf
{
};

struct psbuf_read_buf
{
};

struct psbuf_config
{
    uint64_t buf_len;
    uint32_t queue_len;
    uint8_t waitable;
};

/** Basic read/write api **/

int32_t psbuf_init_pub(psbuf **pub, const char *name, struct psbuf_config *config);
int32_t psbuf_write(psbuf *pub, const void *addr, size_t len);
int32_t psbuf_write_with_key(psbuf *bub, const void *addr, size_t len, uint64_t key);

int32_t psbuf_init_sub(psbuf **sub, const char *name);
int32_t psbuf_read(psbuf *sub, void *addr, size_t len);

/** Control api */

/**
 * @brief get the buffer position where the key is
 */
int32_t psbuf_get_pos_by_key(psbuf *sub, uint32_t *pos, uint64_t key);
int32_t psbuf_get_pos_near_key(psbuf *sub, uint32_t *pos, uint64_t key);

int32_t psbuf_get_head_key(psbuf *sub, uint64_t *key);
int32_t psbuf_get_head_pos(psbuf *sub, uint32_t *pos);
int32_t psbuf_get_pos(psbuf *sub, uint32_t *pos);
int32_t psbuf_set_pos(psbuf *sub, uint32_t pos);

/** Zero copy api */
int32_t psbuf_get_write_buf(psbuf *pub, psbuf_write_buf *block, size_t len);
int32_t psbuf_notify_write(psbuf *pub, psbuf_write_buf *block, size_t len);

int32_t psbuf_get_read_buf(psbuf *pub, psbuf_read_buf *block, size_t len);
int32_t psbuf_is_good(psbuf *pub, psbuf_read_buf *block);

#endif
