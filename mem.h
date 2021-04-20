#ifndef MEM_H
#define MEM_H
#include <stdlib.h>
#ifdef MEM_DEBUG
#include <stdio.h>
    #define FREE(P) fprintf(stderr,"F\t%p\t%s\t%d\n",(P),__FILE__,__LINE__),free((P))
    #define MALLOC(P,C,S) P=(C*)malloc((S)),fprintf(stderr,"A\t%p\t%s\t%d\n",(P),__FILE__,__LINE__)
    #define REALLOC(P,C,S,O) P=(C*)realloc((O),(S)),((P)==O)?:fprintf(stderr,"F\t%p\t%s\t%d\nA\t%p\t%s\t%d\n",(O),__FILE__,__LINE__,(P),__FILE__,__LINE__)
#else
    #define FREE(P) free((P))
    #define MALLOC(P,C,S) P=(C*)malloc((S))
    #define REALLOC(P,C,S,O) P=(C*)realloc((O),(S))
#endif
#endif /* MEM_H */
