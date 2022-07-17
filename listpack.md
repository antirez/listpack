Listpack specification
===

    Version 1.0, 1 Feb 2017: Intial specification.

    Version 1.1, 2 Feb 2017: Integer encoding simplified. Appendix A added.

    Version 1.2, 3 Feb 2017: Better specify the meaning of the num-elements
                             field with value of 65535. The two 12 bits
                             positive/negative integers encodings were
                             replaced by a single 13 bit signed integer.
                             Crash resistance better specified.
                             (Thanks to Oran Agra for all the hints).

    Salvatore Sanfilippo
    Yuval Inbar
    Oran Agra

Since the early stage of Redis development, to optimize for low memory usage was
an important concern. Scalable data structures are often composed of nodes
(heap allocated chunks of memory) containing references (pointers) to other
nodes. This representation, while able to scale well with the number of elements
in a data structure, is extremely wasteful: meta data easily account for 50% of
the space in memory if the average element size is small. However when a data
structure is used to hold a very small number of elements, it is possible
to switch to a different, more compact representation. Because the number of
elements for which the alternating representation is used is constant and small,
the time complexity of the data structure is the same. Moreover, the constant
times of working with such compact representation of a small number of elements,
even when a full scan of the elements is needed in order to access or modify the
data structure, are well compensated by the cache locality of sequentially
accessing a linear array of bytes. This allows to save memory, while
transparently switching to a linked representation once a given maximum size
is reached.

Traditionally Redis, as compact representation of hashes, lists, and sorted sets
having few elements, used a data structure called *ziplist*. A ziplist is
basically a single heap allocated chunk of memory containing a list of string
elements. It can be used to represent maps by alternating keys and values, or
ordered lists of elements. The ziplist data structure served us very well for
years, however recently an user signaled a crash in the context of accessing
ziplists. The bug happened with error corrected memory modules of the latest
generation, and RDB files are protected by a CRC64 checksum. So we started an
investigation in order to discover for bugs in the `ziplist.c` file.

After weeks of work, I (Salvatore) analytically discovered a bug that is not
related to the user crash. Oran Agra and Yuval Inbar, that are also contributors
of this specification, joined the effort of auditing the code. Salvatore also
wrote several fuzz testers modeling the layout of the user data. Even if the
fuzzing techniques used could easily find the very complex to replicate bug
that was found analytically, no crash was ever seen using the Hash data type,
the one used during the crash reported by the user.

Even if apparently `ziplist.c` does not contain bugs, or at least we cannot find
them, nor there are often reports of crashes related to a potential bug in this
part of the code, during the review all the programmers involved agreed that
the ziplist code was so complex and had such non trivial side effects that it
was a wise idea to switch to something else. The reason why it was hard to
audit was that a ziplist has the following layout:

    <header> <entry> <entry> ... <entry> <end-of-ziplist>

However it is important for Redis that a ziplist can be accessed backward, from
the latest element to the first, in order to model commands such as `LRANGE`
in a way to avoid scanning the whole ziplist to just fetch a few elements from
the tail. So each entry, was actually composed of the following two parts:

    <previous-entry-length> <entry-data>

The previous entry length could change in size from 1 to 5 bytes, in order to
use little space to encode small previous entries lengths. However inserting
or deleting elements in the middle, while using this particular encoding, may
have a cascading effect, where the previous length and even the number of bytes
the previous length is encoded, may change and may *cascade* to the next
elements. This was the main source of complexity of ziplist. However other
alternatives implementations exist that can prevent this problem and only
use local information for each entry.

Listpack takes the good ideas of ziplists and reimplement it in order to create
a more compact, faster to parse implementation, that maps directly to simple
to audit and understand code. Moreover the single entries representation in
listpack were redesigned in order to better exploit the kind of data Redis
users normally store in lists and hashes. This document describes the new format.

General structure
===

A listpack is encoded into a single linear chunk of memory. It has a fixed
length header of six bytes (instead of ten bytes of ziplist, since we no
longer need a pointer to the start of the last element). The header is
followed by the listpack elements. In theory the data structure does not need
any terminator, however for certain concerns, a special entry marking the
end of the listpack is provided, in the form of a single byte with value
FF (255). The main advantages of the terminator are the ability to scan the
listpack without holding (and comparing at each iteration) the address of
the end of the listpack, and to recognize easily if a listpack is well
formed or truncated. These advantages are, in the idea of the writer, worth
the additional byte needed in the representation.

    <tot-bytes> <num-elements> <element-1> ... <element-N> <listpack-end-byte>

The six byte header, composed of the tot-bytes and num-elements fields is
encoded in the following way:

