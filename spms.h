#ifndef SPMS_H
#define SPMS_H

#include <stddef.h>
#include <stdint.h>

typedef struct spms_pub spms_pub;
typedef struct spms_sub spms_sub;

struct spms_config
{
    size_t buf_length;
    size_t msg_entries;
};

struct spms_msg_info
{
    int8_t is_key;
    int8_t is_nil;
    uint64_t ts;
};

#define SPMS_FLAG_PERSISTENT 0x01

/** Constructors and destructors **/

int32_t spms_pub_create(spms_pub **ring, const char *name, struct spms_config *config, int32_t flags);
int32_t spms_sub_create(spms_sub **ring, const char *name);
void spms_pub_free(spms_pub *ring);
void spms_sub_free(spms_sub *ring);

/** Basic read/write API **/

int32_t spms_pub_write_msg(spms_pub *ring, const void *addr, size_t len);
int32_t spms_pub_write_msg_with_info(spms_pub *ring, const void *addr, size_t len, struct spms_msg_info *info);
int32_t spms_sub_read_msg(spms_sub *ring, void *addr, size_t len);

/** Control API **/

/** spms_sub_pos_rewind
 * @brief Move the read position to the latest msg in the ring
 * @param sub The subscriber to rewind
 * @return 0 on success, -1 on failure
 */
int32_t spms_sub_pos_rewind(spms_sub *sub);

/** spms_sub_get_pos_by_ts
 * @brief Get the position of the first msg with a timestamp >= ts
 * @param sub The subscriber
 * @param pos The position of the msg
 * @param ts The timestamp to search for
 * @return 0 on success, -1 on failure
 */
int32_t spms_sub_get_pos_by_ts(spms_sub *sub, uint32_t *pos, uint64_t ts);

/** spms_sub_get_latest_ts
 * @brief Get the timestamp of the latest msg in the ring
 * @param sub The subscriber
 * @param ts The timestamp of the latest msg
 * @return 0 on success, -1 on failure
 */
int32_t spms_sub_get_latest_ts(spms_sub *sub, uint64_t *ts);

/** spms_sub_get_latest_pos
 * @brief Get the position of the latest msg in the ring
 * @param sub The subscriber
 * @param pos (out) The position of the latest msg
 * @return 0 on success, -1 on failure
 */
int32_t spms_sub_get_latest_pos(spms_sub *sub, uint32_t *pos);

/** spms_sub_get_latest_key_pos
 * @brief Get the position of the latest key msg in the ring
 * @param sub The subscriber
 * @param pos (out) The position of the latest key msg
 * @return 0 on success, -1 on failure
 */
int32_t spms_sub_get_latest_key_pos(spms_sub *sub, uint32_t *pos);

/** spms_sub_get_pos
 * @brief Get the current read position of the subscriber
 * @param sub The subscriber
 * @param pos The current read position
 * @return 0 on success, -1 on failure
 */
int32_t spms_sub_get_pos(spms_sub *sub, uint32_t *pos);

/** spms_sub_set_pos
 * @brief Set the current read position of the subscriber
 * @param sub The subscriber
 * @param pos The new read position
 * @return 0 on success, -1 on failure
 */
int32_t spms_sub_set_pos(spms_sub *sub, uint32_t pos);

/** Zero copy API **/

int32_t spms_pub_get_write_buf(spms_pub *ring, void **addr, size_t len);
int32_t spms_pub_flush_write_buf_with_info(spms_pub *ring, void *addr, size_t len, struct spms_msg_info *info);

int32_t spms_sub_get_read_buf(spms_sub *ring, const void **addr, size_t *len);
int32_t spms_sub_finalize_read(spms_sub *ring, int32_t ver);

#endif
