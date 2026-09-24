/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/fs.h> // file_operations
#include <linux/mutex.h> // needed for locking/unlocking
#include <linux/slab.h> // needed for kernel memory allocation
#include <linux/uaccess.h> // needed for transferring to/from userspace
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Jordan Gamache"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    // aesd_circular_buffer_init(&dev->kernel_buffer);

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

// Transfer data FROM KERNELSPACE (driver) TO USERSPACE
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    size_t byte_offset = 0;
    size_t copy_bytes;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry * entry = aesd_circular_buffer_find_entry_offset_for_fpos(
        &dev->kernel_buffer, (size_t)*f_pos, &byte_offset);

    if (mutex_lock_interruptible(&dev->lock)) { //? Why mutex and not semaphore?
        return -ERESTARTSYS;
    }

    // if (*f_pos >= dev->size)
    //     goto out;
    // if (*f_pos + count > dev->size)
    //     count = dev->size - *f_pos;

    if (entry != NULL) {
        copy_bytes = entry->size - byte_offset;
        if (copy_bytes > count) {
            copy_bytes = count;
        }

        if (copy_to_user(buf, entry->buffptr + byte_offset, copy_bytes)) {
            retval = -EFAULT;
        } else {
            *f_pos += copy_bytes;
            retval = copy_bytes;
        }
    }
    
    out:
        mutex_unlock(&dev->lock);
        return retval;
}

// Transfer data FROM USERSPACE TO KERNELSPACE (driver)
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    struct aesd_dev *dev = filp->private_data;
    char * buffer = NULL;
    char * writeCmd = NULL;
    size_t size;
    size_t start = 0;
    size_t index;
    struct aesd_buffer_entry entry;
    
    if (count == 0) {
        return 0; // Handle calls where count is zero (empty)
    }
    
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    size = dev->buffered_size + count;
    buffer = kmalloc(size, GFP_KERNEL);
    if (buffer == NULL) {
        goto out;
    }

    if (dev->buffered_size > 0) {
        memcpy(buffer, dev->buffered_entry, dev->buffered_size);
    }

    if (copy_from_user(buffer + dev->buffered_size, buf, count)) {
        retval = -EFAULT;
        goto out;
    }

    kfree(dev->buffered_entry);
    dev->buffered_entry = NULL;
    dev->buffered_size = 0;

    for (index = 0; index < size; index++) {
        if (buffer[index] == '\n') {
            size_t writeCmd_size = index - start + 1;

            writeCmd = kmalloc(writeCmd_size, GFP_KERNEL);
            if (writeCmd == NULL) {
                retval = -ENOMEM;
                goto out;
            }

            memcpy(writeCmd, buffer + start, writeCmd_size);

            if (dev->kernel_buffer.full) {
                kfree(dev->kernel_buffer.entry[dev->kernel_buffer.in_offs].buffptr);
            }

            entry.buffptr = writeCmd;
            entry.size = writeCmd_size;
            aesd_circular_buffer_add_entry(&dev->kernel_buffer, &entry);

            writeCmd = NULL;
            start = index + 1;
        }
    }

    // Update size
    if (start < size) {
        dev->buffered_size = size - start;
        dev->buffered_entry = kmalloc(dev->buffered_size, GFP_KERNEL);
        if (dev->buffered_entry == NULL) {
            dev->buffered_size = 0;
            retval = -ENOMEM;
            goto out;
        }

        memcpy(dev->buffered_entry, buffer + start, dev->buffered_size);
    }

    // *f_pos += count;
    retval = count;

    out: 
        kfree(writeCmd);
        kfree(buffer);
        mutex_unlock(&dev->lock);
        return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    // memset(&kernel_buffer,0,sizeof(struct aesd_circular_buffer));
    aesd_circular_buffer_init(&aesd_device.kernel_buffer);
    mutex_init(&aesd_device.lock);
    aesd_device.buffered_entry = NULL;
    aesd_device.buffered_size = 0;
    
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    uint8_t index;
    struct aesd_buffer_entry * entry;

    kfree(aesd_device.buffered_entry);
    aesd_device.buffered_entry = NULL;
    aesd_device.buffered_size = 0;

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.kernel_buffer, index) {
        if (entry->buffptr != NULL) {
            kfree(entry->buffptr);
            entry->buffptr = NULL;
            entry->size = 0;
        }
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
