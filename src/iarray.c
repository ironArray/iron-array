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

#include <contribs/tinyexpr/tinyexpr.h>
#include <blosc.h>
#include <caterva.h>

#include <iarray_private.h>

#include <stdbool.h>

#define _IARRAY_MEMPOOL_EVAL_SIZE (8*1024*1024)
#define _IARRAY_EXPR_VAR_MAX      (128)

/* Tuning params */
#define _IARRAY_BLOSC_BLOCK_SIZE  (16 * (int)_IARRAY_SIZE_KB)  // 16 KB seems optimal for evaluating expressions

struct iarray_context_s {
    iarray_config_t *cfg;
    ina_mempool_t *mp;
    /* FIXME: track expressions -> list */
};

typedef struct _iarray_tinyexpr_var_s {
    const char *var;
    iarray_container_t *c;
} _iarray_tinyexpr_var_t;

struct iarray_expression_s {
    iarray_context_t *ctx;
    ina_str_t expr;
    size_t nchunks;
    size_t blocksize;
    size_t typesize;
    size_t chunksize;
    int nvars;
    te_expr *texpr;
    iarray_temporary_t **temp_vars;
    iarray_container_t *out;
    _iarray_tinyexpr_var_t vars[_IARRAY_EXPR_VAR_MAX];
};

struct iarray_container_s {
    iarray_dtshape_t *dtshape;
    blosc2_cparams *cparams;
    blosc2_dparams *dparams;
    caterva_pparams *pparams;
    blosc2_frame *frame;
    caterva_array *catarr;
    ina_str_t name;
    union {
        float f;
        double d;
    } scalar_value;
};

static ina_rc_t _iarray_container_new(iarray_context_t *ctx, 
                                      iarray_dtshape_t *shape, 
                                      const char *name,
                                      int flags,
                                      iarray_container_t **c)
{
    blosc2_cparams cparams = BLOSC_CPARAMS_DEFAULTS;
    blosc2_dparams dparams = BLOSC_DPARAMS_DEFAULTS;
    caterva_pparams pparams;
    int blosc_filter_idx = 0;

    /* validation */
    if (shape->ndim > CATERVA_MAXDIM) {
        return INA_ERROR(INA_ERR_EXCEEDED);
    }
    if (flags & IARRAY_CONTAINER_PERSIST && name == NULL) {
        return INA_ERROR(INA_ERR_INVALID_ARGUMENT);
    }
    for (int i = 0; i < shape->ndim; ++i) {
        if (shape->dims[i] < shape->partshape[i]) {
            return INA_ERROR(INA_ERR_INVALID_ARGUMENT);
        }
    }

    *c = (iarray_container_t*)ina_mem_alloc(sizeof(iarray_container_t));
    INA_RETURN_IF_NULL(c);

    (*c)->dtshape = (iarray_dtshape_t*)ina_mem_alloc(sizeof(iarray_dtshape_t));
    INA_FAIL_IF((*c)->dtshape == NULL);
    ina_mem_cpy((*c)->dtshape, shape, sizeof(iarray_dtshape_t));

    (*c)->frame = (blosc2_frame*)ina_mem_alloc(sizeof(blosc2_frame));
    INA_FAIL_IF((*c)->frame == NULL);
    ina_mem_cpy((*c)->frame, &BLOSC_EMPTY_FRAME, sizeof(blosc2_frame));

    (*c)->cparams = (blosc2_cparams*)ina_mem_alloc(sizeof(blosc2_cparams));
    INA_FAIL_IF((*c)->cparams == NULL);

    (*c)->dparams = (blosc2_dparams*)ina_mem_alloc(sizeof(blosc2_dparams));
    INA_FAIL_IF((*c)->dparams == NULL);

    (*c)->pparams = (caterva_pparams*)ina_mem_alloc(sizeof(caterva_pparams));
    INA_FAIL_IF((*c)->pparams == NULL);

    if (flags & IARRAY_CONTAINER_PERSIST) {
        (*c)->name = ina_str_new_fromcstr(name);
        INA_FAIL_IF((*c)->name == NULL);
        (*c)->frame->fname = (char*)ina_str_cstr((*c)->name); /* FIXME: shouldn't fname be a const char? */
    }

    switch (shape->dtype) {
        case IARRAY_DATA_TYPE_DOUBLE:
            cparams.typesize = sizeof(double);
            break;
        case IARRAY_DATA_TYPE_FLOAT:
            cparams.typesize = sizeof(float);
            break;
    }
    cparams.compcode = ctx->cfg->compression_codec;
    cparams.clevel = (uint8_t)ctx->cfg->compression_level; /* Since its just a mapping, we know the cast is ok */
    cparams.blocksize = _IARRAY_BLOSC_BLOCK_SIZE;
    cparams.nthreads = (uint16_t)ctx->cfg->max_num_threads; /* Since its just a mapping, we know the cast is ok */
    if (shape->dtype == IARRAY_DATA_TYPE_DOUBLE && ctx->cfg->flags & IARRAY_COMP_TRUNC_PREC) {
        cparams.filters[blosc_filter_idx] = BLOSC_TRUNC_PREC;
        cparams.filters_meta[blosc_filter_idx] = ctx->cfg->fp_mantissa_bits;
        blosc_filter_idx++;
    }
    if (ctx->cfg->flags & IARRAY_COMP_BITSHUFFLE) {
        cparams.filters[blosc_filter_idx] = BLOSC_BITSHUFFLE;
        blosc_filter_idx++;
    }
    if (ctx->cfg->flags & IARRAY_COMP_SHUFFLE) {
        cparams.filters[blosc_filter_idx] = BLOSC_SHUFFLE;
        blosc_filter_idx++;
    }
    if (ctx->cfg->flags & IARRAY_COMP_DELTA) {
        cparams.filters[blosc_filter_idx] = BLOSC_DELTA;
        blosc_filter_idx++;
    }
    ina_mem_cpy((*c)->cparams, &cparams, sizeof(blosc2_cparams));

    dparams.nthreads = (uint16_t)ctx->cfg->max_num_threads; /* Since its just a mapping, we know the cast is ok */
    ina_mem_cpy((*c)->dparams, &dparams, sizeof(blosc2_dparams));
    
    for (int i = 0; i < CATERVA_MAXDIM; i++) {
        pparams.shape[i] = 1;
        pparams.cshape[i] = 1;
    }
    for (int i = 0; i < shape->ndim; ++i) { // FIXME: 1's at the beginning should be removed
        pparams.shape[CATERVA_MAXDIM - (i + 1)] = shape->dims[i];
        pparams.cshape[CATERVA_MAXDIM - (i + 1)] = shape->partshape[i];
    }
    pparams.ndims = shape->ndim;
    ina_mem_cpy((*c)->pparams, &pparams, sizeof(caterva_pparams));

    (*c)->catarr = caterva_new_array(*(*c)->cparams, *(*c)->dparams, (*c)->frame, *(*c)->pparams);
    INA_FAIL_IF((*c)->catarr == NULL);

    return INA_SUCCESS;

fail:
    iarray_container_free(ctx, c);
    return ina_err_get_rc();
}

