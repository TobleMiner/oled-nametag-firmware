#ifndef _DATATYPES_H_
#define _DATATYPES_H_

#include <sys/types.h>

enum {
  DATATYPE_INT8,
  DATATYPE_INT16,
  DATATYPE_INT32,
  DATATYPE_INT64,
  DATATYPE_STRING,
};

ssize_t datatype_get_size(int type, void* ptr);

#endif
