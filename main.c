#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

int reader(char * path, char * buf){
    int fd = open(path,0);
    if (fd == -1){
        printf("Invalid Filename or Path.");
        return NULL;
    }
    int size = read(fd, buf, 29);
    close(fd);
    return size;
}


int main(){
    char buf[30];
    int size = reader("test.txt", buf);
    buf[size] = '\0';
    printf("%s\n", buf);
}