static ina_rc_t _iarray_container_fill_float(iarray_container_t *c, float value)
{
    /* FIXME: blosc set container */
    return INA_SUCCESS;
}

static ina_rc_t _iarray_container_fill_double(iarray_container_t *c, double value)
{
    /* FIXME: blosc set container */
    return INA_SUCCESS;
}

INA_API(ina_rc_t) iarray_init()
{
    ina_init();
    blosc_init();
    return INA_SUCCESS;
}

INA_API(void) iarray_destroy()
{
    blosc_destroy();
}

INA_API(ina_rc_t) iarray_context_new(iarray_config_t *cfg, iarray_context_t **ctx)
{
    INA_VERIFY_NOT_NULL(ctx);
    *ctx = ina_mem_alloc(sizeof(iarray_context_t));
    INA_RETURN_IF_NULL(ctx);
    (*ctx)->cfg = ina_mem_alloc(sizeof(iarray_config_t));
    INA_FAIL_IF((*ctx)->cfg == NULL);
    ina_mem_cpy((*ctx)->cfg, cfg, sizeof(iarray_config_t));
    if (!(cfg->flags & IARRAY_EXPR_EVAL_BLOCK) && !(cfg->flags & IARRAY_EXPR_EVAL_CHUNK)) {
        (*ctx)->cfg->flags |= IARRAY_EXPR_EVAL_CHUNK;
    }
    INA_FAIL_IF_ERROR(ina_mempool_new(_IARRAY_MEMPOOL_EVAL_SIZE, NULL, INA_MEM_DYNAMIC, &(*ctx)->mp));
    return INA_SUCCESS;

fail:
    iarray_context_free(ctx);
    return ina_err_get_rc();
}

INA_API(void) iarray_context_free(iarray_context_t **ctx)
{
    INA_FREE_CHECK(ctx);
    ina_mempool_free(&(*ctx)->mp);
    INA_MEM_FREE_SAFE((*ctx)->cfg);
    INA_MEM_FREE_SAFE(*ctx);
}

INA_API(ina_rc_t) iarray_container_new(iarray_context_t *ctx,
    iarray_dtshape_t *dtshape,
    const char *name,
    int flags,
    iarray_container_t **container)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(dtshape);
    INA_VERIFY_NOT_NULL(container);

    return _iarray_container_new(ctx, dtshape, name, flags, container);
}

INA_API(ina_rc_t) iarray_arange(iarray_context_t *ctx,
    iarray_dtshape_t *dtshape,
    int start,
    int stop,
    int step,
    const char *name,
    int flags,
    iarray_container_t **container)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(dtshape);
    INA_VERIFY_NOT_NULL(container);

    INA_RETURN_IF_FAILED(_iarray_container_new(ctx, dtshape, name, flags, container));

    /* implement arange */

    return INA_SUCCESS;
}

INA_API(ina_rc_t) iarray_zeros(iarray_context_t *ctx,
    iarray_dtshape_t *dtshape,
    const char *name,
    int flags,
    iarray_container_t **container)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(dtshape);
    INA_VERIFY_NOT_NULL(container);

    INA_RETURN_IF_FAILED(_iarray_container_new(ctx, dtshape, name, flags, container));

    switch (dtshape->dtype) {
        case IARRAY_DATA_TYPE_DOUBLE:
            INA_FAIL_IF_ERROR(_iarray_container_fill_double(*container, 0.0));
            break;
        case IARRAY_DATA_TYPE_FLOAT:
            INA_FAIL_IF_ERROR(_iarray_container_fill_float(*container, 0.0f));
            break;
    }
    return INA_SUCCESS;
