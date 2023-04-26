#include <stdint.h>
#include <string.h>

#include "datatypes.h"

ssize_t datatype_get_size(int type, void* ptr) {
  switch(type) {
    case DATATYPE_INT8:
      return 1;
    case DATATYPE_INT16:
      return 2;
    case DATATYPE_INT32:
      return 4;
    case DATATYPE_INT64:
      return 8;
    case DATATYPE_STRING:
      return strlen((char*)ptr) + 1;
    default:
      return -1;
  }
}
