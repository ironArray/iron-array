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

#ifndef _IARRAY_TEST_COMMON_H_
#define _IARRAY_TEST_COMMON_H_

#include <libiarray/iarray.h>

static void ffill_buf(float *x, size_t nitems)
{

    /* Fill with even values between 0 and 10 */
    float incx = (float) 10. / nitems;

    for (size_t i = 0; i < nitems; i++) {
        x[i] = incx * i;
    }
}

static void dfill_buf(double *x, size_t nitems)
{

    /* Fill with even values between 0 and 10 */
    double incx = 10. / nitems;

    for (size_t i = 0; i < nitems; i++) {
        x[i] = incx * i;
    }
}

static ina_rc_t _iarray_test_container_dbl_buffer_cmp(iarray_context_t *ctx, iarray_container_t *c, const double *buffer, size_t buffer_len)
{
    double *bufcmp = ina_mem_alloc(buffer_len);

    INA_RETURN_IF_FAILED(iarray_to_buffer(ctx, c, bufcmp, buffer_len, IARRAY_STORAGE_ROW_WISE));

    size_t len = buffer_len / sizeof(double);
    for (size_t i = 0; i < len; ++i) {
        double vdiff = fabs(buffer[i] - bufcmp[i]);
        if (vdiff > 1e-6) {
            INA_TEST_MSG("Values differ in (%d nelem) (diff: %f)\n", i, vdiff);
            INA_FAIL_IF(1);
        }
    }
    ina_mem_free(bufcmp);
    return 1;

fail:
    ina_mem_free(bufcmp);
    return 0;
}

static int fmm_mul(size_t M, size_t K, size_t N, float const *a, float const *b, float *c)
{
    for (size_t m = 0; m < M; ++m) {
        for (size_t n= 0; n < N; ++n) {
            for (size_t k = 0; k < K; ++k) {
                c[m * N + n] += a[m * K + k] * b[k * N + n];
            }
        }
    }
    return 0;
}

static int dmm_mul(size_t M, size_t K, size_t N, double const *a, double const *b, double *c)
{
    for (size_t m = 0; m < M; ++m) {
        for (size_t n= 0; n < N; ++n) {
            for (size_t k = 0; k < K; ++k) {
                c[m * N + n] += a[m * K + k] * b[k * N + n];
            }
        }
    }
    return 0;
}

static int fmv_mul(size_t M, size_t K, float const *a, float const *b, float *c)
{
    for (size_t m = 0; m < M; ++m) {
        for (size_t k = 0; k < K; ++k) {
            c[m] += a[m * K + k] * b[k];
        }

    }
    return 0;
}

static int dmv_mul(size_t M, size_t K, double const *a, double const *b, double *c)
{
    for (size_t m = 0; m < M; ++m) {
        for (size_t k = 0; k < K; ++k) {
            c[m] += a[m * K + k] * b[k];
        }

    }
    return 0;
}

static ina_rc_t _iarray_test_container_dbl_buffer_cmp(iarray_context_t *ctx, iarray_container_t *c, const double *buffer, size_t buffer_len)
{
    double *bufcmp = ina_mem_alloc(buffer_len);
    
    INA_RETURN_IF_FAILED(iarray_to_buffer(ctx, c, bufcmp, buffer_len, IARRAY_STORAGE_ROW_WISE));
    
    size_t len = buffer_len / sizeof(double);
    for (size_t i = 0; i < len; ++i) {
        double vdiff = fabs(buffer[i] - bufcmp[i]);
        if (vdiff > 1e-6) {
            INA_TEST_MSG("Values differ in (%d nelem) (diff: %f)\n", i, vdiff);
            INA_FAIL_IF(1);
        }
    }
    ina_mem_free(bufcmp);
    return 1;

fail:
    ina_mem_free(bufcmp);
    return 0;
}

#endif