fail:
    iarray_container_free(ctx, container);
    return ina_err_get_rc();
}

INA_API(ina_rc_t) iarray_ones(iarray_context_t *ctx,
    iarray_dtshape_t *dtshape,
    const char *name,
    int flags,
    iarray_container_t **container)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(dtshape);
    INA_VERIFY_NOT_NULL(container);

    INA_RETURN_IF_FAILED(_iarray_container_new(ctx, dtshape, name, flags, container));

    switch (dtshape->dtype) {
    case IARRAY_DATA_TYPE_DOUBLE:
        INA_FAIL_IF_ERROR(_iarray_container_fill_double(*container, 1.0));
        break;
    case IARRAY_DATA_TYPE_FLOAT:
        INA_FAIL_IF_ERROR(_iarray_container_fill_float(*container, 1.0f));
        break;
    }
    return INA_SUCCESS;
fail:
    iarray_container_free(ctx, container);
    return ina_err_get_rc();
}

INA_API(ina_rc_t) iarray_fill_float(iarray_context_t *ctx,
    iarray_dtshape_t *dtshape,
    float value,
    const char *name,
    int flags,
    iarray_container_t **container)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(dtshape);
    INA_VERIFY_NOT_NULL(container);

    INA_RETURN_IF_FAILED(_iarray_container_new(ctx, dtshape, name, flags, container));

    INA_FAIL_IF_ERROR(_iarray_container_fill_float(*container, value));

    return INA_SUCCESS;

fail:
    iarray_container_free(ctx, container);
    return ina_err_get_rc();
}

INA_API(ina_rc_t) iarray_fill_double(iarray_context_t *ctx,
    iarray_dtshape_t *dtshape,
    double value,
    const char *name,
    int flags,
    iarray_container_t **container)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(dtshape);
    INA_VERIFY_NOT_NULL(container);

    INA_RETURN_IF_FAILED(_iarray_container_new(ctx, dtshape, name, flags, container));

    INA_FAIL_IF_ERROR(_iarray_container_fill_double(*container, value));

    return INA_SUCCESS;

fail:
    iarray_container_free(ctx, container);
    return ina_err_get_rc();
}

INA_API(ina_rc_t) iarray_rand(iarray_context_t *ctx,
    iarray_dtshape_t *dtshape,
    iarray_rng_t rng,
    const char *name,
    int flags,
    iarray_container_t **container)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(dtshape);
    INA_VERIFY_NOT_NULL(container);

    INA_RETURN_IF_FAILED(_iarray_container_new(ctx, dtshape, name, flags, container));

    /* implement rand */

    return INA_SUCCESS;
}

INA_API(ina_rc_t) iarray_slice(iarray_context_t *ctx,
    iarray_container_t *c,
    iarray_slice_param_t *params,
    iarray_container_t **container)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(params);
    INA_VERIFY_NOT_NULL(container);

    /* implement get slice via caterva_get_slice */
    
    return INA_SUCCESS;
}

INA_API(ina_rc_t) iarray_from_buffer(iarray_context_t *ctx,
    iarray_dtshape_t *dtshape,
    const void *buffer,
    size_t buffer_len,
    iarray_storage_format_t fmt,
    const char *name,
    int flags,
    iarray_container_t **container)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(dtshape);
    INA_VERIFY_NOT_NULL(buffer);
    INA_VERIFY_NOT_NULL(container);

    INA_RETURN_IF_FAILED(_iarray_container_new(ctx, dtshape, name, flags, container));

    if (caterva_from_buffer((*container)->catarr, buffer) != 0) {
        INA_ERROR(INA_ERR_FAILED);
        INA_FAIL_IF(1);
    }

    return INA_SUCCESS;

fail:
    iarray_container_free(ctx, container);
    return ina_err_get_rc();
}

INA_API(ina_rc_t) iarray_to_buffer(iarray_context_t *ctx,
    iarray_container_t *container,
    void *buffer,
    size_t buffer_len,
    iarray_storage_format_t fmt)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(buffer);
    INA_VERIFY_NOT_NULL(container);

    if (caterva_to_buffer(container->catarr, buffer) != 0) {
        return INA_ERROR(INA_ERR_FAILED);
    }

    return INA_SUCCESS;
}

INA_API(ina_rc_t) iarray_container_info(iarray_container_t *c,
    size_t *size_in_bytes,
    size_t *compressed_size_in_bytes)
{
    INA_VERIFY_NOT_NULL(c);

    *size_in_bytes = c->catarr->sc->nbytes;
    *compressed_size_in_bytes = c->catarr->sc->cbytes;

    return INA_SUCCESS;
}

INA_API(void) iarray_container_free(iarray_context_t *ctx, iarray_container_t **container)
{
    INA_FREE_CHECK(container);
    if ((*container)->catarr != NULL) {
        caterva_free_array((*container)->catarr);
    }
    if ((*container)->frame != NULL) {
        blosc2_free_frame((*container)->frame);
    }
    INA_MEM_FREE_SAFE((*container)->frame);
    INA_MEM_FREE_SAFE((*container)->cparams);
    INA_MEM_FREE_SAFE((*container)->dparams);
    INA_MEM_FREE_SAFE((*container)->pparams);
    INA_MEM_FREE_SAFE((*container)->dtshape);
    INA_MEM_FREE_SAFE(*container);
}

