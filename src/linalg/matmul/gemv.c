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


#include <libiarray/iarray.h>
#include <iarray_private.h>
#include "gemv.h"

typedef struct iarray_parallel_matmul_params_s {
    iarray_container_t *a;
    iarray_container_t *b;
    iarray_container_t *c;
    uint8_t *cache_a;
    uint8_t *cache_b;
} iarray_parallel_matmul_params_t;

static void _gemv_prefilter_block_info(iarray_container_t *c,
                                       int32_t offset,
                                       int32_t size,
                                       int32_t *start) {

    int8_t ndim = c->dtshape->ndim;

    int64_t strides[2] = {0};
    // Element strides (in elements)
    strides[ndim - 1] = 1;
    for (int i = ndim - 2; i >= 0 ; --i) {
        strides[i] = strides[i+1] * c->catarr->blockshape[i+1];
    }

    // Block strides (in blocks)
    int32_t strides_block[IARRAY_DIMENSION_MAX];
    strides_block[ndim - 1] = 1;
    for (int i = ndim - 2; i >= 0 ; --i) {
        strides_block[i] = strides_block[i+1] * (int32_t) (c->catarr->extchunkshape[i+1] /
                                                           c->catarr->blockshape[i+1]);
    }

    // Flattened block number
    int32_t nblock = offset / size;

    // Multidimensional block number
    int32_t nblock_ndim[IARRAY_DIMENSION_MAX];
    for (int i = ndim - 1; i >= 0; --i) {
        if (i != 0) {
            nblock_ndim[i] = (nblock % strides_block[i-1]) / strides_block[i];
        } else {
            nblock_ndim[i] = (int32_t)(nblock % (c->catarr->extchunknitems / c->catarr->blocknitems)) / strides_block[i];
        }
    }

    // Position of the first element of the block (inside current chunk)
    int32_t start_in_chunk[IARRAY_DIMENSION_MAX];
    for (int i = 0; i < ndim; ++i) {
        start_in_chunk[i] = nblock_ndim[i] * c->catarr->blockshape[i];
    }

    for (int i = 0; i < ndim; ++i) {
        start[i] = start_in_chunk[i];
    }
}

static int _gemv_prefilter(blosc2_prefilter_params *pparams) {

    iarray_parallel_matmul_params_t *matmul_params = (iarray_parallel_matmul_params_t *) pparams->user_data;
    iarray_container_t *a = matmul_params->a;
    iarray_container_t *b = matmul_params->b;
    iarray_container_t *c = matmul_params->c;

    // Compute block info
    int32_t start[2] = {0};
    _gemv_prefilter_block_info(matmul_params->c, pparams->out_offset, pparams->out_size, start);

    uint8_t* buffer_a = &matmul_params->cache_a[start[0] * a->dtshape->shape[1] * a->catarr->itemsize];
    uint8_t* buffer_b = &matmul_params->cache_b[start[1] * b->dtshape->shape[0] * b->catarr->itemsize];

    int trans_a = CblasNoTrans;

    int64_t m = c->storage->blockshape[0];
    int64_t n = a->dtshape->shape[1];

    int64_t ld_a = n;

    if (c->dtshape->dtype == IARRAY_DATA_TYPE_DOUBLE) {
        cblas_dgemv(CblasRowMajor, trans_a, (int) m, (int) n,1.0, (double *) buffer_a, ld_a,
                    (double *) buffer_b, 1, 0.0, (double *) pparams->out, 1);
    } else {
        cblas_sgemv(CblasRowMajor, trans_a, (int) m, (int) n,1.0f, (float *) buffer_a, ld_a,
                    (float *) buffer_b, 1, 0.0f, (float *) pparams->out, 1);
    }

    return 0;
}


