#ifndef EJ_CONSTANTS_H
#define EJ_CONSTANTS_H

/* Maximum number of attribute columns per row. */
#define MAX_ATTRIBUTES 64

/* Maximum columns per table (for schema names). */
#define MAX_COLS 64

/* Maximum length of a column name (including null terminator). */
#define MAX_COL_NAME 32

/* AES-CTR parameters. */
#define AES_KEY_SIZE   16   /* 128-bit key */
#define AES_BLOCK_SIZE 16

/* Sentinel for unset integer metadata fields. */
#define NULL_VALUE INT32_MAX

/* Entry field_type values. */
#define SORT_PADDING 0
#define SOURCE       1
#define START        2
#define END          3
#define TARGET       4
#define DIST_PADDING 5

/* equality_type values (for boundary conditions). */
#define NONE 0
#define EQ   1
#define NEQ  2

/* Join attribute safe range (avoids int32 overflow on ±INF arithmetic). */
#define JOIN_ATTR_MIN     (-1073741820)
#define JOIN_ATTR_MAX      (1073741820)
#define JOIN_ATTR_NEG_INF (-1073741821)
#define JOIN_ATTR_POS_INF  (1073741821)

/* Window size for linear-pass operations. */
#define WINDOW_SIZE 2

/* Oblivious sort: k-way merge parameters. */
#define MERGE_SORT_K 8

/* AKS distribute: in-EPC working buffer (entries). */
#define AKS_BUFFER 512

#endif /* EJ_CONSTANTS_H */