INA_API(ina_rc_t) iarray_expr_new(iarray_context_t *ctx, iarray_expression_t **e)
{
    INA_VERIFY_NOT_NULL(ctx);
    INA_VERIFY_NOT_NULL(e);
    *e = ina_mem_alloc(sizeof(iarray_expression_t));
    INA_RETURN_IF_NULL(e);
    (*e)->ctx = ctx;
    (*e)->nvars = 0;
    (*e)->temp_vars = ina_mem_alloc(sizeof(iarray_temporary_t*)*_IARRAY_EXPR_VAR_MAX);
    ina_mem_set(&(*e)->vars, 0, sizeof(_iarray_tinyexpr_var_t)*_IARRAY_EXPR_VAR_MAX);
    return INA_SUCCESS;
}

INA_API(void) iarray_expr_free(iarray_context_t *ctx, iarray_expression_t **e)
{
    INA_FREE_CHECK(e);
    INA_FREE_CHECK(&ctx);
    ina_mempool_reset(ctx->mp); // FIXME
    INA_MEM_FREE_SAFE((*e)->temp_vars);
    INA_MEM_FREE_SAFE(*e);
}

INA_API(ina_rc_t) iarray_expr_bind(iarray_expression_t *e, const char *var, iarray_container_t *val)
{
    if (val->dtshape->ndim > 2) {
        return INA_ERROR(INA_ERR_INVALID_ARGUMENT);
    }
    e->vars[e->nvars].var = var;
    e->vars[e->nvars].c = val;
    e->nvars++;
    return INA_SUCCESS;
}

//INA_API(ina_rc_t) iarray_expr_bind_scalar_float(iarray_expression_t *e, const char *var, float val)
//{
//	iarray_container_t *c = ina_mempool_dalloc(e->mp, sizeof(iarray_container_t));
//	c->dtshape = ina_mempool_dalloc(e->mp, sizeof(iarray_dtshape_t));
//	c->dtshape->ndim = 0;
//	c->dtshape->dims = NULL;
//	c->dtshape->dtype = IARRAY_DATA_TYPE_FLOAT;
//	c->scalar_value.f = val;
//	return INA_SUCCESS;
//}

INA_API(ina_rc_t) iarray_expr_bind_scalar_double(iarray_expression_t *e, const char *var, double val)
{
    iarray_container_t *c = ina_mempool_dalloc(e->ctx->mp, sizeof(iarray_container_t));
    c->dtshape = ina_mempool_dalloc(e->ctx->mp, sizeof(iarray_dtshape_t));
    c->dtshape->ndim = 0;
    c->dtshape->dtype = IARRAY_DATA_TYPE_DOUBLE;
    c->scalar_value.d = val;
    e->vars[e->nvars].var = var;
    e->vars[e->nvars].c = c;
    e->nvars++;
    return INA_SUCCESS;
}

INA_API(ina_rc_t) iarray_expr_compile(iarray_expression_t *e, const char *expr)
{
    e->expr = ina_str_new_fromcstr(expr);
    te_variable *te_vars = ina_mempool_dalloc(e->ctx->mp, e->nvars * sizeof(te_variable));
    caterva_array *catarr = e->vars[0].c->catarr;
    blosc2_schunk *schunk = catarr->sc;
    int dim0 = 0;
    if (e->ctx->cfg->flags & IARRAY_EXPR_EVAL_BLOCK) {
        int typesize = schunk->typesize;
        int nchunks = schunk->nchunks;
        void *chunk;
        bool needs_free;
        int retcode = blosc2_schunk_get_chunk(schunk, 0, &chunk, &needs_free);
        size_t chunksize, cbytes, blocksize;
        blosc_cbuffer_sizes(chunk, &chunksize, &cbytes, &blocksize);
        if (needs_free) {
            free(chunk);
        }
        dim0 = (int)blocksize / typesize;
        e->nchunks = nchunks;
        e->chunksize = chunksize;
        e->blocksize = blocksize;
        e->typesize = typesize;
    }
    else if (e->ctx->cfg->flags & IARRAY_EXPR_EVAL_CHUNK) {
        dim0 = schunk->chunksize / schunk->typesize;
        e->nchunks = schunk->nchunks;
        e->chunksize = schunk->chunksize;
        e->typesize = schunk->typesize;
    }
    else {
        fprintf(stderr, "Flag %d is not supported\n", e->ctx->cfg->flags);
        return INA_ERR_NOT_SUPPORTED;
    }
    iarray_dtshape_t shape_var = {
        .ndim = 1,
        .dims = {dim0},
        .dtype = e->vars[0].c->dtshape->dtype,
    };
    for (int nvar = 0; nvar < e->nvars; nvar++) {
        iarray_temporary_new(e, e->vars[nvar].c, &shape_var, &e->temp_vars[nvar]);
        te_vars[nvar].name = e->vars[nvar].var;
        te_vars[nvar].address = &e->temp_vars[nvar];
        te_vars[nvar].type = TE_VARIABLE;
        te_vars[nvar].context = NULL;
    }
    int err = 0;
    e->texpr = te_compile(e, ina_str_cstr(e->expr), te_vars, e->nvars, &err);
    if (e->texpr == 0) {
        return INA_ERROR(INA_ERR_NOT_COMPILED);
    }
    return INA_SUCCESS;
}