static ina_rc_t gemv_blosc(iarray_context_t *ctx,
                           iarray_container_t *a,
                           iarray_container_t *b,
                           iarray_container_t *c) {

    // Set up prefilter
    iarray_context_t *prefilter_ctx = ina_mem_alloc(sizeof(iarray_context_t));
    memcpy(prefilter_ctx, ctx, sizeof(iarray_context_t));
    prefilter_ctx->prefilter_fn = (blosc2_prefilter_fn) _gemv_prefilter;
    iarray_parallel_matmul_params_t matmul_params = {0};
    matmul_params.a = a;
    matmul_params.b = b;
    matmul_params.c = c;
    blosc2_prefilter_params pparams = {0};
    pparams.user_data = &matmul_params;
    prefilter_ctx->prefilter_params = &pparams;

    // Init caches

    int64_t cache_size_b = b->catarr->extshape[0] * b->catarr->itemsize;
    uint8_t *cache_b = ina_mem_alloc(cache_size_b);

    int64_t cache_size_a = c->catarr->extchunkshape[0] * a->dtshape->shape[1] * c->catarr->itemsize;

    uint8_t *cache_a = ina_mem_alloc(cache_size_a);

    matmul_params.cache_a = cache_a;
    matmul_params.cache_b = cache_b;

    // Start iterator
    int64_t start_b[1] = {0};
    int64_t stop_b[1] = {b->dtshape->shape[0]};
    int64_t shape_b[1] = {b->catarr->extshape[0]};

    IARRAY_RETURN_IF_FAILED(_iarray_get_slice_buffer(ctx, b, start_b, stop_b, shape_b, cache_b, cache_size_b));

    int64_t nchunk = 0;
    while (nchunk < c->catarr->extnitems / c->catarr->chunknitems) {
        int64_t elem_index = nchunk * c->catarr->chunkshape[0];
        int64_t chunk_shape;
        if (elem_index + c->catarr->chunkshape[0] <= c->catarr->shape[0]) {
            chunk_shape = c->catarr->chunkshape[0];
        } else {
            chunk_shape = c->catarr->shape[0] - elem_index;
        }

        int64_t start_a[2] = {0};
        start_a[0] = elem_index;
        start_a[1] = 0;
        int64_t stop_a[2] = {0};
        stop_a[0] = elem_index + chunk_shape;
        stop_a[1] = a->dtshape->shape[1];

        int64_t shape_a[2] = {0};
        shape_a[0] = c->catarr->extchunkshape[0];
        shape_a[1] = a->dtshape->shape[1];

        IARRAY_RETURN_IF_FAILED(_iarray_get_slice_buffer(ctx, a, start_a, stop_a, shape_a, cache_a, cache_size_a));

        // Compress data
        blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
        IARRAY_RETURN_IF_FAILED(iarray_create_blosc_cparams(&cparams, prefilter_ctx, c->catarr->itemsize,
                                                            c->catarr->blocknitems * c->catarr->itemsize));
        blosc2_context *cctx = blosc2_create_cctx(cparams);
        uint8_t *chunk = malloc(c->catarr->extchunknitems * c->catarr->itemsize +
                                BLOSC2_MAX_OVERHEAD);
        int csize = blosc2_compress_ctx(cctx, NULL, (int32_t)c->catarr->extchunknitems * c->catarr->itemsize,
                                        chunk,
                                        (int32_t)c->catarr->extchunknitems * c->catarr->itemsize +
                                        BLOSC2_MAX_OVERHEAD);
        if (csize <= 0) {
            IARRAY_TRACE1(iarray.error, "Error compressing a blosc chunk");
            return INA_ERROR(IARRAY_ERR_BLOSC_FAILED);
        }
        blosc2_free_ctx(cctx);

        // Append to schunk
        blosc2_schunk_update_chunk(c->catarr->sc, nchunk, chunk, false);

        nchunk++;
    }

    INA_MEM_FREE_SAFE(cache_a);
    INA_MEM_FREE_SAFE(cache_b);

    return INA_SUCCESS;
}


INA_API(ina_rc_t) iarray_gemv(iarray_context_t *ctx,
                              iarray_container_t *a,
                              iarray_container_t *b,
                              iarray_container_t *c) {
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(a);
    INA_VERIFY_NOT_NULL(b);
    INA_VERIFY_NOT_NULL(c);

    int nthreads = mkl_get_max_threads();
    mkl_set_num_threads(1);
    IARRAY_RETURN_IF_FAILED(gemv_blosc(ctx, a, b, c));
    mkl_set_num_threads(nthreads);

    return INA_SUCCESS;
}
