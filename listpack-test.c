#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "listpack.h"

void showListpack(unsigned char *lp, int backward) {
    unsigned char *p = backward ? lpLast(lp) : lpFirst(lp);
    while(p) {
        unsigned char buf[LP_INTBUF_SIZE];
        int64_t v;
        unsigned char *ele = lpGet(p,&v,buf);
        printf("- %.*s\n", (int)v, ele);
        p = backward ? lpPrev(lp,p) : lpNext(lp,p);
    }
    printf("\n");
}

void dumpListpack(unsigned char *lp) {
    uint32_t bytes = lpBytes(lp);
    for (uint32_t j = 0; j < bytes; j++) {
        printf("%02x ", lp[j]);
    }
    printf("\n\n");
}

#define LP_SELF_TEST_MAX_ELE (1024*5) /* Make sure to stress 32 bit strings. */
unsigned long lpSelfTestRandomElement(unsigned char *ele) {
    long long half_max = LLONG_MAX / 2;
    if (rand() % 2) {
        long long max = 16, n;
        while((rand() % 6) != 0) {
            if (max > half_max) {
                max = LLONG_MAX;
                break;
            }
            max *= 2;
        }
        n = rand() % max;
        if (rand() % 2) n = -n;
        return snprintf((char*)ele,LP_SELF_TEST_MAX_ELE,"%lld",n);
    } else {
        unsigned long l = rand() % (LP_SELF_TEST_MAX_ELE+1);
        for (unsigned long i = 0; i < l; i++)
            ele[i] = 'A'+rand()%26;
        return l;
    }
}

/* Perform an iteration of the self test. The self test is performed by
 * appending, inserting and deleting a given number of items at random
 * both in a linear array and in the listpack. At the end the two structures
 * should contain the same exact sequence.
 *
 * The function returns 0 if the test fails (array and listpack content
 * are not the same), otherwise 1 is returned. */
int lpSelfTestIteration(unsigned long maxlen) {
    unsigned char **array = malloc(sizeof(unsigned char*)*maxlen);
    unsigned char *lp = lpNew();
    unsigned long curlen = 0;
    unsigned char ele[LP_SELF_TEST_MAX_ELE];
    int elelen;

    for (unsigned long i = 0; i < maxlen; i++) {
#if 0
        showListpack(lp,0);
        showListpack(lp,1);
        printf("--\n");
#endif
        elelen = lpSelfTestRandomElement(ele);
        if (curlen == 0 || rand() % 2) {
            /* Append. */
            array[curlen] = malloc(elelen+1);
            memcpy(array[curlen],ele,elelen);
            array[curlen][elelen] = '\0';
            lp = lpAppend(lp,ele,elelen);
        } else {
            /* Insert. */
            unsigned long pos = rand() % curlen;
            unsigned char *p = lpSeek(lp,pos);
            lp = lpInsert(lp,ele,elelen,p,LP_BEFORE,&p);
            memmove(array+pos+1,array+pos,sizeof(unsigned char*)*(curlen-pos));
            array[pos] = malloc(elelen+1);
            memcpy(array[pos],ele,elelen);
            array[pos][elelen] = '\0';
        }
        curlen++;

        if (rand() % 20) {
            /* TODO: Seek & delete random item. */
        }
    }

    /* Check for consistency. */
    unsigned char *p = lpFirst(lp);
    for (unsigned long i = 0; i < curlen; i++) {
        unsigned char buf[LP_INTBUF_SIZE];
        int64_t v;
        unsigned char *ele = lpGet(p,&v,buf);
        if (memcmp(ele,array[i],v) != 0) {
            printf("Element %lu mismatch:\n"
                   "LISTPACK = [%d]%.*s\n"
                   "C-STRING = [%d]%s\n",
                   i,(int)v,(int)v,ele,(int)strlen((char*)array[i]),array[i]);
            exit(1);
        }
        p = lpNext(lp,p);
    }

    /* Release allocations. */
    for (unsigned int i = 0; i < curlen; i++) {
        free(array[i]);
    }
    free(array);
    lpFree(lp);
    return 1;
}

/* Perform a self test of the specified number of iterations. */
int lpSelfTest(long long iterations) {
    for(long long i = 0; i < iterations; i++) {
        if ((i % 100) == 0) {
            printf("."); fflush(stdout);
        }
        if (lpSelfTestIteration(100) == 0)
            return 0; /* Test failed. */
    }
    return 1; /* Test passeed. */
}

int main(void) {
    unsigned char *lp = lpNew();
    unsigned char *p;

    lp = lpAppend(lp,(unsigned char*)"9",1);
    lp = lpAppend(lp,(unsigned char*)"-10",3);
    lp = lpAppend(lp,(unsigned char*)"9999",4);
    lp = lpAppend(lp,(unsigned char*)"foo",3);
    lp = lpAppend(lp,(unsigned char*)"1152921504606846976",19);
    printf("Listpack len: %d\n", (int)lpLength(lp));

    showListpack(lp,0);
    showListpack(lp,1);
    dumpListpack(lp);

    /* Add two entries before/after. */
    p = lpFirst(lp);
    p = lpNext(lp,p);
    p = lpNext(lp,p);
    lp = lpInsert(lp,(unsigned char*)"before 9999",11,p,LP_BEFORE,&p);
    p = lpNext(lp,p);
    lp = lpInsert(lp,(unsigned char*)"after 9999",10,p,LP_AFTER,&p);
    showListpack(lp,0);

    /* Remove + replace a few entries. */
    dumpListpack(lp);
    p = lpFirst(lp);
    lp = lpDelete(lp,p,NULL);
    p = lpFirst(lp);
    p = lpNext(lp,p);
    lp = lpDelete(lp,p,NULL);
    p = lpFirst(lp);
    lp = lpInsert(lp,(unsigned char*)"Hello World there was -10 here",30,p,LP_REPLACE,NULL);
    dumpListpack(lp);
    showListpack(lp,0);

    /* Seek */
    for (int i = -10; i < 10; i++) {
        unsigned char buf[LP_INTBUF_SIZE];
        int64_t v;
        unsigned char *p = lpSeek(lp,i);
        if (p) {
            unsigned char *ele = lpGet(p,&v,buf);
            printf("Seek %d: %.*s\n", i, (int)v, ele);
        } else {
            printf("Seek %d: NULL\n", i);
        }
    }

    /* Append long element. */
    unsigned char buf[407];
    memset(buf,'A',407);
    lp = lpAppend(lp,buf,407);
    dumpListpack(lp);
    showListpack(lp,0);
    lpFree(lp);

    lpSelfTest(100000);

    return 0;
}