INA_API(ina_rc_t) iarray_eval(iarray_expression_t *e, iarray_container_t *ret)
{
    blosc2_schunk *schunk0 = e->vars[0].c->catarr->sc;  // get the super-chunk of the first variable
    size_t nitems_in_schunk = schunk0->nbytes / e->typesize;
    size_t nitems_in_chunk = e->chunksize / e->typesize;
    int nvars = e->nvars;
    caterva_array out = *ret->catarr;

    if (e->ctx->cfg->flags & IARRAY_EXPR_EVAL_BLOCK) {
        int8_t *outbuf = ina_mem_alloc(e->chunksize);  // FIXME: this could benefit from using a mempool (probably not)
        size_t nitems = e->blocksize / e->typesize;
        void **var_chunks = ina_mem_alloc(nvars * sizeof(void*));
        bool *var_needs_free = ina_mem_alloc(nvars * sizeof(bool));
        // Allocate a buffer for every (compressed) chunk
        for (int nvar = 0; nvar < nvars; nvar++) {
            //var_chunks[nvar] = ina_mem_alloc(e->chunksize);  // FIXME: looks like this does not work correctly
            var_chunks[nvar] = malloc(e->chunksize);
            var_needs_free[nvar] = false;
        }
        for (size_t nchunk = 0; nchunk < e->nchunks; nchunk++) {
            size_t chunksize = (nchunk < e->nchunks - 1) ? e->chunksize : schunk0->nbytes - nchunk * e->chunksize;
            size_t nblocks_in_chunk = chunksize / e->blocksize;
            size_t corrected_blocksize = e->blocksize;
            size_t corrected_nitems = nitems;
            if (nblocks_in_chunk * e->blocksize < e->chunksize) {
                nitems_in_chunk = chunksize / e->typesize;
                nblocks_in_chunk += 1;
            }
            // Allocate a buffer for every chunk (specially useful for reading on-disk variables)
            for (int nvar = 0; nvar < nvars; nvar++) {
                blosc2_schunk *schunk = e->vars[nvar].c->catarr->sc;
                int retcode = blosc2_schunk_get_chunk(schunk, (int)nchunk, &var_chunks[nvar], &var_needs_free[nvar]);
            }
//#pragma omp parallel for schedule(dynamic)
            for (size_t nblock = 0; nblock < nblocks_in_chunk; nblock++) {
                if ((nblock + 1 == nblocks_in_chunk) && (nblock + 1) * e->blocksize > chunksize) {
                    corrected_blocksize = chunksize - nblock * e->blocksize;
                    corrected_nitems = (int)corrected_blocksize / e->typesize;
                }
                // Decompress blocks in variables into temporaries
                for (int nvar = 0; nvar < nvars; nvar++) {
                    int dsize = blosc_getitem(var_chunks[nvar], (int)(nblock * nitems), (int)corrected_nitems, e->temp_vars[nvar]->data);
                    if (dsize < 0) {
                        printf("Decompression error.  Error code: %d\n", dsize);
                        return INA_ERR_FAILED;
                    }
                }
                // Evaluate the expression for this block
                const iarray_temporary_t *expr_out = te_eval(e, e->texpr);
                ina_mem_cpy(outbuf + nblock * e->blocksize, expr_out->data, corrected_blocksize);
            }
            blosc2_schunk_append_buffer(out.sc, outbuf, nitems_in_chunk * e->typesize);
        }
        for (int nvar = 0; nvar < nvars; nvar++) {
            if (var_needs_free[nvar]) {
                //ina_mem_free(var_chunks[nvar]);  // this raises an error (bug in the ina library?)
                free(var_chunks[nvar]);
            }
        }
        ina_mem_free(var_chunks);
        ina_mem_free(var_needs_free);
        ina_mem_free(outbuf);
    }
    else {
        // Evaluate the expression for all the chunks in variables
        for (size_t nchunk = 0; nchunk < e->nchunks; nchunk++) {
            // Decompress chunks in variables into temporaries
            for (int nvar = 0; nvar < nvars; nvar++) {
                blosc2_schunk *schunk = e->vars[nvar].c->catarr->sc;
                int dsize = blosc2_schunk_decompress_chunk(schunk, (int)nchunk, e->temp_vars[nvar]->data, e->chunksize);
                if (dsize < 0) {
                    printf("Decompression error.  Error code: %d\n", dsize);
                    return INA_ERR_FAILED;
                }
            }
            const iarray_temporary_t *expr_out = te_eval(e, e->texpr);
            // Correct the number of items in last chunk
            nitems_in_chunk = (nchunk < e->nchunks - 1) ? nitems_in_chunk : nitems_in_schunk - nchunk * nitems_in_chunk;
            blosc2_schunk_append_buffer(out.sc, expr_out->data, nitems_in_chunk * e->typesize);
        }
    }
    return INA_SUCCESS;
}

ina_rc_t iarray_shape_size(iarray_dtshape_t *dtshape, size_t *size)
{
    size_t type_size = 0;
    switch (dtshape->dtype) {
        case IARRAY_DATA_TYPE_DOUBLE:
            type_size = sizeof(double);
            break;
        case IARRAY_DATA_TYPE_FLOAT:
            type_size = sizeof(float);
            break;
    }
    for (int i = 0; i < dtshape->ndim; ++i) {
        *size += dtshape->dims[i] * type_size;
    }
    return INA_SUCCESS;
}

