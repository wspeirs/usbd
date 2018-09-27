#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>


struct io_request_response_t {
    unsigned int type; // 1 = write; 0 = read
    size_t amount;  // < 0 for error
} request_response;


int main(int argc, char **argv)
{
    int fd, ret;
    char *buffer;
    long page_size = sysconf(_SC_PAGE_SIZE);

    fd = open("/proc/usbd_io", O_RDWR | O_SYNC);

    if(fd < 0) {
        printf("Error opening /proc/usbd_io: %d\n", fd);
        return -1;
    }

    printf("Opened /proc/usbd_io\n");

    // mmap in our file
    buffer = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    printf("mmap /proc/usbd_io: %p\n", buffer);

    // wait on a read
    ret = read(fd, &request_response, sizeof(struct io_request_response_t));

    if(ret < 0) {
        printf("Error reading: %d\n", ret);
        close(fd);
        return -1;
    }

    printf("Read %d of %lu\n", ret, sizeof(struct io_request_response_t));
    printf("Type: %u Size: %lu\n", request_response.type, request_response.amount);

    printf("Buffer: 0x%X\n", buffer[0]);

    request_response.amount = 1;

    write(fd, &request_response, sizeof(struct io_request_response_t));

    munmap(buffer, page_size);

    close(fd);
}
