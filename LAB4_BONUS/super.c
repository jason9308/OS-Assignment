#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include "osfs.h"

/**
 * Struct: osfs_super_ops
 * Description: Defines the superblock operations for the osfs filesystem.
 */
const struct super_operations osfs_super_ops = {
    .statfs = simple_statfs,            // 提供檔案系統的統計資訊（如剩餘空間）。
    .drop_inode = generic_delete_inode, // 處理 inode 的刪除。
    .destroy_inode = osfs_destroy_inode // 自定義釋放 inode 資源。
};

// 釋放與 VFS 的 inode (struct inode) 關聯的 osfs_inode 
void osfs_destroy_inode(struct inode *inode)
{
    struct osfs_inode *osfs_inode;
    struct osfs_sb_info *sb_info;
    uint32_t current_block, next_block;

    // 如果 inode 沒有私有數據，直接返回
    if (!inode->i_private)
        return;

    osfs_inode = inode->i_private;
    sb_info = inode->i_sb->s_fs_info;

    // 遍歷釋放所有鏈接的 block
    current_block = osfs_inode->first_block;
    while (current_block != -1) {
     
        // 計算當前 block的地址 
        struct osfs_data_block *data_block = (struct osfs_data_block *)(sb_info->data_blocks + current_block * BLOCK_SIZE);
        // 先記錄下一個 block 
        next_block = data_block->next_block;

        // 釋放當前 block
        osfs_free_data_block(sb_info, current_block);
        // 移動到下一個 block
        current_block = next_block;
    }

    // 清空 inode 的私有數據
    inode->i_private = NULL;
}



/**
 * Function: osfs_fill_super
 * Description: Initializes the superblock with filesystem-specific information during mount.
 * Inputs:
 *   - sb: The superblock to be filled.
 *   - data: Data passed during mount.
 *   - silent: If non-zero, suppress certain error messages.
 * Returns:
 *   - 0 on successful initialization.
 *   - A negative error code on failure.
 */
int osfs_fill_super(struct super_block *sb, void *data, int silent)
{
    pr_info("osfs: Filling super start\n");
    struct inode *root_inode;
    struct osfs_sb_info *sb_info;
    void *memory_region;
    size_t total_memory_size;

    // Calculate total memory size required
    total_memory_size = sizeof(struct osfs_sb_info) +
                        INODE_BITMAP_SIZE * sizeof(unsigned long) +
                        BLOCK_BITMAP_SIZE * sizeof(unsigned long) +
                        INODE_COUNT * sizeof(struct osfs_inode) +
                        DATA_BLOCK_COUNT * sizeof(struct osfs_data_block); // 改成用 osfs_data_block的大小 

    // Allocate memory for superblock information and related structures
    memory_region = vmalloc(total_memory_size);
    if (!memory_region)
        return -ENOMEM;

    memset(memory_region, 0, total_memory_size);

    // Initialize superblock information
    sb_info = (struct osfs_sb_info *)memory_region;
    sb_info->magic = OSFS_MAGIC;
    sb_info->block_size = sizeof(struct osfs_data_block);
    sb_info->inode_count = INODE_COUNT;
    sb_info->block_count = DATA_BLOCK_COUNT;
    sb_info->nr_free_inodes = INODE_COUNT - 1;
    sb_info->nr_free_blocks = DATA_BLOCK_COUNT;

    // Partition the memory region into respective components
    sb_info->inode_bitmap = (unsigned long *)(sb_info + 1);
    sb_info->block_bitmap = sb_info->inode_bitmap + INODE_BITMAP_SIZE;
    sb_info->inode_table = (void *)(sb_info->block_bitmap + BLOCK_BITMAP_SIZE);
    sb_info->data_blocks = (void *)((char *)sb_info->inode_table +
                                    INODE_COUNT * sizeof(struct osfs_inode));

    // Initialize bitmaps
    memset(sb_info->inode_bitmap, 0, INODE_BITMAP_SIZE * sizeof(unsigned long));
    memset(sb_info->block_bitmap, 0, BLOCK_BITMAP_SIZE * sizeof(unsigned long));

    // Set superblock fields
    sb->s_magic = sb_info->magic;
    sb->s_fs_info = sb_info;
    sb->s_op = &osfs_super_ops;

    // Create root directory inode
    root_inode = new_inode(sb);
    if (!root_inode) {
        vfree(memory_region);
        return -ENOMEM;
    }

    root_inode->i_ino = ROOT_INODE;
    root_inode->i_sb = sb;
    root_inode->i_op = &osfs_dir_inode_operations;
    root_inode->i_fop = &osfs_dir_operations;
    root_inode->i_mode = S_IFDIR | 0755;
    set_nlink(root_inode, 2);
    simple_inode_init_ts(root_inode);
    
    // Initialize root directory's osfs_inode
    struct osfs_inode *root_osfs_inode = osfs_get_osfs_inode(sb, ROOT_INODE);
    if (!root_osfs_inode) {
        iput(root_inode);
        vfree(memory_region);
        return -EIO;
    }
    memset(root_osfs_inode, 0, sizeof(*root_osfs_inode));

    root_osfs_inode->i_ino = ROOT_INODE;
    root_osfs_inode->i_mode = root_inode->i_mode;
    root_osfs_inode->i_links_count = 2;
    root_osfs_inode->__i_atime = root_osfs_inode->__i_mtime = root_osfs_inode->__i_ctime = current_time(root_inode);
    root_inode->i_private = root_osfs_inode;
    root_osfs_inode->first_block = -1; // 記得要初始化 !!! 

    // Mark root directory inode as used
    set_bit(ROOT_INODE, sb_info->inode_bitmap);

    // Update root directory size
    root_inode->i_size = 0;
    inode_init_owner(&nop_mnt_idmap, root_inode, NULL, root_inode->i_mode);
    // Set the root directory
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        iput(root_inode);
        vfree(memory_region);
        return -ENOMEM;
    }
    pr_info("osfs: Superblock filled successfully \n");
    return 0;
}