ina_rc_t iarray_temporary_new(iarray_expression_t *expr, iarray_container_t *c, iarray_dtshape_t *dtshape,
        iarray_temporary_t **temp)
{
    *temp = ina_mempool_dalloc(expr->ctx->mp, sizeof(iarray_temporary_t));
    (*temp)->dtshape = ina_mempool_dalloc(expr->ctx->mp, sizeof(iarray_dtshape_t));
    ina_mem_cpy((*temp)->dtshape, dtshape, sizeof(iarray_dtshape_t));
    size_t size = 0;
    iarray_shape_size(dtshape, &size);
    (*temp)->size = size;
    if (c != NULL) {
        // FIXME: support float values too
        ina_mem_cpy(&(*temp)->scalar_value, &c->scalar_value, sizeof(double));
    }
    if (size > 0) {
        (*temp)->data = ina_mempool_dalloc(expr->ctx->mp, size);
    }

    return INA_SUCCESS;
}

static iarray_temporary_t* _iarray_op(iarray_expression_t *expr, iarray_temporary_t *lhs, iarray_temporary_t *rhs, iarray_optype_t op)
{
    bool scalar = false;
    bool scalar_vector = false;
    bool vector_vector = false;
    iarray_dtshape_t dtshape;
    ina_mem_set(&dtshape, 0, sizeof(iarray_dtshape_t));
    iarray_blas_type_t op_type = IARRAY_OPERATION_TYPE_BLAS1;
    iarray_temporary_t *scalar_tmp = NULL;
    iarray_temporary_t *scalar_lhs = NULL;
    iarray_temporary_t *out;

    if (lhs->dtshape->ndim == 0 && rhs->dtshape->ndim == 0) {   /* scalar-scalar */
        dtshape.dtype = rhs->dtshape->dtype;
        dtshape.ndim = rhs->dtshape->ndim;
        memcpy(dtshape.dims, rhs->dtshape->dims, sizeof(int) * dtshape.ndim);
        scalar = true;
    }
    else if (lhs->dtshape->ndim == 0 || rhs->dtshape->ndim == 0) {   /* scalar-vector */
        if (lhs->dtshape->ndim == 0) {
            dtshape.dtype = rhs->dtshape->dtype;
            dtshape.ndim = rhs->dtshape->ndim;
            ina_mem_cpy(dtshape.dims, rhs->dtshape->dims, sizeof(int) * dtshape.ndim);
            scalar_tmp = lhs;
            scalar_lhs = rhs;
        }
        else {
            dtshape.dtype = lhs->dtshape->dtype;
            dtshape.ndim = lhs->dtshape->ndim;
            ina_mem_cpy(dtshape.dims, lhs->dtshape->dims, sizeof(int) * dtshape.ndim);
            scalar_tmp = rhs;
            scalar_lhs = lhs;
        }
        scalar_vector = true;
    }
    else if (lhs->dtshape->ndim == 1 && rhs->dtshape->ndim == 1) { /* vector-vector */
        dtshape.dtype = lhs->dtshape->dtype;
        dtshape.ndim = lhs->dtshape->ndim;
        ina_mem_cpy(dtshape.dims, lhs->dtshape->dims, sizeof(int)*lhs->dtshape->ndim);
        vector_vector = true;
    }
    else {
        /* FIXME: matrix/vector and matrix/matrix addition */
    }

    iarray_temporary_new(expr, NULL, &dtshape, &out);

    switch (dtshape.dtype) {
        case IARRAY_DATA_TYPE_DOUBLE:
        {
            int len = (int)out->size / sizeof(double);
            if (scalar) {
                switch(op) {
                case IARRAY_OPERATION_TYPE_ADD:
                    out->scalar_value.d = lhs->scalar_value.d + rhs->scalar_value.d;
                    break;
                case IARRAY_OPERATION_TYPE_SUB:
                    out->scalar_value.d = lhs->scalar_value.d - rhs->scalar_value.d;
                    break;
                case IARRAY_OPERATION_TYPE_MUL:
                    out->scalar_value.d = lhs->scalar_value.d * rhs->scalar_value.d;
                    break;
                case IARRAY_OPERATION_TYPE_DIVIDE:
                    out->scalar_value.d = lhs->scalar_value.d / rhs->scalar_value.d;
                    break;
                default:
                    printf("Operation not supported yet");
                }
            }
            else if (scalar_vector) {
                double dscalar = scalar_tmp->scalar_value.d;
                double *odata = (double*)out->data;
                double *ldata = (double*)scalar_lhs->data;
                switch(op) {
                case IARRAY_OPERATION_TYPE_ADD:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        odata[i] = ldata[i] + dscalar;
                    }
                    break;
                case IARRAY_OPERATION_TYPE_SUB:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        odata[i] = ldata[i] - dscalar;
                    }
                    break;
                case IARRAY_OPERATION_TYPE_MUL:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        odata[i] = ldata[i] * dscalar;
                    }
                    break;
                case IARRAY_OPERATION_TYPE_DIVIDE:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        odata[i] = ldata[i] / dscalar;
                    }
                    break;
                default:
                    printf("Operation not supported yet");
                }
            }
            else if (vector_vector) {
                switch(op) {
                case IARRAY_OPERATION_TYPE_ADD:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        ((double*)out->data)[i] = ((double*)lhs->data)[i] + ((double*)rhs->data)[i];
                    }
                    break;
                case IARRAY_OPERATION_TYPE_SUB:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        ((double*)out->data)[i] = ((double*)lhs->data)[i] - ((double*)rhs->data)[i];
                    }
                    break;
                case IARRAY_OPERATION_TYPE_MUL:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        ((double*)out->data)[i] = ((double*)lhs->data)[i] * ((double*)rhs->data)[i];
                    }
                    break;
                case IARRAY_OPERATION_TYPE_DIVIDE:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        ((double*)out->data)[i] = ((double*)lhs->data)[i] / ((double*)rhs->data)[i];
                    }
                    break;
                default:
                    printf("Operation not supported yet");
                }
            }
            else {
                printf("DTshape combination not supported yet\n");
                return NULL;
            }
        }
        break;
        case IARRAY_DATA_TYPE_FLOAT:
        {
            int len = (int)out->size / sizeof(float);
            if (scalar) {
                switch(op) {
                case IARRAY_OPERATION_TYPE_ADD:
                    out->scalar_value.f = lhs->scalar_value.f + rhs->scalar_value.f;
                    break;
                case IARRAY_OPERATION_TYPE_SUB:
                    out->scalar_value.f = lhs->scalar_value.f - rhs->scalar_value.f;
                    break;
                case IARRAY_OPERATION_TYPE_MUL:
                    out->scalar_value.f = lhs->scalar_value.f * rhs->scalar_value.f;
                    break;
                case IARRAY_OPERATION_TYPE_DIVIDE:
                    out->scalar_value.f = lhs->scalar_value.f / rhs->scalar_value.f;
                    break;
                default:
                    printf("Operation not supported yet");
                }
            }
            else if (scalar_vector) {
                float dscalar = (float)scalar_tmp->scalar_value.d;
                float *odata = (float*)out->data;
                float *ldata = (float*)scalar_lhs->data;
                switch(op) {
                case IARRAY_OPERATION_TYPE_ADD:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        odata[i] = ldata[i] + dscalar;
                    }
                    break;
                case IARRAY_OPERATION_TYPE_SUB:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        odata[i] = ldata[i] - dscalar;
                    }
                    break;
                case IARRAY_OPERATION_TYPE_MUL:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        odata[i] = ldata[i] * dscalar;
                    }
                    break;
                case IARRAY_OPERATION_TYPE_DIVIDE:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        odata[i] = ldata[i] / dscalar;
                    }
                    break;
                default:
                    printf("Operation not supported yet");
                }
            }
            else if (vector_vector) {
                switch(op) {
                case IARRAY_OPERATION_TYPE_ADD:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        ((float*)out->data)[i] = ((float*)lhs->data)[i] + ((float*)rhs->data)[i];
                    }
                    break;
                case IARRAY_OPERATION_TYPE_SUB:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        ((float*)out->data)[i] = ((float*)lhs->data)[i] - ((float*)rhs->data)[i];
                    }
                    break;
                case IARRAY_OPERATION_TYPE_MUL:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        ((float*)out->data)[i] = ((float*)lhs->data)[i] * ((float*)rhs->data)[i];
                    }
                    break;
                case IARRAY_OPERATION_TYPE_DIVIDE:
