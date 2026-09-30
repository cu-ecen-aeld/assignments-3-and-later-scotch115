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
#include "aesd_ioctl.h"

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
            *f_pos,
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
            goto out;
        }
        // If we were able to read/return the bytes successfully, update the
        // f_pos and retval values.
        *f_pos += bytesRead;
        retval = bytesRead;
    } else {
        goto out;
    }

    out:
        // Unlock/release the aesd_device mutex.
        mutex_unlock(&dev->lock);
        return retval;
}

// Assignment 9 Update: Recognized that this function was overly complicated and hard to debug;
//                      removed unnecessary variables and checks
// Transfer data FROM USERSPACE TO KERNELSPACE (driver).
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    struct aesd_dev *dev = filp->private_data;
    char * writeCmd = NULL;
    
    // Handle calls where count is zero (empty call).
    if (count == 0) {
        return 0;
    }

    writeCmd = kmalloc(count, GFP_KERNEL);
    if (!writeCmd) {
        return -ENOMEM;
    }

    if (copy_from_user(writeCmd, buf, count)) {
        kfree(writeCmd);
        return -EFAULT;
    }

    // Lock with aesd_device mutex.
    if (mutex_lock_interruptible(&dev->lock)) {
        kfree(writeCmd);
        return -ERESTARTSYS;
    }

    // If something is buffered, add it to the local buffer first.
    if (dev->buffered_entry.size == 0) {
        dev->buffered_entry.buffptr = kmalloc(count, GFP_KERNEL);
        if (!dev->buffered_entry.buffptr) {
            retval = -ENOMEM;
            goto out;
        }
        memcpy((char *)dev->buffered_entry.buffptr, writeCmd, count);
        dev->buffered_entry.size = count;
        PDEBUG("Added %s to 'dev->buffered_entry'", writeCmd);
    } else {
        dev->buffered_entry.buffptr = krealloc(dev->buffered_entry.buffptr, dev->buffered_entry.size + count, GFP_KERNEL);
        if (!dev->buffered_entry.buffptr) {
            retval = -ENOMEM;
            goto out;
        }
        memcpy((char *)dev->buffered_entry.buffptr + dev->buffered_entry.size, writeCmd, count);
        dev->buffered_entry.size += count;
    }

    if (memchr(writeCmd, '\n', count)) {
        if (dev->kernel_buffer.full) {
            kfree(dev->kernel_buffer.entry[dev->kernel_buffer.in_offs].buffptr);
        }

        aesd_circular_buffer_add_entry(&dev->kernel_buffer, &dev->buffered_entry);

        dev->buffered_entry.buffptr = NULL;
        dev->buffered_entry.size = 0;
    }

    retval = count;

    // Cleanup.
    out: 
        mutex_unlock(&dev->lock);
        kfree(writeCmd);
        return retval;
}

/**
 * TODO: Define llseek function per Assignment 9 requirements
 */
loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t retval;
    size_t bufferSize = 0;
    struct aesd_buffer_entry *entry;
    uint8_t index;

    // Lock mutex
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    // Loop through each item in the buffer and add it to "bufferSize" in order to be able to treat
    // the buffer as a single string of chars
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->kernel_buffer, index) {
        if (entry->buffptr) {
            bufferSize += entry->size;
        }
    }

    // Use "fixed_size_llseek" kernel function to support SEEK_SET, SEEK_CUR, and SEEK_END
    // Reference: https://github.com/torvalds/linux/blob/551c722f40809618230001baccf219193e22fc5a/fs/read_write.c#L264
    retval = fixed_size_llseek(filp, off, whence, bufferSize);

    // Unlock mutex
    mutex_unlock(&dev->lock);
    return retval;
}

/**
 * TODO: Define custom ioctl function for the aesdchar driver per Assignment 9 requirements
 */
long aesd_ioctl(struct file *filp, unsigned int ioctlCommand, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    long retval = 0;
    struct aesd_seekto seekto;
    size_t current_offset = 0;
    uint8_t count = 0;
    uint8_t bufferSize = 0;
    uint8_t index = 0;
    struct aesd_buffer_entry *entry;

    // Verify that the IOCTL Command received is intended for our driver by checking that the "_IOC_TYPE" equals our magic number
    if (_IOC_TYPE(ioctlCommand) != AESD_IOC_MAGIC) {
        return -ENOTTY;
    }

    // Verify that the max number of IOCTL commands does not exceed the maximum defined in AESDCHAR_IOC_MAXNR
    if (_IOC_NR(ioctlCommand) > AESDCHAR_IOC_MAXNR) {
        return -ENOTTY;
    }

    // Only process the IOCTL Command received if it matches  "AESDCHAR_IOCSEEKTO" as defined by aesdchar_ioctl.h
    if (ioctlCommand == AESDCHAR_IOCSEEKTO) {
            // Read the incoming IOCTL command into the aesd_seekto structure, and return an error if it fails
            if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto))) {
                return -EFAULT;
            }

            // Lock mutex
            if (mutex_lock_interruptible(&dev->lock)) {
                return -ERESTARTSYS;
            }

            // Count each entry in the circular buffer and increase the "bufferSize" accordingly
            AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->kernel_buffer, index) {
                bufferSize++;
            }

            // If the write command is larger than our total bufferSize, return an error (it won't fit in our buffer!)
            if (seekto.write_cmd >= bufferSize) {
                retval = -EINVAL;
            } else {
                uint8_t i = 0;
                // Iterate through the circular buffer to locate where to write our data in the buffer
                AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->kernel_buffer, i) {
                    // When we match the command to write into, ensure the command length does not match or exceed the entry's size
                    if (count == seekto.write_cmd) {
                        if (seekto.write_cmd_offset >= entry->size) {
                            retval = -EINVAL;
                        } else {
                            // If it does not, update the file pointer position's current offset to include the IOCTL write offset
                            filp->f_pos = current_offset + seekto.write_cmd_offset;
                        }
                        break;
                    }
                    current_offset += entry->size;
                    count++;
                }
            }

            // Unlock mutex
            mutex_unlock(&dev->lock);
    // Return "ENOTTY" for any other IOCTL command requests -- we only support AESDCHAR_IOCSEEKTO
     } else {
         retval = -ENOTTY;
     }

    return retval;        
}

struct file_operations aesd_fops = {
    .owner          = THIS_MODULE,
    .read           = aesd_read,
    .write          = aesd_write,
    .open           = aesd_open,
    .release        = aesd_release,
    .llseek         = aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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
    aesd_device.buffered_entry.buffptr = NULL;
    aesd_device.buffered_entry.size = 0;
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

    kfree(aesd_device.buffered_entry.buffptr);
    aesd_device.buffered_entry.buffptr = NULL;
    aesd_device.buffered_entry.size = 0;

    // Use the provided circular buffer loop to free all entries on cleanup.
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.kernel_buffer, index) {
        if (entry->buffptr != NULL) {
            kfree(entry->buffptr);
            entry->buffptr = NULL;
            entry->size = 0;
        }
    }
    PDEBUG("Unloaded aesdchar driver module.");

    mutex_destroy(&aesd_device.lock);
    unregister_chrdev_region(devno, 1);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
