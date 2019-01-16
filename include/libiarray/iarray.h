/*
 * Copyright INAOS GmbH, Thalwil, 2018.
 * Copyright Francesc Alted, 2018.
 *
 * All rights reserved.
 *
 * This software is the confidential and proprietary information of INAOS GmbH
 * and Francesc Alted ("Confidential Information"). You shall not disclose such Confidential
 * Information and shall use it only in accordance with the terms of the license agreement.
 *
 */
#ifndef _IARRAY_H_
#define _IARRAY_H_

#include <libinac/lib.h>

#define IARRAY_DIMENSION_MAX 8 /* A fixed size simplifies the code and should be enough for most IronArray cases */

typedef struct iarray_context_s iarray_context_t;
typedef struct iarray_container_s iarray_container_t;
typedef struct iarray_iter_write_s iarray_iter_write_t;
typedef struct iarray_iter_write_part_s iarray_iter_write_part_t;
typedef struct iarray_iter_write_s iarray_iter_read_t;
typedef struct iarray_iter_read_block_s iarray_iter_read_block_t;

typedef struct iarray_expression_s iarray_expression_t;

typedef enum iarray_random_rng_e {
    IARRAY_RANDOM_RNG_MERSENNE_TWISTER,
    IARRAY_RANDOM_RNG_SOBOL,
} iarray_random_rng_t;

typedef enum iarray_data_type_e {
    IARRAY_DATA_TYPE_DOUBLE,
    IARRAY_DATA_TYPE_FLOAT,
    IARRAY_DATA_TYPE_MAX  // marker; must be the last entry
} iarray_data_type_t;

typedef enum iarray_storage_format_e {
    IARRAY_STORAGE_ROW_WISE = 0,
    IARRAY_STORAGE_COL_WISE
} iarray_storage_format_t;

typedef struct iarray_store_properties_s {
    const char *id;
} iarray_store_properties_t;

typedef enum iarray_config_flags_e {
    IARRAY_EXPR_EVAL_BLOCK = 0x1,
    IARRAY_EXPR_EVAL_CHUNK = 0x2,
    IARRAY_COMP_SHUFFLE    = 0x4,
    IARRAY_COMP_BITSHUFFLE = 0x8,
    IARRAY_COMP_DELTA      = 0x10,
    IARRAY_COMP_TRUNC_PREC = 0x20,
} iarray_config_flags_t;

typedef enum iarray_bind_flags_e {
    IARRAY_BIND_UPDATE_CONTAINER = 0x1
} iarray_bind_flags_t;

typedef enum iarray_container_flags_e {
    IARRAY_CONTAINER_PERSIST = 0x1
} iarray_container_flags_t;

typedef enum iarray_operator_hint_e {
    IARRAY_OPERATOR_GENERAL = 0,
    IARRAY_OPERATOR_SYMMETRIC,
    IARRAY_OPERATOR_TRIANGULAR
} iarray_operator_hint_t;

typedef enum iarray_compression_codec_e {
    IARRAY_COMPRESSION_BLOSCLZ = 0,
    IARRAY_COMPRESSION_LZ4,
    IARRAY_COMPRESSION_LZ4HC,
    IARRAY_COMPRESSION_SNAPPY,
    IARRAY_COMPRESSION_ZLIB,
    IARRAY_COMPRESSION_ZSTD,
    IARRAY_COMPRESSION_LIZARD
} iarray_compression_codec_t;

typedef enum iarray_linalg_norm_e {
    IARRAY_LINALG_NORM_NONE,
    IARRAY_LINALG_NORM_FROBENIUS,
    IARRAY_LINALG_NORM_NUCLEAR,
    IARRAY_LINALG_NORM_MAX_ROWS,
    IARRAY_LINALG_NORM_MAX_COLS,
    IARRAY_LINALG_NORM_MIN_ROWS,
    IARRAY_LINALG_NORM_MIN_COLS,
    IARRAY_LINALG_NORM_SING_MAX,
    IARRAY_LINALG_NORM_SING_MIN
} iarray_linalg_norm_t;

typedef struct iarray_config_s {
    iarray_compression_codec_t compression_codec;
    int compression_level;
    int flags;
    int max_num_threads; /* Maximum number of threads to use */
    uint8_t fp_mantissa_bits; /* Only useful together with flag: IARRAY_COMP_TRUNC_PREC */
    int blocksize; /* Advanced Tuning Parameter */
} iarray_config_t;

