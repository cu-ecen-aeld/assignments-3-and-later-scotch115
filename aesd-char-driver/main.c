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
#include <linux/mutex.h> // needed for locking/unlocking.
#include <linux/slab.h> // needed for kernel memory allocation.
#include <linux/uaccess.h> // needed for transferring to/from userspace.
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
    filp->f_pos = 0;

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

// Transfer data FROM KERNELSPACE (driver) TO USERSPACE.
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    size_t byte_offset = 0;
    size_t bytesRead;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry * entry;

    // Lock with aesd_device mutex.
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    // Get entry/offset using f_pos                         
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(
            &dev->kernel_buffer,
            /* Cast *f_pos as size_t to pass it to the function correctly. */
            (size_t)*f_pos,
            &byte_offset);

    // If we get a match, set 'bytesRead' to the entry size - the returned byte offset .
    if (entry != NULL) {
        bytesRead = entry->size - byte_offset;
        // If bytesRead exceeds the count, cap it to the count.
        if (bytesRead > count) {
            bytesRead = count;
        }

        // Return the entry text, starting at the retured byte offset, to the
        // user-space call that requested the data.
        if (copy_to_user(buf, entry->buffptr + byte_offset, bytesRead)) {
            retval = -EFAULT;
        } else {
            // If we were able to read/return the bytes successfully, update the
            // f_pos and retval values.
            *f_pos += bytesRead;
            retval = bytesRead;
        }
    }

    // Unlock/release the aesd_device mutex.
    mutex_unlock(&dev->lock);
    return retval;
}

// Transfer data FROM USERSPACE TO KERNELSPACE (driver).
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
    
    // Handle calls where count is zero (empty call).
    if (count == 0) {
        return 0;
    }

    // Lock with aesd_device mutex.
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    // Set the size to the size of any buffered entries + the size of the incoming entry.
    size = dev->buffered_size + count;
    buffer = kmalloc(size, GFP_KERNEL);
    if (buffer == NULL) {
        goto out;
    }

    // If something is buffered, add it to the local buffer first.
    if (dev->buffered_size > 0) {
        memcpy(buffer, dev->buffered_entry, dev->buffered_size);
        PDEBUG("Added %s to 'dev->buffered_entry'", buffer);
    }

    // Write the incoming data from user-space to the buffer, starting after any pre-buffered entries.
    if (copy_from_user(buffer + dev->buffered_size, buf, count)) {
        retval = -EFAULT;
        goto out;
    }

    // Free the buffered entry in the aesd_device object since we have it
    // stored in the local buffer; reset the size to '0'.
    kfree(dev->buffered_entry);
    dev->buffered_entry = NULL;
    dev->buffered_size = 0;

    // Loop through each item in the local buffer to check for newline character.
    for (index = 0; index < size; index++) {
        // If we get a match, process the command and pass it to the circular buffer function.
        if (buffer[index] == '\n') {
            size_t writeCmd_size = index - start + 1;

            writeCmd = kmalloc(writeCmd_size, GFP_KERNEL);
            if (writeCmd == NULL) {
                retval = -ENOMEM;
                goto out;
            }

            memcpy(writeCmd, buffer + start, writeCmd_size);

            // Check if the buffer is full; if it is, free the data at the in_offs so we can insert the new item.
            if (dev->kernel_buffer.full) {
                kfree(dev->kernel_buffer.entry[dev->kernel_buffer.in_offs].buffptr);
            }

            entry.buffptr = writeCmd;
            entry.size = writeCmd_size;
            aesd_circular_buffer_add_entry(&dev->kernel_buffer, &entry);

            // Reset the command to continue running through the loop.
            writeCmd = NULL;
            start = index + 1;
        }
    }

    // Update the size and data for any buffered data in the aesd_device object.
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

    retval = count;

    // Cleanup.
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
    // Initialize the circular buffer and mutex.
    aesd_circular_buffer_init(&aesd_device.kernel_buffer);
    mutex_init(&aesd_device.lock);
    aesd_device.buffered_entry = NULL;
    aesd_device.buffered_size = 0;
    PDEBUG("Loaded aesdchar driver module.");
    
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

    // Use the provided circular buffer loop to free all entries on cleanup.
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
    PDEBUG("Unloaded aesdchar driver module.");

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
