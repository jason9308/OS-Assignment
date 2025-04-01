#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <asm/current.h>

#define procfs_name "Mythread_info"
#define BUFSIZE  1024
char buf[BUFSIZE];

static ssize_t Mywrite(struct file *fileptr, const char __user *ubuf, size_t buffer_len, loff_t *offset){
    /* Do nothing */
	return 0;
}


static ssize_t Myread(struct file *fileptr, char __user *ubuf, size_t buffer_len, loff_t *offset){
    /*Your code here*/
    struct task_struct *task = current;
    struct task_struct *thread;

    int len = 0;
    if (*offset > 0) {
        return 0;  // Indicates that the entire buffer has already been read to avoid infinite looping
    }

    // Iterate over all threads of the current process, skipping the main thread
    for_each_thread(task, thread) {
        if (thread->pid != task->pid) {  // Skip the main thread
            len += snprintf(buf + len, BUFSIZE - len, 
                            "PID: %d, TID: %d, Priority: %d, State: %d\n", 
                            task->pid, thread->pid, thread->prio, thread->__state);
        }
    }

    // Copy the formatted buffer to the user space
    if (copy_to_user(ubuf, buf, len)) {
        return -EFAULT;  // Return an error if the copy to user space fails
    }

    *offset = len;  // Update the offset to indicate the buffer has been read
    return len;     // Return the total length of data read

    /****************/
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
    //remove_proc_entry(procfs_name, NULL);
    remove_proc_entry(procfs_name, NULL);
    pr_info("My kernel says GOODBYE");
}

module_init(My_Kernel_Init);
module_exit(My_Kernel_Exit);

MODULE_LICENSE("GPL");