typedef struct iarray_dtshape_s {
    iarray_data_type_t dtype;
    uint8_t ndim;     /* IF ndim = 0 THEN it is a scalar */
    uint64_t shape[IARRAY_DIMENSION_MAX];
    uint64_t pshape[IARRAY_DIMENSION_MAX]; /* Partition-Shape, optional in the future */
} iarray_dtshape_t;

typedef struct iarray_iter_write_value_s {
    void *pointer;
    uint64_t *index;
    uint64_t nelem;
} iarray_iter_write_value_t;

typedef struct iarray_iter_write_value_s iarray_iter_read_value_t;

typedef struct iarray_iter_write_part_value_s {
    void *pointer;
    uint64_t *part_index;
    uint64_t *elem_index;
    uint64_t nelem;
    uint64_t* part_shape;
} iarray_iter_write_part_value_t;


typedef struct iarray_iter_read_block_value_s {
    void *pointer;
    uint64_t *block_index;
    uint64_t *elem_index;
    uint64_t nelem;
    uint64_t* block_shape;
} iarray_iter_read_block_value_t;

typedef struct iarray_slice_param_s {
    int axis;
    int idx;
} iarray_slice_param_t;

typedef struct iarray_random_ctx_s iarray_random_ctx_t;

static const iarray_config_t IARRAY_CONFIG_DEFAULTS = { IARRAY_COMPRESSION_BLOSCLZ, 5, 0, 1, 0, 0 };
static const iarray_config_t IARRAY_CONFIG_NO_COMPRESSION = { IARRAY_COMPRESSION_BLOSCLZ, 0, 0, 1, 0, 0 };

INA_API(ina_rc_t) iarray_init();
INA_API(void) iarray_destroy();

INA_API(ina_rc_t) iarray_context_new(iarray_config_t *cfg, iarray_context_t **ctx);
INA_API(void) iarray_context_free(iarray_context_t **ctx);

INA_API(ina_rc_t) iarray_partition_advice(iarray_data_type_t dtype, int *max_nelem, int *min_nelem);

INA_API(ina_rc_t) iarray_random_ctx_new(iarray_context_t *ctx,
                                        uint32_t seed,
                                        iarray_random_rng_t rng,
                                        iarray_random_ctx_t **rng_ctx);

INA_API(void) iarray_random_ctx_free(iarray_context_t *ctx, iarray_random_ctx_t **rng_ctx);

INA_API(ina_rc_t) iarray_container_new(iarray_context_t *ctx,
                                       iarray_dtshape_t *dtshape,
                                       iarray_store_properties_t *store,
                                       int flags,
                                       iarray_container_t **container);

INA_API(ina_rc_t) iarray_arange(iarray_context_t *ctx,
                                iarray_dtshape_t *dtshape,
                                double start,
                                double stop,
                                double step,
                                iarray_store_properties_t *store,
                                int flags,
                                iarray_container_t **container);

INA_API(ina_rc_t) iarray_linspace(iarray_context_t *ctx,
                                  iarray_dtshape_t *dtshape,
                                  int64_t nelem,
                                  double start,
                                  double stop,
                                  iarray_store_properties_t *store,
                                  int flags,
                                  iarray_container_t **container);

INA_API(ina_rc_t) iarray_zeros(iarray_context_t *ctx,
                               iarray_dtshape_t *dtshape,
                               iarray_store_properties_t *store,
                               int flags,
                               iarray_container_t **container);

INA_API(ina_rc_t) iarray_ones(iarray_context_t *ctx,
                              iarray_dtshape_t *dtshape,
                              iarray_store_properties_t *store,
                              int flags,
                              iarray_container_t **container);

INA_API(ina_rc_t) iarray_fill_float(iarray_context_t *ctx,
                                    iarray_dtshape_t *dtshape,
                                    float value,
                                    iarray_store_properties_t *store,
                                    int flags,
                                    iarray_container_t **container);

INA_API(ina_rc_t) iarray_fill_double(iarray_context_t *ctx,
                                     iarray_dtshape_t *dtshape,
                                     double value,
                                     iarray_store_properties_t *store,
                                     int flags,
                                     iarray_container_t **container);

INA_API(ina_rc_t) iarray_random_rand(iarray_context_t *ctx,
                                     iarray_dtshape_t *dtshape,
                                     iarray_random_ctx_t *rand_ctx,
                                     iarray_store_properties_t *store,
                                     int flags,
                                     iarray_container_t **container);

