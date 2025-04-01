#include <linux/fs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "osfs.h"

/**
 * Function: osfs_lookup
 * Description: Looks up a file within a directory.
 * Inputs:
 *   - dir: The inode of the directory to search in.
 *   - dentry: The dentry representing the file to look up.
 *   - flags: Flags for the lookup operation.
 * Returns:
 *   - A pointer to the dentry if the file is found.
 *   - NULL if the file is not found, allowing the VFS to handle it.
 */
static struct dentry *osfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
    // 用來查找指定目錄中的文件 
    // 從osfs_inode 取得數據塊鏈結的起始點 first_block，遍歷所有鏈結的 block 
    struct osfs_sb_info *sb_info = dir->i_sb->s_fs_info;
    struct osfs_inode *parent_inode = dir->i_private;
    uint32_t current_block;
    struct osfs_dir_entry *dir_entries;
    int dir_entry_count, i;
    struct inode *inode = NULL;

    pr_info("osfs_lookup: Looking up '%.*s' in inode %lu\n",
            (int)dentry->d_name.len, dentry->d_name.name, dir->i_ino);

    current_block = parent_inode->first_block;

    // 遍歷所有鏈接的數據塊
    while (current_block != -1) {
        struct osfs_data_block *data_block =
            (struct osfs_data_block *)(sb_info->data_blocks + current_block * sb_info->block_size);

        // 計算當前數據塊中的目錄項目數量
        dir_entry_count = parent_inode->i_size / sizeof(struct osfs_dir_entry);
        dir_entries = (struct osfs_dir_entry *)data_block->data;

        // 遍歷當前數據塊的目錄項目
        for (i = 0; i < dir_entry_count; i++) {
            if (strlen(dir_entries[i].filename) == dentry->d_name.len &&
                strncmp(dir_entries[i].filename, dentry->d_name.name, dentry->d_name.len) == 0) {
                // 找到匹配的檔案，載入 inode
                inode = osfs_iget(dir->i_sb, dir_entries[i].inode_no);
                if (IS_ERR(inode)) {
                    pr_err("osfs_lookup: Error getting inode %u\n", dir_entries[i].inode_no);
                    return ERR_CAST(inode);
                }
                return d_splice_alias(inode, dentry);
            }
        }

        // 移動到下一個數據塊
        current_block = data_block->next_block;
    }

    return NULL; // 若無匹配檔案，返回 NULL
}

/**
 * Function: osfs_iterate
 * Description: Iterates over the entries in a directory.
 * Inputs:
 *   - filp: The file pointer representing the directory.
 *   - ctx: The directory context used for iteration.
 * Returns:
 *   - 0 on successful iteration.
 *   - A negative error code on failure.
 */
static int osfs_iterate(struct file *filp, struct dir_context *ctx)
{
    // 遍歷指定目錄中的所有條目，將條目的名稱和元數據返回給用戶空間
    struct inode *inode = file_inode(filp);
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    struct osfs_inode *osfs_inode = inode->i_private;
    uint32_t current_block;
    struct osfs_dir_entry *dir_entries;
    int dir_entry_count, i;
    int entry_index = ctx->pos - 2; // Adjust position to skip "." and ".."

    if (ctx->pos == 0) {
        // Emit "." and ".." entries for the current directory
        if (!dir_emit_dots(filp, ctx))
            return 0;
    }

    current_block = osfs_inode->first_block;

    // Traverse all linked data blocks
    while (current_block != -1) {
        struct osfs_data_block *data_block =
            (struct osfs_data_block *)(sb_info->data_blocks + current_block * sb_info->block_size);

        dir_entries = (struct osfs_dir_entry *)data_block->data;
        dir_entry_count = osfs_inode->i_size / sizeof(struct osfs_dir_entry);

        // Traverse directory entries in the current block
        for (i = 0; i < dir_entry_count; i++) {
            if (entry_index > 0) {
                entry_index--; // Skip entries until reaching the position
                continue;
            }

            if (!dir_emit(ctx, dir_entries[i].filename, strlen(dir_entries[i].filename),
                          dir_entries[i].inode_no, DT_UNKNOWN)) {
                pr_err("osfs_iterate: dir_emit failed for entry '%s'\n", dir_entries[i].filename);
                return -EINVAL;
            }

            ctx->pos++; // Advance the context position
        }

        // Move to the next data block in the chain
        current_block = data_block->next_block;
    }

    return 0; // Successfully iterated through all entries
}

/**
 * Function: osfs_new_inode
 * Description: Creates a new inode within the filesystem.
 * Inputs:
 *   - dir: The inode of the directory where the new inode will be created.
 *   - mode: The mode (permissions and type) for the new inode.
 * Returns:
 *   - A pointer to the newly created inode on success.
 *   - ERR_PTR(-EINVAL) if the file type is not supported.
 *   - ERR_PTR(-ENOSPC) if there are no free inodes or blocks.
 *   - ERR_PTR(-ENOMEM) if memory allocation fails.
 *   - ERR_PTR(-EIO) if an I/O error occurs.
 */
