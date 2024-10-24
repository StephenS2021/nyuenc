#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>


#define BUFFER_SIZE 256
#define BYTES_PER_LINE 16

void encode(struct stat sb, char *addr, char *buffer, char *cur, int *len, unsigned int *count){
    for(size_t i = 0; i < (size_t)sb.st_size; i++){
        if(i==0){
            // If the beginning of the file, set first char and add offset to buffer in hexadexcimal
            *cur = addr[i];
            *count += 1;
        }else if(*cur != addr[i]){
            *len = snprintf(buffer, sizeof(buffer)-*len, "%c%c", (unsigned char) *cur, *count);
            write(STDOUT_FILENO, buffer, *len);
            *cur = addr[i];
            *count = 1;
        }else{
            *count +=1;
        }
    }

}

int main(int argc, const char *const *argv) {
    char cur = '\0';
    unsigned int count = 0;
    char buffer[BUFFER_SIZE];
    int len = 0;
    if(argc < 2){
        fprintf(stderr, "Error: incorrect input\n");
        return 1;
    }
    for(int i = 1; i < argc; i++){
        int fd = open(argv[i], O_RDONLY);
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
        
        encode(sb, addr, buffer, &cur, &len, &count);
        

        if(munmap(addr, sb.st_size) == -1){
            fprintf(stderr, "Error: unable to unmap file");
            return 1;
        }
    }
    len = snprintf(buffer, sizeof(buffer)-len, "%c%c", (unsigned char) cur, count);
    write(STDOUT_FILENO, buffer, len);

    // char cur = '\0';
    // int count = 0;
    // int char_count = 0;
    // char char_list[16];
    // int char_list_len = 0;
    // char buffer[BUFFER_SIZE];
    // int len = 0;
    // for(size_t i = 0; i < (size_t)sb.st_size; i++){
    //     if(i==0){
    //         // If the beginning of the file, set first char and add offset to buffer in hexadexcimal
    //         len += snprintf(buffer+len, sizeof(buffer)-len, "%08x: ", 0);
    //         char_list_len += snprintf(char_list+char_list_len, sizeof(char_list)-char_list_len, "%c", addr[i]);
    //         cur = addr[i];
    //         char_count+=1;
    //         count = 1;
    //     }
    //     else if(cur != addr[i]){
    //         // Add the hex char and count of the last chars
    //         len += snprintf(buffer+len, sizeof(buffer)-len, "%02x%02d ", (unsigned char) cur, count);
    //         // Set the new string and count
    //         cur = addr[i];
    //         count = 1;
    //         char_count+=1;
    //         // If not the last char of the line, add new char to letter list
    //         if(char_count % 9 != 0){
    //             char_list_len += snprintf(char_list + char_list_len, sizeof(char_list)-char_list_len, ".%c", addr[i]);
    //         }
    //     }else{
    //         count+=1;
    //     }
    //     // If 8 characters are set, add them to the new line
    //     if(char_count % 9 == 0){
    //         // add the characters to the end of the last line
    //         len += snprintf(buffer+len, sizeof(buffer)-len, "%s", char_list);
    //         // Write the current buffer to stdout and reset the buffer
    //         write(STDOUT_FILENO, buffer, len);
    //         memset(buffer, 0, sizeof(buffer));
    //         len = 0;
    //         // reset the character list
    //         memset(char_list, 0, sizeof(char_list));
    //         char_list_len = 0;
    //         // Add the first char of the new char list
    //         char_list_len += snprintf(char_list + char_list_len, sizeof(char_list)-char_list_len, "%c", addr[i]);
    //         // create newline and set the address
    //         len += snprintf(buffer+len, sizeof(buffer)-len, "\n%08x: ", (char_count/8)*16);
    //     }
    // }
    // // print the last group of characters
    // len += snprintf(buffer+len, sizeof(buffer)-len, "%02x%02d ", (unsigned char) cur, count);
    
    // // Fill in blank spaces until end of chars
    // while(char_count % 8 != 0){
    //     len += snprintf(buffer+len, sizeof(buffer)-len, "    ");
    //     char_count += 1;
    // }
    // // add the characters at the end
    // len += snprintf(buffer+len, sizeof(buffer)-len, " %s\n", char_list);
    // write(STDOUT_FILENO, buffer, len);

    // for(size_t offset = 0; offset < (size_t)sb.st_size; offset+=BYTES_PER_LINE){
    //     char buffer[BUFFER_SIZE];
    //     int len = snprintf(buffer, sizeof(buffer), "%08zx: ", offset);
    //     for(size_t i = 0; i < BYTES_PER_LINE; i++){
    //         // If there are still characters left to read
    //         if(offset+i < (size_t)sb.st_size){
    //             len += snprintf(buffer+len, sizeof(buffer)-len, "%02x ", addr[offset + i]);
    //         }else{
    //             len += snprintf(buffer+len, sizeof(buffer)-len, "   ");
    //         }
    //     }

    //     write(STDOUT_FILENO, buffer, len);
    //     write(STDOUT_FILENO, "\n", 1);

    // }

    


    return 0;
}