INA_API(ina_rc_t) iarray_random_randn(iarray_context_t *ctx,
                                      iarray_dtshape_t *dtshape,
                                      iarray_random_ctx_t *rand_ctx,
                                      iarray_store_properties_t *store,
                                      int flags,
                                      iarray_container_t **container);

INA_API(ina_rc_t) iarray_random_beta(iarray_context_t *ctx,
                                     iarray_dtshape_t *dtshape,
                                     iarray_random_ctx_t *rand_ctx,
                                     iarray_store_properties_t *store,
                                     int flags,
                                     iarray_container_t **container);

INA_API(ina_rc_t) iarray_random_lognormal(iarray_context_t *ctx,
                                          iarray_dtshape_t *dtshape,
                                          iarray_random_ctx_t *rand_ctx,
                                          iarray_store_properties_t *store,
                                          int flags,
                                          iarray_container_t **container);

INA_API(ina_rc_t) iarray_slice(iarray_context_t *ctx,
                               iarray_container_t *c,
                               int64_t *start,
                               int64_t *stop,
                               iarray_dtshape_t *dtshape,
                               iarray_store_properties_t *store,
                               int flags,
                               iarray_container_t **container);

INA_API(ina_rc_t) iarray_slice_buffer(iarray_context_t *ctx,
                                      iarray_container_t *c,
                                      int64_t *start,
                                      int64_t *stop,
                                      void *buffer,
                                      uint64_t buflen);

INA_API(ina_rc_t) iarray_from_file(iarray_context_t *ctx,
                                   iarray_store_properties_t *store,
                                   iarray_container_t **container);

INA_API(ina_rc_t) iarray_squeeze(iarray_context_t *ctx,
                                 iarray_container_t *container);

INA_API(ina_rc_t) iarray_from_buffer(iarray_context_t *ctx,
                                     iarray_dtshape_t *dtshape,
                                     void *buffer,
                                     size_t buffer_len,
                                     iarray_store_properties_t *store,
                                     int flags,
                                     iarray_container_t **container);

INA_API(ina_rc_t) iarray_to_buffer(iarray_context_t *ctx,
                                   iarray_container_t *container,
                                   void *buffer,
                                   size_t buffer_len);


INA_API(ina_rc_t) iarray_container_dtshape_equal(iarray_dtshape_t *a, iarray_dtshape_t *b);
INA_API(ina_rc_t) iarray_container_info(iarray_container_t *c, uint64_t *nbytes, uint64_t *cbytes);

INA_API(void) iarray_container_free(iarray_context_t *ctx, iarray_container_t **container);

