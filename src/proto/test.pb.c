/* Automatically generated nanopb constant definitions */
/* Generated by nanopb-0.3.9.1 at Fri May 25 16:12:28 2018. */

#include "test.pb.h"

/* @@protoc_insertion_point(includes) */
#if PB_PROTO_HEADER_VERSION != 30
#error Regenerate this file with the current version of nanopb generator.
#endif



const pb_field_t test_message_fields[4] = {
    PB_FIELD(  1, INT32   , REQUIRED, STATIC  , FIRST, test_message, value1, value1, 0),
    PB_FIELD(  2, UINT64  , REQUIRED, STATIC  , OTHER, test_message, value2, value1, 0),
    PB_FIELD(  3, INT32   , REPEATED, CALLBACK, OTHER, test_message, value_arr, value2, 0),
    PB_LAST_FIELD
};



/* @@protoc_insertion_point(eof) */
