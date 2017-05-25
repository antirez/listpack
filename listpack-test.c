#include <stdio.h>
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

    return 0;
}
