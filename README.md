# spms_ring
## Single Publisher Multiple Subscribers Ring Buffer written in C11

### Installation
Simply copy `spms.h` and `spms.c` into your project and include `spms.h` in your source files.


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
