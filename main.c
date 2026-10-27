#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>

static int dedup_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void)fi;
    memset(stbuf, 0, sizeof(struct stat));

    // 基礎測試：只允許讀取根目錄
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }
    return -ENOENT;
}

static struct fuse_operations dedup_oper = {
    .getattr = dedup_getattr,
    // 未來在此新增 open, read, write, readdir 等函式
};

int main(int argc, char *argv[])
{
    // 啟動 FUSE daemon
    return fuse_main(argc, argv, &dedup_oper, NULL);
}