#pragma omp parallel for
                    for (int i = 0; i < len; ++i) {
                        ((float*)out->data)[i] = ((float*)lhs->data)[i] / ((float*)rhs->data)[i];
                    }
                    break;
                default:
                    printf("Operation not supported yet");
                }
            }
            else {
                printf("DTshape combination not supported yet\n");
                return NULL;
            }
        }
        break;
    }

    return out;
}

iarray_temporary_t* _iarray_op_add(iarray_expression_t *expr, iarray_temporary_t *lhs, iarray_temporary_t *rhs)
{
    return _iarray_op(expr, lhs, rhs, IARRAY_OPERATION_TYPE_ADD);
}

iarray_temporary_t* _iarray_op_sub(iarray_expression_t *expr, iarray_temporary_t *lhs, iarray_temporary_t *rhs)
{
    return _iarray_op(expr, lhs, rhs, IARRAY_OPERATION_TYPE_SUB);
}

iarray_temporary_t* _iarray_op_mul(iarray_expression_t *expr, iarray_temporary_t *lhs, iarray_temporary_t *rhs)
{
    return _iarray_op(expr, lhs, rhs, IARRAY_OPERATION_TYPE_MUL);
}

iarray_temporary_t* _iarray_op_divide(iarray_expression_t *expr, iarray_temporary_t *lhs, iarray_temporary_t *rhs)
{
    return _iarray_op(expr, lhs, rhs, IARRAY_OPERATION_TYPE_DIVIDE);
}

INA_API(ina_rc_t) iarray_expr_get_mp(iarray_expression_t *e, ina_mempool_t **mp)
{
    *mp = e->ctx->mp;
    return INA_SUCCESS;
}

// TODO: This should go when support for MKL is here
int _mm_mul_d(size_t n, double const *a, double const *b, double *c)
{
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            for (size_t k = 0; k < n; ++k) {
                c[i*n+j] += a[i*n+k] * b[k*n+j];
            }
        }
    }
    return 0;
}

