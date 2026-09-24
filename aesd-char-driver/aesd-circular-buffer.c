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
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
          struct aesd_circular_buffer *buffer, size_t char_offset, 
          size_t *entry_offset_byte_rtn)
{
    /**
    * TODO: implement per description
    */
    uint8_t index = 0;
    size_t char_check;
    int sizeCheck = 0;
    struct aesd_buffer_entry * entry;
    char_check = char_offset;

    AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, index) {
      // printf("Entry has size %ld and value: %s", entry->size, entry->buffptr);
      // printf("Char Check value is: %ld\n", char_check);
      sizeCheck += entry->size;
      // Check if the "char_offset" is bigger or smaller than the entry size
      if (char_check >= entry->size - 1) {
        char_check -= entry->size;
     // printf("Index is: %d\n", index);
      } else {
        break;
      }
    }
    // printf("Return value %ld\n", char_check);
    // printf("Total buffer size: %d ; char offset size: %d\n", sizeCheck - 1, (int)(char_offset));
    if (sizeCheck - 1 < (int)(char_offset)) {
      // printf("OVERFLOW\n");
    } else {
      if (char_check == -1) {
        size_t len = strlen(buffer->entry[index-1].buffptr) - 1;
        *entry_offset_byte_rtn = len;
        return &buffer->entry[index-1];
      } else {
        // printf("Index %d:  %s\n", index, buffer->entry[index].buffptr);
        // printf("Test: %c\n", buffer->entry[index].buffptr[strlen(buffer->entry[index].buffptr)]);
        entry_offset_byte_rtn = &char_check;
        return &buffer->entry[index];
      }
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
void aesd_circular_buffer_add_entry(
         struct aesd_circular_buffer *buffer,
         const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */
    if (buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
      buffer->full = 1;
      // TODO: For each item, scoot it one element back and insert the new item at the end
      uint8_t index = 0;
      struct aesd_buffer_entry * entry;
      AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, index) {
        if (index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1) {
          entry->buffptr = buffer->entry[index+1].buffptr;
          entry->size = strlen(buffer->entry[index+1].buffptr);
        } else {
          entry->buffptr = buffer->entry[index+1].buffptr;
          buffer->entry[index+1].buffptr = NULL;
        }
      }    
      buffer->in_offs = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1;
      // printf("%s", add_entry->buffptr);
      // printf("BUFFER FULL\n");
    }

    // printf("Input offset: %d ; Output offset: %d\n", buffer->in_offs, buffer->out_offs);

    // Loop through the buffer entry array to see if we have space to add a new entry
    buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
    buffer->entry[buffer->in_offs].size = strlen(add_entry->buffptr);
    buffer->in_offs += 1;
    if (buffer->full == 1) {
      buffer->out_offs += buffer->in_offs;
    }
  }

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
