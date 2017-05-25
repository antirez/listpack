Listpack: work in progress implementation
===

This repository is a work in progress, currently not usable, implementation
of a data structure called *listpack*, suitable to store lists of string
elements in a representation which is space efficient and that can be
efficiently accessed from left to right and from right to left.

You can find the specification in the `listpack.md` file in this repository.

The `listpack.c` file implemented the full specification, however since
Redis Conf 2017 is near, I'll pause the work for some time to focus on
talks & training day material. The implementation lacks a complete fuzz testing
suite like the one of [rax](https://github.com/antirez/rax), the functions
to validate a listpack load from an untrusted source, and good API
documentation (even if `listpack.c` top functions comment is already a good
starting point).

The code is almost completely untested, so will likely explode while you
are reading this README file.

API TODO
===

    unsigned char *lpSeek(unsigned char *lp, long index);
    int lpIsValid(unsigned char *lp, uint32_t len);
    int lpSelfTest(void);
