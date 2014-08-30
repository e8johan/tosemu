/* - In your host program, be sure to call m68k_pulse_reset() once before calling
  any of the other functions as this initializes the core.

- Use m68k_execute() to execute instructions and m68k_set_irq() to cause an
  interrupt.

  - In m68kconf.h, set M68K_EMULATE_INT_ACK to OPT_SPECIFY_HANDLER

- In m68kconf.h, set M68K_INT_ACK_CALLBACK(A) to your interrupt acknowledge
  routine

- Your interrupt acknowledge routine must return an interrupt vector,
  M68K_INT_ACK_AUTOVECTOR, or M68K_INT_ACK_SPURIOUS.  most m68k
  implementations just use autovectored interrupts.

- When the interrupting device is satisfied, you must call m68k_set_irq(0) to
  remove the interrupt request.

  - In your host program, call m68k_set_cpu_type() and then call
  m68k_pulse_reset().  Valid CPU types are:
    M68K_CPU_TYPE_68000,
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
    
#include "tossystem.h"
    
int main(int argc, char **argv)
{
    int binary_file;
    void *binary_data;
    struct stat sb;
    struct tos_environment te;
    
    if(argc != 2)
    {
        printf("Usage: tosemu <binary>\n\n\t<binary> name of binary to execute\n");
        return -1;
    }

    binary_file = open(argv[1], O_RDONLY);
    if (binary_file == -1)
    {
        printf("Error: failed to open '%s'\n", argv[1]);
        return -1;
    }
    
    fstat(binary_file, &sb);
    
    binary_data = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, binary_file, 0);
    if (!binary_data)
    {
        printf("Error: failed to mmap '%s'\n", argv[1]);
        close(binary_file);
        return -1;
    }
    
    if( ((char*)binary_data)[0] != 0x60 && ((char*)binary_data)[1] == 0x1a)
    {
        printf("Error: invalid magic in '%s'\n", argv[1]);
        close(binary_file);
        return -1;
    }
    
    if (init_tos_environment(&te, binary_data, sb.st_size))
    {
        printf("Error: failed to initialize TOS environment\n");
        close(binary_file);
        return -1;
    }
    
    close(binary_file);
    
    return 0; 
}
