# spms_ring
## Single Publisher Multiple Subscribers Ring Buffer written in C11

### Description
`spms_ring` is a single publisher multiple subscriber ring buffer suitable for shared memory.
The publisher writes messages to the ring without being aware of the subscribers.
When a subscriber reads a message, it is not removed from the ring and still can
be read by other subscribers. If a subscriber is not fast enough to read the messages, it will miss some of them.

Besides the usual read and write functions, I wanted the ring to have the following features:
- Timestamps can be added to the messages and subscribers can read from a specific timestamp on.
- Messages can be marked as key messages (Like key frames in video streams) and
subscribers can jump to the next key message.
- A zero copy API.
- Optionally letting the subscriber wait for new messages if the ring is empty.

### Installation
Simply copy `spms.h` and `spms.c` into your project and include `spms.h` in your source files.

### Basic Usage

Setup the memory for the ring buffer and the publisher
```c
struct spms_config config = {0};
config.buf_length = 1024 * 1024; // buffer size
config.msg_entries = 1024;       // max number of messages in the buffer
buffer = calloc(1, spms_ring_mem_needed_size(&config));
spms_pub *pub = NULL;
spms_pub_create(&pub, buffer, &config);
```
Write data to the ring
```c
spms_pub_write_msg(pub, data, data_len, NULL);
```

Setup the subscriber
```c
spms_sub *sub = NULL;
spms_sub_create(&sub, buffer);
```

Read data from the ring
```c
uint8_t data[1024];
spms_sub_read_msg(sub, data, sizeof(data), NULL, 0);
```

### Examples
Compile the example with
```sh
gcc -std=c11 -o example example.c spms.c
```
**Note:** On Linux you need to add `-lrt` to the command above.
Run the publisher
```sh
./example pub
```

Run the subscriber in one or multiple other terminals
```sh
./example sub
```
