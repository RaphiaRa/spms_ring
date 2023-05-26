#ifndef SPMS_H
#define SPMS_H

#include <stddef.h>
#include <stdint.h>

/** SPMS_ERROR_AGAIN
 * @brief The ring buffer empty, try again later
 */
#define SPMS_ERROR_AGAIN -1

/** SPMS_ERROR_INVALID_POS
 * @brief The current read position is invalid, probably because it was overwritten
 */
#define SPMS_ERROR_INVALID_POS -2

/** SPMS_ERROR_INVALID_ARG
 * @brief An invalid argument was passed to a function
 */
#define SPMS_ERROR_INVALID_ARG -3

/** SPMS_ERROR_OS
 * @brief An error occurred in the underlying OS
 * @note errno will be set if this error is returned
 */
#define SPMS_ERROR_OS -4

/** SPMS_ERROR_NOT_AVAILABLE
 * @brief The requested inforation is not available
 */
#define SPMS_ERROR_NOT_AVAILABLE -5

/** SPMS_ERROR_TIMEOUT
 * @brief A timeout occurred
 */
#define SPMS_ERROR_TIMEOUT -6

/** SPMS_ERROR_INVALID_STATE
 * @brief The ring buffer is in an invalid state
 * and can't be used anymore
 */
#define SPMS_ERROR_INVALID_STATE -7

typedef struct spms_pub spms_pub;
typedef struct spms_sub spms_sub;

struct spms_config
{
    /** buf_length
     * @brief The length of the ring buffer in bytes
     * @note Must be a power of 2
     */
    size_t buf_length;

    /** msg_entries
     * @brief The number of entries in the ring buffer
     * @note Must be a power of 2
     */
    size_t msg_entries;

    /** nonblocking
     * @brief Whether the ring buffer should be nonblocking
     * @note If nonblocking is set, then reading from the ring buffer will not block when it is empty.
     */
    int8_t nonblocking;
};

/** spms_ring_needed_size
 * @brief Get the amount of memory required to create a ring buffer with the given config
 * @param config The config to use
 * @return The amount of memory required to create a ring buffer with the given config
 */
uint64_t spms_ring_mem_needed_size(struct spms_config *config);

/** spms_ring_init
 * @brief Initialize a ring buffer in the given memory region. This is done automatically by spms_pub_create, but
 * in some cases it may be useful to initialize the ring buffer earlier (For example, to avoid race conditions between spms_pub_create and spms_sub_create)
 * @param mem The memory region to initialize the ring buffer in
 * @param config The config to use
 * @return 0 on success, error code on failure
 * @note The memory region must be at least spms_mem_needed_size(config) bytes long
 * @note The memory region must be aligned to a multiple of alignof(max_align_t)
 */
int32_t spms_ring_mem_init(void *mem, struct spms_config *config);

struct spms_msg_info
{
    uint8_t is_key;
    uint64_t ts;
};

/** Constructors and destructors **/

/** spms_pub_create
 * @brief Create a publisher
 * @param ring (out) The publisher
 * @param mem The memory region to create the publisher in
 * @param config The config to use
 * @return 0 on success, error code on failure
 * Possible error codes:
 * SPMS_ERROR_INVALID_STATE: This happens if the passed memory region already
 * contains a ring buffer structure that is not in a valid state.
 * This could, for example, happen if a newly created memory region was not zeroed before use.
 * @note The memory region must be at least spms_mem_needed_size(config) bytes long
 * @note The memory region must be aligned to a multiple of alignof(max_align_t)
 */
int32_t spms_pub_create(spms_pub **ring, void *mem, struct spms_config *config);
int32_t spms_sub_create(spms_sub **ring, void *mem);
void spms_pub_free(spms_pub *ring);
void spms_sub_free(spms_sub *ring);

/** Basic read/write API **/

/** spms_pub_write_msg
 * @brief Write a message to the ring buffer, overwriting the oldest message
 * if the ring is full. On success, the message is guaranteed to be written completely.
 * @param pub The publisher to write to
 * @param addr The address of the message to write
 * @param len The length of the message to write
 * @return 0 on success, error code on failure
 */
int32_t spms_pub_write_msg(spms_pub *pub, const void *addr, size_t len, const struct spms_msg_info *info);

struct spms_ovec
{
    const void *addr;
    size_t len;
};

/** spms_pub_writev_msg
 * @brief Same as spms_pub_write_msg, but allows writing multiple buffers at once
 * @param pub The publisher to write to
 * @param ovec The buffers to write
 * @param len The number of buffers to write
 * @return 0 on success, error code on failure
 */
int32_t spms_pub_writev_msg(spms_pub *pub, struct spms_ovec *ovec, size_t len, const struct spms_msg_info *info);

