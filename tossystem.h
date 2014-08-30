#ifndef TOSSYSTEM_H
#define TOSSYSTEM_H

#include <stdint.h>

struct tos_environment {
    uint64_t size;
    void *binary;
    
    uint32_t tsize, 
             dsize, 
             bsize, 
             ssize;

};

int init_tos_environment(struct tos_environment *te, void *binary, uint64_t binary_size);

#endif /* TOSSYSTEM_H */
