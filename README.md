# spms_ring
## Single Publisher Multiple Subscribers Ring Buffer

### Installation
Simply copy `spms.h` and `spms.c` into your project and include `spms.h` in your source files.


### Examples
Compile the example with
```sh
gcc -o example example.c spms.c
```
Run the publisher
```sh
./example pub
```

Run the subscriber in one or multiple other terminals
```sh
./example sub
```
