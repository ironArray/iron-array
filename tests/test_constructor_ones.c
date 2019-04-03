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

static ina_rc_t test_ones(iarray_context_t *ctx,
                          iarray_data_type_t dtype,
                          size_t type_size,
                          int8_t ndim,
                          const int64_t *shape,
                          const int64_t *pshape)
{
    iarray_dtshape_t xdtshape;

    xdtshape.dtype = dtype;
    xdtshape.ndim = ndim;
    for (int i = 0; i < ndim; ++i) {
        xdtshape.shape[i] = shape[i];
        xdtshape.pshape[i] = pshape[i];
    }

    int64_t buf_size = 1;
    for (int j = 0; j < ndim; ++j) {
        buf_size *= shape[j];
    }

    uint8_t *buf_dest = malloc((size_t)buf_size * type_size);

    iarray_container_t *c_x;

    INA_TEST_ASSERT_SUCCEED(iarray_ones(ctx, &xdtshape, NULL, 0, &c_x));

    iarray_to_buffer(ctx, c_x, buf_dest, (size_t)buf_size);

    if (dtype == IARRAY_DATA_TYPE_DOUBLE) {
        double *buff = (double *) buf_dest;
        for (int64_t i = 0; i < buf_size; ++i) {
            INA_TEST_ASSERT_EQUAL_FLOATING(buff[i], 1);
        }
    } else {
        float *buff = (float *) buf_dest;
        for (int64_t i = 0; i < buf_size; ++i) {
            INA_TEST_ASSERT_EQUAL_FLOATING(buff[i], 1);
        }
    }

    return INA_SUCCESS;
}

INA_TEST_DATA(constructor_ones) {
    iarray_context_t *ctx;
};

INA_TEST_SETUP(constructor_ones)
{
    iarray_init();

    iarray_config_t cfg = IARRAY_CONFIG_DEFAULTS;
    cfg.compression_codec = IARRAY_COMPRESSION_LZ4;
    cfg.eval_flags = IARRAY_EXPR_EVAL_CHUNK;

    iarray_context_new(&cfg, &data->ctx);
}

INA_TEST_TEARDOWN(constructor_ones)
{
    iarray_context_free(&data->ctx);
    iarray_destroy();
}

INA_TEST_FIXTURE(constructor_ones, 2_d)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_DOUBLE;
    size_t type_size = sizeof(double);

    int8_t ndim = 2;
    int64_t shape[] = {10, 10};
    int64_t pshape[] = {3, 4};

    INA_TEST_ASSERT_SUCCEED(test_ones(data->ctx, dtype, type_size, ndim, shape, pshape));
}

INA_TEST_FIXTURE(constructor_ones, 4_f_p)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_FLOAT;
    size_t type_size = sizeof(float);

    int8_t ndim = 4;
    int64_t shape[] = {10, 10, 10, 10};
    int64_t pshape[] = {0, 0, 0, 0};

    INA_TEST_ASSERT_SUCCEED(test_ones(data->ctx, dtype, type_size, ndim, shape, pshape));
}

INA_TEST_FIXTURE(constructor_ones, 5_d)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_DOUBLE;
    size_t type_size = sizeof(double);

    int8_t ndim = 5;
    int64_t shape[] = {10, 10, 10, 10, 10};
    int64_t pshape[] = {3, 4, 6, 3, 3};

    INA_TEST_ASSERT_SUCCEED(test_ones(data->ctx, dtype, type_size, ndim, shape, pshape));
}

INA_TEST_FIXTURE(constructor_ones, 7_f_p)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_FLOAT;
    size_t type_size = sizeof(float);

    int8_t ndim = 7;
    int64_t shape[] = {10, 10, 10, 10, 10, 10, 10};
    int64_t pshape[] = {4, 3, 6, 2, 3, 3, 2};

    INA_TEST_ASSERT_SUCCEED(test_ones(data->ctx, dtype, type_size, ndim, shape, pshape));
}