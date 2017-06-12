#ifndef DISTSIM_MEM_WRAP_HPP
#define DISTSIM_MEM_WRAP_HPP
#include <stdio.h>
#include <strings.h>
#include <sys/mman.h>
#ifdef __cplusplus
extern "C" {
#endif

extern void *__libc_malloc(size_t size);
extern void *__libc_realloc(void* ptr, size_t size);

void* malloc(size_t size) {
    void* ret =  __libc_malloc(size);
    madvise(ret, size, MADV_MERGEABLE);
    return ret;
}

void* realloc(void* ptr, size_t size) {
    void* ret =  __libc_realloc(ptr, size);
    madvise(ret, size, MADV_MERGEABLE);
    return ret;
}

void* calloc(size_t nmemb, size_t size) {
    void* ret = malloc(nmemb*size);
    bzero(ret, nmemb*size);
    return ret;
}

#ifdef __cplusplus
}
#endif
#endif
