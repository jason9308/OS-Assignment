#include <linux/fs.h>
#include <linux/uaccess.h>
#include "osfs.h"

/**
 * Function: osfs_get_osfs_inode
 * Description: Retrieves the osfs_inode structure for a given inode number.
 * Inputs:
 *   - sb: The superblock of the filesystem.
 *   - ino: The inode number to retrieve.
 * Returns:
 *   - A pointer to the osfs_inode structure if successful.
 *   - NULL if the inode number is invalid or out of bounds.
 */
struct osfs_inode *osfs_get_osfs_inode(struct super_block *sb, uint32_t ino)
{
    // 根據 ino編號找到對應 osfs_inode 
    struct osfs_sb_info *sb_info = sb->s_fs_info;

    if (ino == 0 || ino >= sb_info->inode_count) // File system inode count upper bound
        return NULL;
    return &((struct osfs_inode *)(sb_info->inode_table))[ino];
}

/**
 * Function: osfs_get_free_inode
 * Description: Allocates a free inode number from the inode bitmap.
 * Inputs:
 *   - sb_info: The superblock information of the filesystem.
 * Returns:
 *   - The allocated inode number on success.
 *   - -ENOSPC if no free inode is available.
 */
int osfs_get_free_inode(struct osfs_sb_info *sb_info)
{
    // 照順序搜索可用的 inode 
    uint32_t ino;

    for (ino = 1; ino < sb_info->inode_count; ino++) {
        if (!test_bit(ino, sb_info->inode_bitmap)) {
            set_bit(ino, sb_info->inode_bitmap);
            sb_info->nr_free_inodes--;
            return ino;
        }
    }
    pr_err("osfs_get_free_inode: No free inode available\n");
    return -ENOSPC;
}

/**
 * Function: osfs_iget
 * Description: Creates or retrieves a VFS inode from a given inode number.
 * Inputs:
 *   - sb: The superblock of the filesystem.
 *   - ino: The inode number to load.
 * Returns:
 *   - A pointer to the VFS inode on success.
 *   - ERR_PTR(-EFAULT) if the osfs_inode cannot be retrieved.
 *   - ERR_PTR(-ENOMEM) if memory allocation for the inode fails.
 */
struct inode *osfs_iget(struct super_block *sb, unsigned long ino)
{
    // 根據自定義 osfs_inode 建立一個對應的 VFS inode (struct inode)，並將兩者關聯起來
    struct osfs_inode *osfs_inode;
    struct inode *inode;

    osfs_inode = osfs_get_osfs_inode(sb, ino);
    if (!osfs_inode)
        return ERR_PTR(-EFAULT);

    inode = new_inode(sb);
    if (!inode)
        return ERR_PTR(-ENOMEM);

    inode->i_ino = ino;
    inode->i_sb = sb;
    inode->i_mode = osfs_inode->i_mode;
    i_uid_write(inode, osfs_inode->i_uid);
    i_gid_write(inode, osfs_inode->i_gid);
    inode->__i_atime = osfs_inode->__i_atime;
    inode->__i_mtime = osfs_inode->__i_mtime;
    inode->__i_ctime = osfs_inode->__i_ctime;
    inode->i_size = osfs_inode->i_size;
    inode->i_blocks = osfs_inode->i_blocks;
    inode->i_private = osfs_inode;
    
    // 設定 inode 的操作指標
    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &osfs_dir_inode_operations;
        inode->i_fop = &osfs_dir_operations;
    } else if (S_ISREG(inode->i_mode)) {
        inode->i_op = &osfs_file_inode_operations;
        inode->i_fop = &osfs_file_operations;
    }

    // Insert the inode into the inode hash
    insert_inode_hash(inode);

    return inode;
}

/**
 * Function: osfs_alloc_data_block
 * Description: Allocates a free data block from the block bitmap.
 * Inputs:
 *   - sb_info: The superblock information of the filesystem.
 *   - block_no: Pointer to store the allocated block number.
 * Returns:
 *   - 0 on successful allocation.
 *   - -ENOSPC if no free data block is available.
 */
int osfs_alloc_data_block(struct osfs_sb_info *sb_info, uint32_t *block_no)
{
    uint32_t i;
    // 遍歷 block_bitmap，查找第一個未被佔用的 block
    for (i = 0; i < sb_info->block_count; i++) {
        if (!test_bit(i, sb_info->block_bitmap)) {
            set_bit(i, sb_info->block_bitmap); // 標記 block為已使用
            sb_info->nr_free_blocks--;
            *block_no = i;
            
            // 計算 block的地址 
            struct osfs_data_block *data_block = 
                (struct osfs_data_block *)(sb_info->data_blocks + i * sb_info->block_size);
            
            memset(data_block->data, 0, sizeof(data_block->data));
            data_block->next_block = -1; // 初始化鏈接為 -1 
            return 0;
        }
    }
    pr_err("osfs_alloc_data_block: No free data block available\n");
    return -ENOSPC;
}

void osfs_free_data_block(struct osfs_sb_info *sb_info, uint32_t block_no)
{
    clear_bit(block_no, sb_info->block_bitmap);
    sb_info->nr_free_blocks++;
}

void osfs_free_data_block_chain(struct inode *inode)
{
    struct osfs_inode *osfs_inode;
    struct osfs_sb_info *sb_info;
    uint32_t current_block, next_block;

    if (!inode->i_private)
        return;

    osfs_inode = inode->i_private;
    sb_info = inode->i_sb->s_fs_info;

    current_block = osfs_inode->first_block;
    // 釋放整個 block鏈接 
    while (current_block != -1) {
        struct osfs_data_block *data_block = 
            (struct osfs_data_block *)(sb_info->data_blocks + current_block * sb_info->block_size);
        next_block = data_block->next_block;

        osfs_free_data_block(sb_info, current_block);
        current_block = next_block;
    }

    inode->i_private = NULL;
}