* `tot-bytes`: 32 bit unsigned integer holding the total amount of bytes
representing the listpack. Including the header itself and the terminator.
This basically is the total size of the allocation needed to hold the listpack
and allows to jump at the end in order to scan the listpack in reverse order,
from the last to the first element, when needed.
* `num-elements`:  16 bit unsigned integer holding the total number of elements
the listpack holds. However if this field is set to 65535, which is the greatest
unsigned integer representable in 16 bit, it means that the number of listpack
elements is not known, so a LIST-LENGTH operation will require to fully scan
the listpack. This happens when, at some point, the listpack has a number of
elements equal or greater than 65535. The num-elements field will be set again
to a lower number the first time a LIST-LENGTH operation detects the elements
count returned in the representable range.

All integers in the listpack are stored in little endian format, if not
otherwise specified (certain special encodings are in big endian because
it is more natural to represent them in this way for the way the specification
maps to C code).

Elements representation
===

Each element in a listpack has the following structure:

    <encoding-type><element-data><element-tot-len>
    |                                            |
    +--------------------------------------------+
                (This is an element)

The element type and element total length are always present. The element
data itself sometimes is missing, since certain small elements are directly
represented inside the spare bits of the encoding type.

The encoding type is basically useful to understand what kind of data follows
since strings can be encoded as little endian integers, and strings can have
multiple string length fields bits in order to reduce space usage.
The element data is the data itself, like an integer or an array of bytes
representing a string. Finally the element total length, is used in order to
traverse the list backward from the end of the listpack to its head, and
is needed since otherwise there is no unique way to parse the entry from
right to left, so we need to be able to jump to the left of the specified
amount of bytes.

Each element can always be parsed left-to-right. The first two bits of the
first byte select the encoding. There are a total of 3 possibilities. The
first two encodings represents small strings. The third encoding instead is
used in order to specify other sub-encodings.

Small numbers
---

Strings that can be represented as small numbers, such as "65" or "1" are a
very common, so they have a special encoding that allows to specify such
strings representing numbers from 0 to 127 as a single byte:

    0|xxxxxxx

Where `xxxxxxx` is a 7 bit unsigned integer. We can test for this encoding
just checking that the most significant bit of the first byte of the
entry is zero.

A few examples:

    "\x03" -- The string "3"
    "\x12" -- The string "18"

Tiny strings
---

Small strings are also very common elements inside objects represented inside
Redis collections, so the overhead to specify their length is just a single
byte:

    10|xxxxxx <string-data>

This encoding represents strings up to 63 characters in length, since `xxxxxx`
is a 6 bit unsigned integer. The string data is the byte by byte string itself,
and may be missing in the special case of the empty string.

A few examples:

    "\x80" -- The empty string
    "\x85hello" -- The string "hello"

Multi byte encodings
---

If the most significant two bits of the first byte are both set, then the
remaining bits select one of the following sub encodings.

The first three sub encodings happen when the first two bits are both "11" but
the following bits are never "11".

    110|xxxxx yyyyyyyy -- 13 bit signed integer
    1110|xxxx yyyyyyyy -- string with length up to 4095

In this encoding, `xxxx|yyyyyyyy` represent an unsigned integer where `xxxx`
are the most significant bits and `yyyyyyyy` are the least significant bits.

Finally, when the first four bits are all set, the following sub encodings
represented by the remaining four bits are defined:

    1111|0000 <4 bytes len> <large string>
    1111|0001 <16 bits signed integer>
    1111|0010 <24 bits signed integer>
    1111|0011 <32 bits signed integer>
    1111|0100 <64 bits signed integer>
    1111|0101 to 1111|1110 are currently not used.
    1111|1111 End of listpack

Element total length field
---

As already specified, the last part of an entry is a representation of its own
size, so that the listpack can be traversed from right to left. This field
has a variable length, so that we use just a single byte for it if the length
of the field is small, and progressively use more bytes for bigger entries.
The total length field is designed to be parsed from right to left, since this
is how we use it, and cannot be parsed the other way around, from left to
right. However, when we parse the entry from left to right, we already know its
length at the time we need to parse the total length field, so we can also
compute how much bytes are needed in order to represent its total length
field using the variable encoding. This allows to just skip this amount of bytes
without attempting to parse it. We'll make it more clear with examples later in
this section.

The variable length is stored from right to left, and the most significant bit
of each byte is used in order to signal if there are more bytes. This means that
we use only 7 bits in every byte. A entry length smaller than 128 can just be
encoded as an 8-bit unsigned integer having the entry value.

    "\x20" -- 32 bytes entry length

However if I want to encode a entry length with the value of, for example, 500,
two bytes will be required. The binary representation of 500 is the following:

    111110100

We can split the representation in two 7-bit halves:

    0000011 1110100

Note that, since we parse the entry length from right to left, the entry is
stored in big endian (but it's not vanilla big endian since only 7 bits are
used and the 8th bit is used to signal the *more bytes* condition).

However we need to also add the bit to signal if there are more bytes, so
the final representation will be:

    [0]0000011          [1]1110100
     |                   |
     `- no more bytes    `- more bytes to the left!

The actual encoding will be:

    "\xf4\x03" -- 500 bytes entry length