struct inode *osfs_new_inode(const struct inode *dir, umode_t mode)
{
    struct super_block *sb = dir->i_sb;
    struct osfs_sb_info *sb_info = sb->s_fs_info;
    struct inode *inode;
    struct osfs_inode *osfs_inode;
    uint32_t ino, first_block;
    int ret;

    /* 檢查是否支持該文件模式 */
    if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode)) {
        pr_err("osfs_new_inode: Unsupported file type (only directory, regular file, and symlink are supported)\n");
        return ERR_PTR(-EINVAL);
    }

    /* 檢查是否有空閒的 inode 和數據塊 */
    if (sb_info->nr_free_inodes == 0 || sb_info->nr_free_blocks == 0)
        return ERR_PTR(-ENOSPC);

    /* 分配一個新的 inode 編號 */
    ino = osfs_get_free_inode(sb_info);
    if (ino < 0 || ino >= sb_info->inode_count)
        return ERR_PTR(-ENOSPC);

    /* 分配一個新的 VFS inode */
    inode = new_inode(sb);
    if (!inode)
        return ERR_PTR(-ENOMEM);

    /* 初始化 inode 的所有者和權限 */
    inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
    inode->i_ino = ino;
    inode->i_sb = sb;
    inode->i_blocks = 0;
    simple_inode_init_ts(inode);

    /* 設置 inode 的操作和文件操作 */
    if (S_ISDIR(mode)) {
        inode->i_op = &osfs_dir_inode_operations;
        inode->i_fop = &osfs_dir_operations;
        set_nlink(inode, 2); /* . 和 .. */
        inode->i_size = 0;
    } else if (S_ISREG(mode)) {
        inode->i_op = &osfs_file_inode_operations;
        inode->i_fop = &osfs_file_operations;
        set_nlink(inode, 1);
        inode->i_size = 0;
    } else if (S_ISLNK(mode)) {
        set_nlink(inode, 1);
        inode->i_size = 0;
    }

    /* 獲取 osfs_inode */
    osfs_inode = osfs_get_osfs_inode(sb, ino);
    if (!osfs_inode) {
        pr_err("osfs_new_inode: Failed to get osfs_inode for inode %u\n", ino);
        iput(inode);
        return ERR_PTR(-EIO);
    }
    memset(osfs_inode, 0, sizeof(*osfs_inode));

    /* 初始化 osfs_inode */
    osfs_inode->i_ino = ino;
    osfs_inode->i_mode = inode->i_mode;
    osfs_inode->i_uid = i_uid_read(inode);
    osfs_inode->i_gid = i_gid_read(inode);
    osfs_inode->i_size = inode->i_size;
    osfs_inode->i_blocks = 1; // 默認占用一個數據塊
    osfs_inode->__i_atime = osfs_inode->__i_mtime = osfs_inode->__i_ctime = current_time(inode);

    /* 分配第一個數據塊 */
    ret = osfs_alloc_data_block(sb_info, &first_block);
    if (ret) {
        pr_err("osfs_new_inode: Failed to allocate first data block\n");
        iput(inode);
        return ERR_PTR(ret);
    }
    osfs_inode->first_block = first_block;

    inode->i_private = osfs_inode;

    /* 更新 superblock 信息 */
    sb_info->nr_free_inodes--;

    /* 標記 inode 為 dirty */
    mark_inode_dirty(inode);

    pr_info("osfs_new_inode: Created inode %u with first block %u\n", ino, first_block);

    return inode;
}