/** spms_sub_read_msg
 * @brief Read a message from the ring buffer. If the ring is empty, this function will block
 * until a message is available or the timeout expires. On success, the message is guaranteed to be read completely.
 * @param sub The subscriber
 * @param addr The address to the buffer to read the message into
 * @param len (in/out) The length of the buffer addr points to (in), and the length of the message that was read (out)
 * @param timeout_ms The timeout in milliseconds.
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_read_msg(spms_sub *sub, void *addr, size_t *len, struct spms_msg_info *info, uint32_t timeout_ms);

/** spms_wait_readable
 * @brief Wait until the ring buffer is readable
 * @param sub The subscriber
 * @param timeout_ms The timeout in milliseconds.
 * @return 0 on success, error code on failure
 */
int32_t spms_wait_readable(spms_sub *sub, uint32_t timeout_ms);

/** Control API **/

int32_t spms_sub_get_dropped_count(spms_sub *sub, uint64_t *count);

/** spms_sub_rewind
 * @brief Move the read position to the latest msg in the ring
 * @param sub The subscriber to rewind
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_rewind(spms_sub *sub);

/** spms_sub_get_pos_by_ts
 * @brief Get the position of the first msg with a timestamp >= ts
 * @param sub The subscriber
 * @param pos (out) The position of the msg at or after ts
 * @param ts The timestamp to search for
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_get_pos_by_ts(spms_sub *sub, uint32_t *pos, uint64_t ts);

/** spms_sub_get_latest_ts
 * @brief Get the timestamp of the latest msg in the ring
 * @param sub The subscriber
 * @param ts (out) The timestamp of the latest msg
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_get_latest_ts(spms_sub *sub, uint64_t *ts);

/** spms_sub_get_latest_pos
 * @brief Get the position of the latest msg in the ring
 * @param sub The subscriber
 * @param pos (out) The position of the latest msg
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_get_latest_pos(spms_sub *sub, uint32_t *pos);

/** spms_sub_get_latest_key_pos
 * @brief Get the position of the latest key msg in the ring
 * @param sub The subscriber
 * @param pos (out) The position of the latest key msg
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_get_latest_key_pos(spms_sub *sub, uint32_t *pos);

/** spms_sub_get_cur_pos
 * @brief Get the current read position of the subscriber
 * @param sub The subscriber
 * @param pos (out) The current read position
 */
void spms_sub_get_cur_pos(spms_sub *sub, uint32_t *pos);

/** spms_sub_verify_cur_pos
 * @brief Verify that the current read position of the subscriber is valid
 * @param sub The subscriber
 * @return 0 on success, SPMS_ERROR_INVALID_POS if the position is invalid
 */
int32_t spms_sub_verify_cur_pos(spms_sub *sub);

/** spms_sub_get_and_verify_cur_pos
 * @brief Get the current read position of the subscriber and verify that it is valid
 * @param sub The subscriber
 * @param pos (out) The current read position
 * @return 0 on success, SPMS_ERROR_INVALID_POS if the position is invalid
 */
int32_t spms_sub_get_and_verify_cur_pos(spms_sub *sub, uint32_t *pos);

/** spms_sub_ensure_valid_cur_pos
 * @brief Ensure that the current read position of the subscriber is valid
 * @param sub The subscriber
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_ensure_valid_cur_pos(spms_sub *sub);

/** spms_sub_get_cur_ts
 * @brief Get the timestamp of the current read position of the subscriber
 * @param sub The subscriber
 * @param pos (out) The timestamp of the current read position
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_get_cur_ts(spms_sub *sub, uint64_t *pos);

/** spms_sub_next_key_pos
 * @brief Get the position of the next key msg after the current read position
 * @param sub The subscriber
 * @param pos (out) The position of the next key msg
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_get_next_key_pos(spms_sub *sub, uint32_t *pos);

/** spms_sub_get_ts_bys_pos
 * @brief Get the timestamp of the msg at position pos
 * @param sub The subscriber
 * @param ts (out) The timestamp of the msg at position pos
 * @param pos The position of the msg
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_get_ts_by_pos(spms_sub *sub, uint64_t *ts, uint32_t pos);

/** spms_sub_set_pos
 * @brief Set the current read position of the subscriber
 * @param sub The subscriber
 * @param pos The new read position
 * @return 0 on success, error code on failure
 */
int32_t spms_sub_set_pos(spms_sub *sub, uint32_t pos);

/** Zero copy API **/

int32_t spms_pub_get_write_buf(spms_pub *ring, void **addr, size_t len);
int32_t spms_pub_flush_write_buf(spms_pub *ring, void *addr, size_t len, const struct spms_msg_info *info);

int32_t spms_sub_get_read_buf(spms_sub *ring, const void **addr, size_t *len, struct spms_msg_info *info, uint32_t timeout_ms);
int32_t spms_sub_finalize_read(spms_sub *ring);

#endif
