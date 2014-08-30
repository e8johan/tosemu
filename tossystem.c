#include <stdio.h>

#include "tossystem.h"

#pragma pack(push,2)
struct exec_header {
    uint16_t magic;
    uint32_t tsize, 
             dsize, 
             bsize, 
             ssize;
    uint32_t res;
    uint32_t flags;
    uint16_t absflag;
};
#pragma pack(pop)

uint16_t endianize_16(uint16_t in)
{
    uint16_t out;
    int i;
    
    for(i=0; i<2; ++i)
    {
        out = out << 8;
        out = out | (0xff&in);
        in = in >> 8;
    }
    
    return out;
}

uint32_t endianize_32(uint32_t in)
{
    uint32_t out;
    int i;
    
    for(i=0; i<4; ++i)
    {
        out = out << 8;
        out = out | (0xff&in);
        in = in >> 8;
    }
    
    return out;
}

int init_tos_environment(struct tos_environment *te, void *binary, uint64_t size)
{
    struct exec_header *header;
    
    te->size = size;
    te->binary = binary;
    
    if (size < sizeof(struct exec_header))
    {
        printf("Error: Too small binary\n");
        return -1;
    }
    
    header = (struct exec_header*)binary;
    te->tsize = endianize_32(header->tsize);
    te->dsize = endianize_32(header->dsize); 
    te->bsize = endianize_32(header->bsize); 
    te->ssize = endianize_32(header->ssize);
    
    printf("TEXT: 0x%x\nDATA: 0x%x\nBSS:  0x%x\nSYMS: 0x%x\n",
           te->tsize,
           te->dsize,
           te->bsize,
           te->ssize);
    
    return 0;
}


unsigned int  m68k_read_disassembler_8(unsigned int address)
{
    return 0;
}
unsigned int  m68k_read_disassembler_16(unsigned int address)
{
    return 0;
}
unsigned int  m68k_read_disassembler_32(unsigned int address)
{
    return 0;
}

unsigned int  m68k_read_memory_8(unsigned int address)
{
    return 0;
}
unsigned int  m68k_read_memory_16(unsigned int address)
{
    return 0;
}
unsigned int  m68k_read_memory_32(unsigned int address)
{
    return 0;
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    return;
}
void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    return;
}
void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    return;
}
