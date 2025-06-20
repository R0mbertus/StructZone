#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>

#define FILE_PATH "/etc/hostname"

int main() {
    // Note: we very explicitly do not use struct initializer
    struct stat stat_buf;
    stat(FILE_PATH, &stat_buf);
    printf("statting /etc/hostname \n");
    printf("st_dev : %ld \n", stat_buf.st_dev);
    printf("st_ino : %ld \n", stat_buf.st_ino);
    printf("st_mode : %d \n", stat_buf.st_mode);
    assert(stat_buf.st_mode == 33188);
    printf("st_nlink : %ld \n", stat_buf.st_nlink);
    assert(stat_buf.st_nlink == 1);
    printf("st_uid : %d \n", stat_buf.st_uid);
    assert(stat_buf.st_uid == 0);
    printf("st_gid : %d \n", stat_buf.st_gid);
    assert(stat_buf.st_gid == 0);
    printf("st_rdev : %ld \n", stat_buf.st_rdev);
    printf("st_size : %ld \n", stat_buf.st_size);
    assert(stat_buf.st_size == 1);
    printf("st_blksize : %ld \n", stat_buf.st_blksize);
    assert(stat_buf.st_size == 4096);
    printf("st_blocks : %ld \n", stat_buf.st_blocks);
    assert(stat_buf.st_blocks == 8);
    return 0;
}