static int osfs_add_dir_entry(struct inode *dir, uint32_t inode_no, const char *name, size_t name_len)
{
    // 向目錄（directory）中添加新的目錄項
    
    struct osfs_sb_info *sb_info = dir->i_sb->s_fs_info;
    struct osfs_inode *parent_inode = dir->i_private;
    uint32_t current_block = parent_inode->first_block;
    uint32_t prev_block = 0;
    struct osfs_data_block *data_block;
    struct osfs_dir_entry *dir_entries;
    int dir_entry_count;
    int i;

    // Traverse the linked data blocks to find a place for the new entry
    while (current_block != -1) {
        data_block = (struct osfs_data_block *)(sb_info->data_blocks + current_block * sb_info->block_size);

        dir_entries = (struct osfs_dir_entry *)data_block->data;
        dir_entry_count = parent_inode->i_size / sizeof(struct osfs_dir_entry);

        // Check for space in the current block
        for (i = 0; i < MAX_DIR_ENTRIES; i++) {
            if (dir_entries[i].filename[0] == '\0') { // Empty entry found
                strncpy(dir_entries[i].filename, name, name_len);
                dir_entries[i].filename[name_len] = '\0';
                dir_entries[i].inode_no = inode_no;

                parent_inode->i_size += sizeof(struct osfs_dir_entry); // Update size
                parent_inode->__i_mtime = parent_inode->__i_ctime = current_time(dir);

                return 0;
            }
        }

        // No space in this block, move to the next block
        // 沒有空間的話，循環到鏈結的下一個數據塊，繼續檢查 
        prev_block = current_block;
        current_block = data_block->next_block;
    }

    // Allocate a new block for additional directory entries
    // 空間不夠，分配一個新的 block
    uint32_t new_block;
    int ret = osfs_alloc_data_block(sb_info, &new_block);
    if (ret) {
        pr_err("osfs_add_dir_entry: Failed to allocate new data block\n");
        return ret;
    }

    // Initialize the new block
    data_block = (struct osfs_data_block *)(sb_info->data_blocks + new_block * sb_info->block_size);
    memset(data_block, 0, sb_info->block_size);
    data_block->next_block = -1;

    // Link the new block to the previous block
    if (prev_block != 0) {
        struct osfs_data_block *prev_data_block =
            (struct osfs_data_block *)(sb_info->data_blocks + prev_block * sb_info->block_size);
        prev_data_block->next_block = new_block;
    } else {
        parent_inode->first_block = new_block;
    }

    // Add the new entry to the new block
    dir_entries = (struct osfs_dir_entry *)data_block->data;
    strncpy(dir_entries[0].filename, name, name_len);
    dir_entries[0].filename[name_len] = '\0';
    dir_entries[0].inode_no = inode_no;

    parent_inode->i_size += sizeof(struct osfs_dir_entry); // Update size
    parent_inode->__i_mtime = parent_inode->__i_ctime = current_time(dir);

    return 0;
}



/**
 * Function: osfs_create
 * Description: Creates a new file within a directory.
 * Inputs:
 *   - idmap: The mount namespace ID map.
 *   - dir: The inode of the parent directory.
 *   - dentry: The dentry representing the new file.
 *   - mode: The mode (permissions and type) for the new file.
 *   - excl: Whether the creation should be exclusive.
 * Returns:
 *   - 0 on successful creation.
 *   - -EEXIST if the file already exists.
 *   - -ENAMETOOLONG if the file name is too long.
 *   - -ENOSPC if the parent directory is full.
 *   - A negative error code from osfs_new_inode on failure.
 */
static int osfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    struct osfs_inode *parent_inode = dir->i_private;
    struct osfs_inode *osfs_inode;
    struct inode *inode;
    int ret;

    pr_info("osfs_create: Creating file '%.*s'\n", (int)dentry->d_name.len, dentry->d_name.name);

    // 檢查文件名長度是否超過限制
    if (dentry->d_name.len > MAX_FILENAME_LEN) {
        pr_err("osfs_create: File name too long\n");
        return -ENAMETOOLONG;
    }

    // 創建並初始化 VFS 和 osfs 的 inode
    inode = osfs_new_inode(dir, mode);
    if (IS_ERR(inode)) {
        pr_err("osfs_create: Failed to allocate new inode\n");
        return PTR_ERR(inode);
    }

    osfs_inode = inode->i_private;
    if (!osfs_inode) {
        pr_err("osfs_create: Failed to retrieve private osfs_inode for inode %lu\n", inode->i_ino);
        iput(inode);
        return -EIO;
    }

    // 初始化 osfs_inode 的鏈接分配相關信息
    osfs_inode->first_block = -1;  // 初始化為沒有分配任何數據塊
    osfs_inode->i_blocks = 0;     // 目前還沒有數據塊

    // 將新文件添加到父目錄的條目中
    ret = osfs_add_dir_entry(dir, inode->i_ino, dentry->d_name.name, dentry->d_name.len);
    if (ret) {
        pr_err("osfs_create: Failed to add directory entry for '%.*s'\n", (int)dentry->d_name.len, dentry->d_name.name);
        iput(inode);
        return ret;
    }

    // 更新父目錄的元數據
    parent_inode->i_size += sizeof(struct osfs_dir_entry); // 增加目錄條目大小
    parent_inode->__i_mtime = parent_inode->__i_ctime = current_time(dir);

    // 將新 inode 綁定到 VFS 的 dentry
    d_instantiate(dentry, inode);

    pr_info("osfs_create: File '%.*s' created with inode %lu\n", (int)dentry->d_name.len, dentry->d_name.name, inode->i_ino);

    return 0;
}

const struct inode_operations osfs_dir_inode_operations = {
    .lookup = osfs_lookup,
    .create = osfs_create,
    // Add other operations as needed
};

const struct file_operations osfs_dir_operations = {
    .iterate_shared = osfs_iterate,
    .llseek = generic_file_llseek,
    // Add other operations as needed
};
