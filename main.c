#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <limits.h>

// 定義底層的真實目錄（未來會指向你的 F2FS 掛載點）
#define BACKING_DIR "/tmp/f2fs_lower"

// 輔助函式：將 FUSE 掛載點的路徑轉換為底層真實路徑
static void get_full_path(char *fpath, const char *path)
{
    strcpy(fpath, BACKING_DIR);
    // 避免路徑結尾多出斜線
    if (strcmp(path, "/") != 0)
    {
        strncat(fpath, path, PATH_MAX - strlen(BACKING_DIR) - 1);
    }
}

// 取得檔案屬性
static int dedup_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void)fi;
    int res;
    char fpath[PATH_MAX];

    get_full_path(fpath, path);

    // 使用系統的 lstat 讀取真實檔案屬性
    res = lstat(fpath, stbuf);
    if (res == -1)
    {
        return -errno;
    }
    return 0;
}

// 讀取目錄內容
static int dedup_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    DIR *dp;
    struct dirent *de;
    char fpath[PATH_MAX];

    get_full_path(fpath, path);

    dp = opendir(fpath);
    if (dp == NULL)
    {
        return -errno;
    }

    // 逐一讀取真實目錄下的檔案並填入 FUSE 緩衝區
    while ((de = readdir(dp)) != NULL)
    {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;

        if (filler(buf, de->d_name, &st, 0, 0))
            break;
    }

    closedir(dp);
    return 0;
}

static struct fuse_operations dedup_oper = {
    .getattr = dedup_getattr,
    .readdir = dedup_readdir,
};

int main(int argc, char *argv[])
{
    // 確保底層目錄存在 (測試用)
    // 實務上也可以透過系統指令 mkdir 先建立好
    char command[PATH_MAX + 15];
    snprintf(command, sizeof(command), "mkdir -p %s", BACKING_DIR);
    system(command);

    return fuse_main(argc, argv, &dedup_oper, NULL);
}