#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>



int main(int argc, const char *const *argv) {
    if(argc < 2){
        fprintf(stderr, "Error: incorrect input\n");
        return 1;
    }
    int fd = open(argv[1], O_RDONLY);
    if(fd == -1){
        fprintf(stderr, "Error: unable to open file\n");
        return 1;
    }

    // Get fd information
    struct stat sb;
    if(fstat(fd, &sb) == -1){
        fprintf(stderr, "Error: unable to access file\n");
        return 1;
    }
    // Map file into memory
    char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED){
        fprintf(stderr, "Error: unable to read file");
        close(fd);
        return 1;
    }
    // Close input file descriptor after mmaping to memory
    close(fd);

    for(size_t i = 0; i < (size_t)sb.st_size; i++){
        printf("at: %c\n", addr[i]);
    }

    if(munmap(addr, sb.st_size) == -1){
        fprintf(stderr, "Error: unable to unmap file");
        return 1;
    }
    
    


    return 0;
}
