#include <linux/fs.h>
#include <linux/uaccess.h>
#include "osfs.h"

/**
 * Function: osfs_read
 * Description: Reads data from a file.
 * Inputs:
 *   - filp: The file pointer representing the file to read from.
 *   - buf: The user-space buffer to copy the data into.
 *   - len: The number of bytes to read.
 *   - ppos: The file position pointer.
 * Returns:
 *   - The number of bytes read on success.
 *   - 0 if the end of the file is reached.
 *   - -EFAULT if copying data to user space fails.
 */
static ssize_t osfs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(filp);
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    uint32_t current_block = osfs_inode->first_block;
    size_t offset = *ppos;
    size_t bytes_read = 0;

    // 如果檔案為空，直接返回
    if (osfs_inode->i_blocks == 0 || *ppos >= osfs_inode->i_size)
        return 0;

    // 限制讀取範圍
    if (*ppos + len > osfs_inode->i_size)
        len = osfs_inode->i_size - *ppos;

    // 遍歷數據塊鏈，找到對應的數據塊位置
    while (current_block != -1 && len > 0) {
        struct osfs_data_block *data_block =
            (struct osfs_data_block *)(sb_info->data_blocks + current_block * sb_info->block_size);

        size_t block_offset = offset % (sb_info->block_size - sizeof(uint32_t));
        size_t block_size = min(len, (size_t)(sb_info->block_size - sizeof(uint32_t) - block_offset));

        // 從當前數據塊讀取資料到使用者空間
        if (copy_to_user(buf + bytes_read, data_block->data + block_offset, block_size))
            return -EFAULT;

        // 更新偏移量和剩餘讀取長度
        offset -= block_offset;
        len -= block_size;
        bytes_read += block_size;

        current_block = data_block->next_block; // 移動到下一個數據塊
    }

    *ppos += bytes_read; // 更新檔案位置指標
    return bytes_read;
}



/**
 * Function: osfs_write
 * Description: Writes data to a file.
 * Inputs:
 *   - filp: The file pointer representing the file to write to.
 *   - buf: The user-space buffer containing the data to write.
 *   - len: The number of bytes to write.
 *   - ppos: The file position pointer.
 * Returns:
 *   - The number of bytes written on success.
 *   - -EFAULT if copying data from user space fails.
 *   - Adjusted length if the write exceeds the block size.
 */
static ssize_t osfs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(filp);
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    uint32_t current_block = osfs_inode->first_block;
    uint32_t prev_block = 0;
    size_t offset = *ppos;
    size_t bytes_written = 0;

    // 如果尚未分配數據塊，分配第一個數據塊
    if (osfs_inode->i_blocks == 0) {
        if (osfs_alloc_data_block(sb_info, &current_block))
            return -ENOSPC;
        osfs_inode->first_block = current_block;
        osfs_inode->i_blocks++;
    }

    while (len > 0) {
        struct osfs_data_block *data_block =
            (struct osfs_data_block *)(sb_info->data_blocks + current_block * sb_info->block_size);

        size_t block_offset = offset % (sb_info->block_size - sizeof(uint32_t));
        size_t block_size = min(len, (size_t)(sb_info->block_size - sizeof(uint32_t) - block_offset));

        // 從使用者空間寫入資料到數據塊
        if (copy_from_user(data_block->data + block_offset, buf + bytes_written, block_size))
            return -EFAULT;

        // 更新檔案偏移量和寫入長度
        offset -= block_offset;
        len -= block_size;
        bytes_written += block_size;

        // 如果還有剩餘資料且需要新數據塊
        if (len > 0 && data_block->next_block == -1) {
            uint32_t new_block;
            if (osfs_alloc_data_block(sb_info, &new_block))
                return -ENOSPC;

            data_block->next_block = new_block;
            current_block = new_block;
            osfs_inode->i_blocks++;
        } else {
            current_block = data_block->next_block;
        }
    }

    // 更新 inode 的大小和時間戳
    *ppos += bytes_written;
    osfs_inode->i_size = max(osfs_inode->i_size, (uint32_t)*ppos);
    inode->i_size = osfs_inode->i_size;
    osfs_inode->__i_mtime = osfs_inode->__i_ctime = current_time(inode);

    return bytes_written;
}


/**
 * Struct: osfs_file_operations
 * Description: Defines the file operations for regular files in osfs.
 */
const struct file_operations osfs_file_operations = {
    .open = generic_file_open, // Use generic open or implement osfs_open if needed
    .read = osfs_read,
    .write = osfs_write,
    .llseek = default_llseek,
    // Add other operations as needed
};

/**
 * Struct: osfs_file_inode_operations
 * Description: Defines the inode operations for regular files in osfs.
 * Note: Add additional operations such as getattr as needed.
 */
const struct inode_operations osfs_file_inode_operations = {
    // Add inode operations here, e.g., .getattr = osfs_getattr,
};
