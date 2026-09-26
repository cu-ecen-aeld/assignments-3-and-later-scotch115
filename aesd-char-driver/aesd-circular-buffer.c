/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#include <stdio.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer - the buffer to search for corresponding offset. Any necessary locking must be performed by caller.
 * @param char_offset - the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn - a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return - returns the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
// == Updated for Assignment 8 ==
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
          struct aesd_circular_buffer *buffer, size_t char_offset, 
          size_t *entry_offset_byte_rtn)
{
    /**
    * TODO: implement per description
    */
    // Set the "starting point" of the index to 'out_offs'.
    // NOTE: (This was the biggest issue with the previous implementation --
    //        I was not properly handling the updated out offset in this function).
    uint8_t index = buffer->out_offs;
    uint8_t count = 0;
    uint8_t entries;

    // If the buffer is full, we can just default to setting 'entries' to the max supported,
    // otherwise, get the 'in_offs' value.
    entries = buffer->full ?
      AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED :
      buffer->in_offs;

    while (count < entries) {
      struct aesd_buffer_entry *entry = &buffer->entry[index];

      if (char_offset < entry->size) {
        *entry_offset_byte_rtn = char_offset;
        return entry;
      }

      char_offset -= entry->size;

      index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
      count++;
    }
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
// == Updated for Assignment 8 ==
void aesd_circular_buffer_add_entry(
         struct aesd_circular_buffer *buffer,
         const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */
    buffer->entry[buffer->in_offs] = *add_entry;

    // If the buffer is full, set 'out_offs' to the next "first" entry
    // (i.e. if we've already wrapped around and filled in the first entry, move
    //  to the second entry offset).
    if (buffer->full) {
      buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // Update 'in_offs' to one more than the current input offset, wrapping around as needed.
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // If the input and output offset match, that means our buffer is full; update the 'full' attribute.
    if (buffer->in_offs == buffer->out_offs) {
      buffer->full = true;
    }
  }

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    #ifdef __KERNEL__
      printk(KERN_INFO "AESD-CIRCULAR-BUFFER INITIALIZED!!");
    #else
      printf("AESD-CIRCULAR-BUFFER INITIALIZED!!");
    #endif
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