// TODO: This should go when support for MKL is here
int _mm_mul_f(size_t n, float const *a, float const *b, float *c)
{
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            for (size_t k = 0; k < n; ++k) {
                c[i*n+j] += a[i*n+k] * b[k*n+j];
            }
        }
    }
    return 0;
}

// TODO: This should go when support for MKL is here
int _mv_mul_d(size_t n, double const *a, double const *b, double *c)
{
    for (size_t i = 0; i < n; ++i) {
        for (size_t k = 0; k < n; ++k) {
            c[i] += a[i*n+k] * b[k];
        }
    }
    return 0;
}

// TODO: This should go when support for MKL is here
int _mv_mul_f(size_t n, float const *a, float const *b, float *c)
{
    for (size_t i = 0; i < n; ++i) {
        for (size_t k = 0; k < n; ++k) {
            c[i] += a[i*n+k] * b[k];
        }
    }
    return 0;
}

int _dtshape_equal(iarray_dtshape_t *a, iarray_dtshape_t *b) {
    if (a->dtype != b->dtype) {
        return -1;
    }
    if (a->ndim != b->ndim) {
        return -1;
    }
    for (int i = 0; i < CATERVA_MAXDIM; ++i) {
        if (a->dims[i] != b->dims[i]) {
            return -1;
        }
    }
    return 0;
}


INA_API(ina_rc_t) iarray_equal_data(iarray_container_t *a, iarray_container_t *b) {

    if (caterva_equal_data(a->catarr, b->catarr) != 0) {
        return 1;
    }

    return 0;
}


INA_API(ina_rc_t) iarray_gemm(iarray_container_t *a, iarray_container_t *b, iarray_container_t *c) {
    size_t P = a->catarr->cshape[7];
    size_t M = a->catarr->eshape[6];
    size_t K = a->catarr->eshape[7];
    size_t N = b->catarr->eshape[7];

    size_t p_size = P * P * a->catarr->sc->typesize;
    int dtype = a->dtshape->dtype;

    uint8_t *a_block = malloc(p_size);
    uint8_t *b_block = malloc(p_size);
    uint8_t *c_block = malloc(p_size);

    for (size_t m = 0; m < M / P; m++)
    {
        for (size_t n = 0; n < N / P; n++)
        {
            memset(c_block, 0, p_size);
            for (size_t k = 0; k < K / P; k++)
            {
                size_t a_i = (m * K / P + k);
                size_t b_i = (k * N / P + n);

                int a_tam = blosc2_schunk_decompress_chunk(a->catarr->sc, (int)a_i, a_block, p_size);
                int b_tam = blosc2_schunk_decompress_chunk(b->catarr->sc, (int)b_i, b_block, p_size);
// FIXME: Use the blas function when MKL support would be there
//                cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, P, P, P,
//                            1.0, a_block, P, b_block, P, 1.0, c_block, P);
                if (dtype == IARRAY_DATA_TYPE_DOUBLE) {
                    _mm_mul_d(P, (double *) a_block, (double *) b_block, (double *) c_block);
                }
                else if (dtype == IARRAY_DATA_TYPE_FLOAT) {
                    _mm_mul_f(P, (float *) a_block, (float *) b_block, (float *) c_block);
                }
            }
            blosc2_schunk_append_buffer(c->catarr->sc, &c_block[0], p_size);
        }
    }
    free(a_block);
    free(b_block);
    free(c_block);
    return 0;
}

INA_API(ina_rc_t) iarray_gemv(iarray_container_t *a, iarray_container_t *b, iarray_container_t *c) {
    size_t P = a->catarr->cshape[7];

    size_t M = a->catarr->eshape[6];
    size_t K = a->catarr->eshape[7];

    size_t p_size = P * P * a->catarr->sc->typesize;
    size_t p_vsize = P * a->catarr->sc->typesize;

    int dtype = a->dtshape->dtype;

    uint8_t *a_block = malloc(p_size);
    uint8_t *b_block = malloc(p_vsize);
    uint8_t *c_block = malloc(p_vsize);

    size_t a_i, b_i;

    for (size_t m = 0; m < M / P; m++)
    {
        memset(c_block, 0, p_vsize);
        for (size_t k = 0; k < K / P; k++)
        {
            a_i = (m * K / P + k);
            b_i = (k);

            int a_tam = blosc2_schunk_decompress_chunk(a->catarr->sc, (int)a_i, a_block, p_size);
            int b_tam = blosc2_schunk_decompress_chunk(b->catarr->sc, (int)b_i, b_block, p_vsize);

            // cblas_dgemv(CblasRowMajor, CblasNoTrans, P, P, 1.0, a_block, P, b_block, 1, 1.0, c_block, 1);
            if (dtype == IARRAY_DATA_TYPE_DOUBLE) {
                _mv_mul_d(P, (double *) a_block, (double *) b_block, (double *) c_block);
            }
            else if (dtype == IARRAY_DATA_TYPE_FLOAT) {
                _mv_mul_f(P, (float *) a_block, (float *) b_block, (float *) c_block);
            }
        }
        blosc2_schunk_append_buffer(c->catarr->sc, &c_block[0], p_vsize);
    }
    return 0;
}