/* Comparison operators -> not supported yet as we only support float and double and return would be int8 */
INA_API(ina_rc_t) iarray_container_gt(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_container_lt(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_container_gte(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_container_lte(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_container_eq(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);

INA_API(ina_rc_t) iarray_container_almost_equal(iarray_container_t *a, iarray_container_t *b, double tol);

INA_API(ina_rc_t) iarray_container_is_symmetric(iarray_container_t *a);
INA_API(ina_rc_t) iarray_container_is_triangular(iarray_container_t *a);

/* Logical operators -> not supported yet as we only support float and double and return would be int8 */
INA_API(ina_rc_t) iarray_operator_and(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_or(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_xor(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_nand(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_not(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);

/* Arithmetic operators -> element-wise */
INA_API(ina_rc_t) iarray_operator_add(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_sub(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_mul(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_div(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);

/* linear algebra */
INA_API(ina_rc_t) iarray_linalg_transpose(iarray_context_t *ctx, iarray_container_t *a);
INA_API(ina_rc_t) iarray_linalg_inverse(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_linalg_matmul(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result,
                                       iarray_operator_hint_t hint);
INA_API(ina_rc_t) iarray_linalg_dot(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result, iarray_operator_hint_t hint);
INA_API(ina_rc_t) iarray_linalg_det(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_linalg_eigen(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_linalg_norm(iarray_context_t *ctx, iarray_container_t *a, iarray_linalg_norm_t ord, iarray_container_t *result);
INA_API(ina_rc_t) iarray_linalg_solve(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_linalg_lstsq(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_linalg_svd(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_linalg_qr(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result); // Not clear to which MKL function we need to map
INA_API(ina_rc_t) iarray_linalg_lu(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result); // ?getrf (MKL) - Not clear to which MKL function we need to map
INA_API(ina_rc_t) iarray_linalg_cholesky(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);

/* Function operators -> element-wise */
INA_API(ina_rc_t) iarray_operator_abs(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_acos(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_asin(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_atanc(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_atan2(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_ceil(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_cos(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_cosh(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_exp(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_floor(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_log(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_log10(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_pow(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *b, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_sin(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_sinh(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_sqrt(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_tan(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_tanh(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_erf(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_erfc(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_cdfnorm(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_erfinv(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_erfcinv(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_cdfnorminv(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_lgamma(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_tgamma(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_expint1(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_operator_cumsum(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);

/* Reductions */
INA_API(ina_rc_t) iarray_reduction_sum(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_reduction_min(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_reduction_max(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);
INA_API(ina_rc_t) iarray_reduction_mul(iarray_context_t *ctx, iarray_container_t *a, iarray_container_t *result);

/* Iterators */

INA_API(ina_rc_t) iarray_iter_write_new(iarray_context_t *ctx, iarray_container_t *container, iarray_iter_write_t **itr);
INA_API(void) iarray_iter_write_free(iarray_iter_write_t *itr);
INA_API(void) iarray_iter_write_init(iarray_iter_write_t *itr);
INA_API(ina_rc_t) iarray_iter_write_next(iarray_iter_write_t *itr);
INA_API(int) iarray_iter_write_finished(iarray_iter_write_t *itr);
INA_API(void) iarray_iter_write_value(iarray_iter_write_t *itr, iarray_iter_write_value_t *value);

INA_API(ina_rc_t) iarray_iter_write_part_new(iarray_context_t *ctx, iarray_container_t *container,
                                             iarray_iter_write_part_t **itr);
INA_API(void) iarray_iter_write_part_free(iarray_iter_write_part_t *itr);
INA_API(void) iarray_iter_write_part_init(iarray_iter_write_part_t *itr);
INA_API(ina_rc_t) iarray_iter_write_part_next(iarray_iter_write_part_t *itr);
INA_API(int) iarray_iter_write_part_finished(iarray_iter_write_part_t *itr);
INA_API(void) iarray_iter_write_part_value(iarray_iter_write_part_t *itr, iarray_iter_write_part_value_t *value);

INA_API(ina_rc_t) iarray_iter_read_new(iarray_context_t *ctx, iarray_container_t *container,
                                       iarray_iter_read_t **itr);
INA_API(void) iarray_iter_read_free(iarray_iter_read_t *itr);
INA_API(void) iarray_iter_read_init(iarray_iter_read_t *itr);
INA_API(ina_rc_t) iarray_iter_read_next(iarray_iter_read_t *itr);
INA_API(int) iarray_iter_read_finished(iarray_iter_read_t *itr);
INA_API(void) iarray_iter_read_value(iarray_iter_read_t *itr, iarray_iter_read_value_t *val);

INA_API(ina_rc_t) iarray_iter_read_block_new(iarray_context_t *ctx, iarray_container_t *container,
                                             iarray_iter_read_block_t **itr, uint64_t *blockshape);
INA_API(void) iarray_iter_read_block_free(iarray_iter_read_block_t *itr);
INA_API(void) iarray_iter_read_block_init(iarray_iter_read_block_t *itr);
INA_API(ina_rc_t) iarray_iter_read_block_next(iarray_iter_read_block_t *itr);
INA_API(int) iarray_iter_read_block_finished(iarray_iter_read_block_t *itr);
INA_API(void) iarray_iter_read_block_value(iarray_iter_read_block_t *itr, iarray_iter_read_block_value_t *value);

/* Expressions */
INA_API(ina_rc_t) iarray_expr_new(iarray_context_t *ctx, iarray_expression_t **e);
INA_API(void) iarray_expr_free(iarray_context_t *ctx, iarray_expression_t **e);

INA_API(ina_rc_t) iarray_expr_bind(iarray_expression_t *e, const char *var, iarray_container_t *val);
INA_API(ina_rc_t) iarray_expr_bind_scalar_float(iarray_expression_t *e, const char *var, float val);
INA_API(ina_rc_t) iarray_expr_bind_scalar_double(iarray_expression_t *e, const char *var, double val);
INA_API(ina_rc_t) iarray_expr_compile(iarray_expression_t *e, const char *expr);

INA_API(ina_rc_t) iarray_eval(iarray_expression_t *e, iarray_container_t *ret); /* e.g. IARRAY_BIND_UPDATE_CONTAINER */

//FIXME: remove
INA_API(ina_rc_t) iarray_expr_get_mp(iarray_expression_t *e, ina_mempool_t **mp);

#endif
