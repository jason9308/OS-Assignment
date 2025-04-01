#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <asm/current.h>

#define procfs_name "Mythread_info"
#define BUFSIZE  1024
char buf[BUFSIZE]; //kernel buffer

static ssize_t Mywrite(struct file *fileptr, const char __user *ubuf, size_t buffer_len, loff_t *offset) {
    /* Kernel write operation that copies data from user space to kernel space 
       and appends process/thread information */

    // Clear the kernel buffer to avoid any leftover data
    memset(buf, 0, BUFSIZE);

    // Ensure the input data size does not exceed the kernel buffer size
    if (buffer_len > BUFSIZE - 1) {
        pr_err("Input data too large for kernel buffer.\n"); // Log an error message
        return -EINVAL;  // Return error code for invalid argument
    }

    // Copy data from the user-space buffer to the kernel buffer
    if (copy_from_user(buf, ubuf, buffer_len)) {
        pr_err("Failed to copy data from user space.\n"); // Log an error message
        return -EFAULT;  // Return error code for copy failure
    }

    // Null-terminate the string to ensure proper formatting
    buf[buffer_len] = '\0';

    // Get information about the current process/thread
    struct task_struct *task = current; // `current` points to the currently executing task

    // Append additional process/thread information to the kernel buffer
    int len = snprintf(buf + buffer_len, BUFSIZE - buffer_len, 
                       "PID: %d, TID: %d, time: %llu\n",
                       task->tgid,            // Process ID (Thread Group ID)
                       task->pid,             // Thread ID
                       task->utime / 100 / 1000); // User mode execution time in milliseconds

    // Check if appending the information was successful
    if (len < 0 || (size_t)(buffer_len + len) >= BUFSIZE) {
        pr_err("Failed to append thread/process info to buffer.\n"); // Log an error message
        return -ENOSPC;  // Return error code for insufficient space
    }

    // Log the final kernel buffer content for debugging purposes
    pr_info("Formatted message written to kernel buffer:\n%s", buf);

    // Return the total length of data written (input data + appended info)
    return buffer_len + len;
}


static ssize_t Myread(struct file *fileptr, char __user *ubuf, size_t buffer_len, loff_t *offset) {
    /* Kernel read operation that transfers data from the kernel buffer to user space */

    // Check if the offset indicates all data has been read
    if (*offset > 0) {
        return 0;  // Return 0 to indicate no more data is available
    }

    // Get the length of the data in the kernel buffer
    int len = strlen(buf);

    // Calculate the number of bytes to read, ensuring it does not exceed the user buffer size or available data
    size_t bytes_to_read = min_t(size_t, buffer_len, len - *offset);

    // Copy data from the kernel buffer to the user space buffer
    if (copy_to_user(ubuf, buf + *offset, bytes_to_read)) {
        pr_err("Failed to copy data to user space.\n");  // Log an error message
        return -EFAULT;  // Return error code for copy failure
    }

    // Update the offset to reflect the number of bytes read
    *offset += bytes_to_read;

    // Return the actual number of bytes read
    return bytes_to_read;
}


static struct proc_ops Myops = {
    .proc_read = Myread,
    .proc_write = Mywrite,
};

static int My_Kernel_Init(void){
    proc_create(procfs_name, 0644, NULL, &Myops);   
    pr_info("My kernel says Hi");
    return 0;
}

static void My_Kernel_Exit(void){
    remove_proc_entry(procfs_name, NULL);
    pr_info("My kernel says GOODBYE");
}

module_init(My_Kernel_Init);
module_exit(My_Kernel_Exit);

MODULE_LICENSE("GPL");