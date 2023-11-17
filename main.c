#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

int reader(char * path, char * buf, off_t offset){
    int fd = open(path,0);
    if (fd == -1){
        printf("Invalid Filename or Path.");
        return -1;
    }
    lseek(fd,offset,SEEK_CUR);
    int size = read(fd, buf, 29);
    if (size == -1){
        printf("Invalid file.");
        close(fd);
        return -1;
    }
    close(fd);
    return size;
}


int main(){
    char buf[30];
    int size = reader("test.txt", buf,0);
    buf[size] = '\0';
    printf("%s\n", buf);
}
