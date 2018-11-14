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

#include <libiarray/iarray.h>
#include <iarray_private.h>

#include <tests/iarray_test.h>

static ina_rc_t test_gemm(iarray_container_t *c_x, iarray_container_t *c_y, iarray_container_t *c_out, iarray_container_t *c_res) 
{
    INA_TEST_ASSERT_SUCCEED(iarray_gemm(c_x, c_y, c_out));
    if (!iarray_equal_data(c_out, c_res)) {
        return INA_ERROR(INA_ERR_FAILED);
    }
    return INA_SUCCESS;
}

static ina_rc_t _execute_iarray_gemm(iarray_context_t *ctx, 
                                     iarray_data_type_t dtype, 
                                     size_t type_size,
                                     int M, 
                                     int K, 
                                     int N, 
                                     int P)
{
    void *buffer_x;
    void *buffer_y;
    void *buffer_r;
    size_t buffer_x_len;
    size_t buffer_y_len;
    size_t buffer_r_len;
    
    buffer_x_len = type_size * M * K;
    buffer_y_len = type_size * K * N;
    buffer_r_len = type_size * M * N;
    buffer_x = ina_mem_alloc(buffer_x_len);
    buffer_y = ina_mem_alloc(buffer_y_len);
    buffer_r = ina_mem_alloc(buffer_r_len);

    if (type_size == sizeof(float)) {
        ffill_buf((float*)buffer_x, M * K);
        ffill_buf((float*)buffer_y, K * N);
        fmm_mul(M, K, N, (float*)buffer_x, (float*)buffer_y, (float*)buffer_r);
    }
    else {
        dfill_buf((double*)buffer_x, M * K);
        dfill_buf((double*)buffer_y, K * N);
        dmm_mul(M, K, N, (double*)buffer_x, (double*)buffer_y, (double*)buffer_r);
    }

    iarray_dtshape_t xshape;
    iarray_dtshape_t yshape;
    iarray_dtshape_t oshape;
    iarray_dtshape_t rshape;

    xshape.dtype = dtype;
    xshape.ndim = 2;
    xshape.shape[0] = K;
    xshape.shape[1] = M;
    xshape.partshape[0] = P;
    xshape.partshape[1] = P;

    yshape.dtype = dtype;
    yshape.ndim = 2;
    yshape.shape[0] = N;
    yshape.shape[1] = K;
    yshape.partshape[0] = P;
    yshape.partshape[1] = P;

    oshape.dtype = dtype;
    oshape.ndim = 2;
    oshape.shape[0] = N;
    oshape.shape[1] = M;
    oshape.partshape[0] = P;
    oshape.partshape[1] = P;

    rshape.dtype = dtype;
    rshape.ndim = 2;
    rshape.shape[0] = N;
    rshape.shape[1] = M;
    rshape.partshape[0] = N;
    rshape.partshape[1] = M;

    iarray_container_t *c_x;
    iarray_container_t *c_y;
    iarray_container_t *c_out;
    iarray_container_t *c_res;

    INA_TEST_ASSERT_SUCCEED(iarray_from_buffer(ctx, &xshape, buffer_x, buffer_x_len, NULL, 0, &c_x));
    INA_TEST_ASSERT_SUCCEED(iarray_from_buffer(ctx, &yshape, buffer_y, buffer_y_len, NULL, 0, &c_y));
    INA_TEST_ASSERT_SUCCEED(iarray_from_buffer(ctx, &rshape, buffer_r, buffer_r_len, NULL, 0, &c_res));
    INA_TEST_ASSERT_SUCCEED(iarray_container_new(ctx, &oshape, NULL, 0, &c_out));

    INA_TEST_ASSERT_SUCCEED(test_gemm(c_x, c_y, c_out, c_res));

    iarray_container_free(ctx, &c_x);
    iarray_container_free(ctx, &c_y);
    iarray_container_free(ctx, &c_out);
    iarray_container_free(ctx, &c_res);

    ina_mem_free(buffer_x);
    ina_mem_free(buffer_y);
    ina_mem_free(buffer_r);

    return INA_SUCCESS;
}

INA_TEST_DATA(gemm)
{
    iarray_context_t *ctx;
};

INA_TEST_SETUP(gemm)
{
    iarray_init();

    iarray_config_t cfg = IARRAY_CONFIG_DEFAULTS;
    cfg.compression_codec = IARRAY_COMPRESSION_LZ4;
    cfg.flags = IARRAY_EXPR_EVAL_CHUNK;

    iarray_context_new(&cfg, &data->ctx);
}


INA_TEST_TEARDOWN(gemm)
{
    iarray_context_free(&data->ctx);
    iarray_destroy();
}

INA_TEST_FIXTURE(gemm, double_data)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_DOUBLE;
    size_t type_size = sizeof(double);

    int M = 163;
    int K = 135;
    int N = 94;
    int P = 24;    

    INA_TEST_ASSERT_SUCCEED(_execute_iarray_gemm(data->ctx, dtype, type_size, M, K, N, P));
}


INA_TEST_FIXTURE(gemm, float_data)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_FLOAT;
    size_t type_size = sizeof(float);

    int M = 123;
    int K = 50;
    int N = 75;
    int P = 10;
    
    INA_TEST_ASSERT_SUCCEED(_execute_iarray_gemm(data->ctx, dtype, type_size, M, K, N, P));

}
