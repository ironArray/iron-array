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


int main()
{
    int8_t ndim = 2;
    iarray_data_type_t dtype = IARRAY_DATA_TYPE_DOUBLE;
    int64_t shape[] = {10, 10};
    int64_t pshape[] = {2, 3};

    iarray_config_t cfg = IARRAY_CONFIG_DEFAULTS;
    iarray_context_t *ctx;
    IARRAY_FAIL_IF_ERROR(iarray_context_new(&cfg, &ctx));

    iarray_dtshape_t dtshape;
    dtshape.ndim = ndim;
    dtshape.dtype = dtype;
    for (int i = 0; i < ndim; ++i) {
        dtshape.shape[i] = shape[i];
        dtshape.pshape[i] = pshape[i];
    }

    iarray_store_properties_t store;
    store.backend = IARRAY_STORAGE_BLOSC;
    store.enforce_frame = false;
    store.filename = NULL;

    iarray_container_t *cont;
    IARRAY_FAIL_IF_ERROR(iarray_container_new(ctx, &dtshape, &store, 0, &cont));

    iarray_iter_write_t *iter_w;
    iarray_iter_write_value_t val_w;
    IARRAY_FAIL_IF_ERROR(iarray_iter_write_new(ctx, &iter_w, cont, &val_w));

    while (INA_SUCCEED(iarray_iter_write_has_next(iter_w))) {
        IARRAY_FAIL_IF_ERROR(iarray_iter_write_next(iter_w));
        ((double *) val_w.elem_pointer)[0] = (double) val_w.elem_flat_index;
    }
    iarray_iter_write_free(&iter_w);
    IARRAY_FAIL_IF(ina_err_get_rc() != INA_RC_PACK(IARRAY_ERR_END_ITER, 0));

    int64_t start[] = {2, 3};
    int64_t stop[] = {9, 7};

    iarray_container_t *cout;
    iarray_get_slice(ctx, cont, start, stop, true, pshape, &store, 0, &cout);
    
    int64_t cout_size = 1;
    for (int i = 0; i < cout->dtshape->ndim; ++i) {
        cout_size *= cout->dtshape->shape[i];
    }
    
    iarray_iter_read_t *iter;
    iarray_iter_read_value_t val;
    IARRAY_FAIL_IF(iarray_iter_read_new(ctx, &iter, cout, &val));
    while (INA_SUCCEED(iarray_iter_read_has_next(iter))) {
        IARRAY_FAIL_IF(iarray_iter_read_next(iter));
        printf("%f\n", ((double *) val.elem_pointer)[0]);
    }
    iarray_iter_read_free(&iter);
    IARRAY_FAIL_IF(ina_err_get_rc() != INA_RC_PACK(IARRAY_ERR_END_ITER, 0));

    return INA_SUCCESS;

    fail:
    iarray_iter_write_free(&iter_w);
    iarray_iter_read_free(&iter);
    iarray_container_free(ctx, &cont);
    iarray_context_free(&ctx);
    return ina_err_get_rc();

}