Let's take for example a very simple entry encoding the string "hello":

    "\x45hello" -- The string "hello"

The raw entry is 6 bytes: the encoding byte followed by the raw data.
In order for the entry to be complete, we need to add the entry length field
at the end, that in this case is just the byte "06". So the final complete
entry will be:

    "\x45hello\x06" -- A complete entry representing "hello"

Note that we can easily parse the entry from right to left, by reading the
length of 6, and jumping 6 bytes on the left to reach the start of the entry,
but we can also parse the entry from left to right, since after we parsed
the entry data of six bytes, we know how much bytes are used in order to
encode its length by using the following table:

    From 0 to 127: 1 byte
    From 128 to 16383: 2 bytes
    From 16383 to 2097151: 3 bytes
    From 2097151 to 268435455: 4 bytes
    From 268435455 to 34359738367: 5 bytes

No entry can be longer than 34359738367 bytes.

Implementation requirements
===

The wish list about the implementation is, with points in decreasing order of
importance, the following:

1. Crash resistant against wrong encodings. This was not the case with ziplist
implementation.
2. Understandable and easily auditable. Well commented code.
3. Fast. Avoid unnecessary copying. For instance, when adding to head, detect if
realloc() is a non-OP (when advanced malloc functionalities are available) and
instead use malloc() and avoid a copy of the data by copying directly at the
right offset.
4. Availability of an update-element operation, so that if an element is updated
with one of the same size (very common with Hashes, think HINCRBY) there is
no memory copying involved.

Notes about understandability:

Note that understandability cannot be obtained without simplicity of the design,
however the design outlined in this document is thought to have a
straightforward translation to a simple and robust implementation.

Notes about crash resistance:

It is worth noting that crash resistance has limitations: for example a corrupted
listpack header may make the program jump to invalid addresses. In this context
for crash resistance we mean that as long as the corruption does not force the
program to jump to illegal addresses, wrong encodings are detected when possible
(that is, when the corruption does not happen to map to valid entries).
For instance a wrong string length will be detected every time the amount of
remaining bytes in the listpack is not compatible with the announced string
length. The API should always be able to report such errors instead of crashing
the program.

Credits
===

This specification was written by Salvatore Sanfilippo. Oran Agra and Yuval
Inbar, together with the author of this spec analyzed the ziplist implementation
in order to search for bugs and to understand how the specification could be
improved.

Yuval provided the idea of allowing backward traversal by using
only information which is local to the entry (the entry length at the end
of the entry itself) instead of global informations (such as the length
of the previous entry, as it was in ziplist).

Yuval also suggested to use a progressive length integer for the back length.

Oran provided ideas about the optimization of the implementation.


APPENDIX A: potential optimizations not exploited
===

There are certain improvements that we left out of this specification in
order to enhance the simplicity of this data structure.

Different encodings for positive and negative integers.
---

In theory it is possible to better exploit the fact we have free additional
encoding type bits, in order to distinguish between positive and negative
integers and always represent them as unsigned. In this way we could improve the
range of the integers we can represent with a given number of bytes. A former
version of this specification used an encoding like the following:

    1111|0001 <16 bits unsigned integer>
    1111|0010 <16 bits negative integer>
    1111|0011 <24 bits unsigned integer>
    1111|0100 <24 bits negative integer>
    1111|0101 <32 bits unsigned integer>
    1111|0110 <32 bits negative integer>
    1111|0111 <64 bits unsigned integer>
    1111|1000 <64 bits negative integer>

However at a second thought this was believed to make the implementation more
complex and potentially slower, so the slightly less efficient representation of
storing signed integers was chosen instead.

Packed characters
---

Many element in a listpack, notably hash field names representing objects inside
Redis, are going to use a subset of characters in the range `A-z`. Examples
are strings such as `name`, `suername` and so forth.

Using six bits per character it is possible to represent the alphabet consisting
of all the lower and upper case letters, the numbers from 0 to 9, and a few more
chars like `-`, `_`, `.`. So an additional encoding representing strings using
six bits per character could be added in order to improve the space efficiency
of strings considerably.

This was not added mainly for performance considerations, since the complexity
added is believed to be manageable and not a likely source of potential bugs.

Skip index
---

Accessing far elements in a long listpack is O(N), so it looks natural to add
some way in order to speedup this kind of lookups with skip tables. While this
is usually a great idea for rarely changing packed representations of data,
listpacks are going to be used in situations where data is often changed in the
middle (Redis Hash and List data types both stress this usage pattern).

Updating the skip indexes could be error prone and even costly, and with the
default settings Redis only uses relatively small listpacks where the access
locality well compensates the need for scanning.

When a memory saving representation is needed, with the ability to scale to
many elements, the author believes that a linked data structure where listpacks
are used as nodes is the preferred approach: it improves separation of concerns
between the two representations and may be simpler to manage. In this regard
listpacks are very friendly because they can be split and merged easily with
linear copies without offsets adjustments.
