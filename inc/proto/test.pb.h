/* Automatically generated nanopb header */
/* Generated by nanopb-0.3.9.1 at Thu Sep 24 17:37:07 2020. */

#ifndef PB_TEST_PB_H_INCLUDED
#define PB_TEST_PB_H_INCLUDED
#include <pb.h>

/* @@protoc_insertion_point(includes) */
#if PB_PROTO_HEADER_VERSION != 30
#error Regenerate this file with the current version of nanopb generator.
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Enum definitions */
typedef enum ___message_id {
    __message_id_test_message_id = 256
} __message_id;
#define ___message_id_MIN __message_id_test_message_id
#define ___message_id_MAX __message_id_test_message_id
#define ___message_id_ARRAYSIZE ((__message_id)(__message_id_test_message_id+1))

/* Struct definitions */
typedef struct _test_message {
    int32_t value1;
    uint64_t value2;
    pb_callback_t value_arr;
/* @@protoc_insertion_point(struct:test_message) */
} test_message;

/* Default values for struct fields */

/* Initializer values for message structs */
#define test_message_init_default                {0, 0, {{NULL}, NULL}}
#define test_message_init_zero                   {0, 0, {{NULL}, NULL}}

/* Field tags (for use in manual encoding/decoding) */
#define test_message_value1_tag                  1
#define test_message_value2_tag                  2
#define test_message_value_arr_tag               3

/* Struct field encoding specification for nanopb */
extern const pb_field_t test_message_fields[4];

/* Maximum encoded size of messages (where known) */
/* test_message_size depends on runtime parameters */

/* Message IDs (where set with "msgid" option) */
#ifdef PB_MSGID

#define TEST_MESSAGES \


#endif

#ifdef __cplusplus
} /* extern "C" */
#endif
/* @@protoc_insertion_point(eof) */

#endif
