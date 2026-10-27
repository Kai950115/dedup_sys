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
#include <openssl/sha.h>

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

// 輔助函式：計算 SHA-256 並將結果轉為 64 字元的 16 進位字串
static void calculate_sha256(const char *data, size_t len, char outputBuffer[65])
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;

    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, len);
    SHA256_Final(hash, &sha256);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    }
    outputBuffer[64] = 0; // 字串結尾
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

// 開啟檔案
static int dedup_open(const char *path, struct fuse_file_info *fi)
{
    char fpath[PATH_MAX];
    get_full_path(fpath, path);

    // 使用系統的 open 函式
    int res = open(fpath, fi->flags);
    if (res == -1)
    {
        return -errno;
    }
    // 將系統回傳的 file descriptor 存起來，給後續的 read/write 使用
    fi->fh = res;
    return 0;
}

// 讀取檔案
static int dedup_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void)path;
    // 使用 pread 根據指定的 offset 讀取資料
    int res = pread(fi->fh, buf, size, offset);
    if (res == -1)
    {
        res = -errno;
    }
    return res;
}

// 寫入檔案
static int dedup_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void)path;

    // --- 【去重攔截測試】 ---
    char hash_str[65];
    calculate_sha256(buf, size, hash_str);
    // 因為 FUSE 是在前景執行 (-f)，printf 會直接印在第一台終端機上
    printf("[DEDUP] 攔截到寫入請求! 大小: %zu bytes, Hash 指紋: %s\n", size, hash_str);
    // ------------------------

    // 目前先保留原本的寫入機制 (Passthrough)
    int res = pwrite(fi->fh, buf, size, offset);
    if (res == -1)
    {
        res = -errno;
    }
    return res;
}

// 建立新檔案
static int dedup_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    char fpath[PATH_MAX];
    get_full_path(fpath, path);

    // 呼叫系統 open 並帶入建立權限 (mode)
    int res = open(fpath, fi->flags, mode);
    if (res == -1)
    {
        return -errno;
    }
    fi->fh = res;
    return 0;
}

// 關閉檔案
static int dedup_release(const char *path, struct fuse_file_info *fi)
{
    (void)path;
    // 關閉在 open 或 create 階段取得的 file descriptor
    close(fi->fh);
    return 0;
}

// 更新操作結構，把新的函式註冊進去
static struct fuse_operations dedup_oper = {
    .getattr = dedup_getattr,
    .readdir = dedup_readdir,
    .open = dedup_open,
    .read = dedup_read,
    .write = dedup_write,
    .create = dedup_create,   // 新增：處理建立檔案請求
    .release = dedup_release, // 新增：處理關閉檔案請求
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