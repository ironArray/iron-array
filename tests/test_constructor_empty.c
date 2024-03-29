/*
 * Copyright ironArray SL 2021.
 *
 * All rights reserved.
 *
 * This software is the confidential and proprietary information of ironArray SL
 * ("Confidential Information"). You shall not disclose such Confidential
 * Information and shall use it only in accordance with the terms of the license agreement.
 *
 */

#include <src/iarray_private.h>
#include <libiarray/iarray.h>

static ina_rc_t test_empty(iarray_context_t *ctx,
                           iarray_data_type_t dtype,
                           int8_t ndim,
                           const int64_t *shape,
                           const int64_t *cshape,
                           const int64_t *bshape, bool contiguous, char *urlpath)
{
    iarray_dtshape_t xdtshape;

    xdtshape.dtype = dtype;
    xdtshape.ndim = ndim;
    for (int i = 0; i < ndim; ++i) {
        xdtshape.shape[i] = shape[i];
    }

    iarray_storage_t store;
    store.contiguous = contiguous;
    store.urlpath = urlpath;
    for (int i = 0; i < ndim; ++i) {
        store.chunkshape[i] = cshape[i];
        store.blockshape[i] = bshape[i];
    }

    // Empty array
    iarray_container_t *c_x;
    blosc2_remove_urlpath(store.urlpath);
    INA_TEST_ASSERT_SUCCEED(iarray_empty(ctx, &xdtshape, &store, &c_x));


    int64_t nbytes;
    int64_t cbytes;
    INA_TEST_ASSERT_SUCCEED(iarray_container_info(c_x, &nbytes, &cbytes));
    INA_TEST_ASSERT_SUCCEED(cbytes <= nbytes);

    iarray_container_free(ctx, &c_x);
    blosc2_remove_urlpath(store.urlpath);

    return INA_SUCCESS;

}

INA_TEST_DATA(constructor_empty) {
    iarray_context_t *ctx;
};

INA_TEST_SETUP(constructor_empty)
{
    iarray_init();

    iarray_config_t cfg = IARRAY_CONFIG_DEFAULTS;
    INA_TEST_ASSERT_SUCCEED(iarray_context_new(&cfg, &data->ctx));
}

INA_TEST_TEARDOWN(constructor_empty)
{
    iarray_context_free(&data->ctx);
    iarray_destroy();
}

INA_TEST_FIXTURE(constructor_empty, 1_d)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_DOUBLE;
    int8_t ndim = 1;
    int64_t shape[] = {500};
    int64_t cshape[] = {100};
    int64_t bshape[] = {26};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, false, NULL));
}

INA_TEST_FIXTURE(constructor_empty, 1_f_1)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_FLOAT;
    int8_t ndim = 1;
    int64_t shape[] = {667};
    int64_t cshape[] = {252};
    int64_t bshape[] = {34};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, true, NULL));
}

INA_TEST_FIXTURE(constructor_empty, 2_ll)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_INT64;
    int8_t ndim = 2;
    int64_t shape[] = {15, 1112};
    int64_t cshape[] = {3, 12};
    int64_t bshape[] = {3, 12};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, false, "arr.iarr"));
}

INA_TEST_FIXTURE(constructor_empty, 5_ui)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_UINT32;
    int8_t ndim = 5;
    int64_t shape[] = {22, 13, 16, 10, 7};
    int64_t cshape[] = {11, 12, 8, 5, 3};
    int64_t bshape[] = {3, 4, 2, 4, 3};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, false, NULL));
}

INA_TEST_FIXTURE(constructor_empty, 1_ll)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_INT64;
    int8_t ndim = 1;
    int64_t shape[] = {500};
    int64_t cshape[] = {100};
    int64_t bshape[] = {26};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, false, NULL));
}

INA_TEST_FIXTURE(constructor_empty, 1_i_1)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_INT32;
    int8_t ndim = 1;
    int64_t shape[] = {667};
    int64_t cshape[] = {252};
    int64_t bshape[] = {34};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, true, NULL));
}

INA_TEST_FIXTURE(constructor_empty, 2_s)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_INT16;
    int8_t ndim = 2;
    int64_t shape[] = {15, 1112};
    int64_t cshape[] = {3, 12};
    int64_t bshape[] = {3, 12};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, false, "arr.iarr"));
}

INA_TEST_FIXTURE(constructor_empty, 5_sc)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_INT8;
    int8_t ndim = 5;
    int64_t shape[] = {22, 13, 16, 10, 7};
    int64_t cshape[] = {11, 12, 8, 5, 3};
    int64_t bshape[] = {3, 4, 2, 4, 3};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, false, NULL));
}

INA_TEST_FIXTURE(constructor_empty, 1_ull)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_UINT64;
    int8_t ndim = 1;
    int64_t shape[] = {500};
    int64_t cshape[] = {100};
    int64_t bshape[] = {26};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, false, NULL));
}

INA_TEST_FIXTURE(constructor_empty, 1_ui_1)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_UINT32;
    int8_t ndim = 1;
    int64_t shape[] = {667};
    int64_t cshape[] = {252};
    int64_t bshape[] = {34};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, true, NULL));
}

INA_TEST_FIXTURE(constructor_empty, 2_us)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_UINT16;
    int8_t ndim = 2;
    int64_t shape[] = {15, 1112};
    int64_t cshape[] = {3, 12};
    int64_t bshape[] = {3, 12};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, false, "arr.iarr"));
}

INA_TEST_FIXTURE(constructor_empty, 5_uc)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_UINT8;
    int8_t ndim = 5;
    int64_t shape[] = {22, 13, 16, 10, 7};
    int64_t cshape[] = {11, 12, 8, 5, 3};
    int64_t bshape[] = {3, 4, 2, 4, 3};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, false, NULL));
}

INA_TEST_FIXTURE(constructor_empty, 1_b)
{
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_BOOL;
    int8_t ndim = 1;
    int64_t shape[] = {500};
    int64_t cshape[] = {100};
    int64_t bshape[] = {26};

    INA_TEST_ASSERT_SUCCEED(test_empty(data->ctx, dtype, ndim, shape, cshape, bshape, false, NULL));
}
