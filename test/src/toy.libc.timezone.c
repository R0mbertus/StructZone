#include <stdio.h>
#include <sys/time.h>
#include <time.h>

int main(){
    struct timeval tv;

    int time = gettimeofday(&tv, NULL);
    printf("the number of seconds passed since the epoch is: %x \n", tv.tv_sec);
}