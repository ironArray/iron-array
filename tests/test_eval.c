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

#define NCHUNKS  10
#define NITEMS_CHUNK (20 * 1000)
#define NELEM (((NCHUNKS - 1) * NITEMS_CHUNK) + 10)
#define NTHREADS 1

//static double vector_x[NELEM];
//static double vector_y[NELEM];

/* Compute and fill X values in a buffer */
void fill_buffer(double* x, int nchunk, int nitems)
{
    /* Fill with even values between 0 and 10 */
    double incx = 10. / NELEM;

    for (int i = 0; i<nitems; i++) {
        x[i] = incx * (nchunk*NITEMS_CHUNK + i);
    }
}

void fill_sc_x(double *buffer_x, blosc2_schunk* sc_x)
{
    for (int nchunk = 0; nchunk<NCHUNKS; nchunk++) {
        int nitems = (nchunk < NCHUNKS - 1) ? NITEMS_CHUNK : NELEM - nchunk * NITEMS_CHUNK;
        fill_buffer(buffer_x, nchunk, nitems);
        blosc2_schunk_append_buffer(sc_x, buffer_x, nitems * sizeof(double));
    }
}

double poly(const double x)
{
    return (x - 1.35)*(x - 4.45)*(x - 8.5);
}

/* Compute and fill Y values in a buffer */
void fill_buffer_y(const double* x, double* y, int nitems)
{
    for (int i = 0; i<nitems; i++) {
        y[i] = poly(x[i]);
    }
}

/* Check that two super-chunks with the same partitions are equal */
int test_schunks_equal_double(blosc2_schunk* sc1, blosc2_schunk* sc2) 
{
    size_t chunksize = (size_t)sc1->chunksize;
    int nitems_in_chunk = (int)chunksize / sc1->typesize;
    double *buffer_sc1 = malloc(chunksize);
    double *buffer_sc2 = malloc(chunksize);
    for (int nchunk = 0; nchunk < sc1->nchunks; nchunk++) {
        int dsize = blosc2_schunk_decompress_chunk(sc1, nchunk, buffer_sc1, chunksize);
        if (dsize < 0) {
            fprintf(stderr, "Error in decompressing a chunk from sc1\n");
            return false;
        }
        dsize = blosc2_schunk_decompress_chunk(sc2, nchunk, buffer_sc2, chunksize);
        if (dsize < 0) {
            fprintf(stderr, "Error in decompressing a chunk from sc2\n");
            return false;
        }
        for (int nelem = 0; nelem < nitems_in_chunk; nelem++) {
            double vdiff = fabs(buffer_sc1[nelem] - buffer_sc2[nelem]);
            if (vdiff > 1e-6) {
                INA_TEST_MSG("Values differ in (%d nchunk, %d nelem) (diff: %f)\n", nchunk, nelem, vdiff);
                free(buffer_sc1);
                free(buffer_sc2);
                return 0;
            }
        }
    }
    free(buffer_sc1);
    free(buffer_sc2);
    return 1;
}

INA_TEST_DATA(eval)
{
    int tests_run;
    caterva_array *cta_x;
    caterva_array *cta_y;
    caterva_array *cta_out;
    int nbytes;
    int cbytes;
    int clevel;
    double *buffer_x;
    double *buffer_y;
};

INA_TEST_SETUP(eval)
{
    blosc_init();

    // Create a super-chunk container for input (X values)
    blosc2_cparams cparams = BLOSC_CPARAMS_DEFAULTS;
    blosc2_dparams dparams = BLOSC_DPARAMS_DEFAULTS;
    cparams.typesize = sizeof(double);
    cparams.compcode = BLOSC_LZ4;
    cparams.clevel = 9;
    cparams.filters[0] = BLOSC_TRUNC_PREC;
    cparams.filters_meta[0] = 23;  // treat doubles as floats
    cparams.blocksize = 16 * (int)KB;  // 16 KB seems optimal for evaluating expressions
    cparams.nthreads = NTHREADS;
    dparams.nthreads = NTHREADS;

    data->buffer_x = ina_mem_alloc(sizeof(double)*NITEMS_CHUNK);
    data->buffer_y = ina_mem_alloc(sizeof(double)*NITEMS_CHUNK);

    // Create and fill the caterva container for x values
    blosc2_frame frame_x = BLOSC_EMPTY_FRAME;
    caterva_pparams pparams = CATERVA_PPARAMS_ONES;
    pparams.shape[CATERVA_MAXDIM - 1] = NELEM;
    pparams.cshape[CATERVA_MAXDIM - 1] = NITEMS_CHUNK;
    caterva_array *cta_x = caterva_new_array(cparams, dparams, &frame_x, pparams);
    data->cta_x = cta_x;
    fill_sc_x(data->buffer_x, cta_x->sc);

    // Create a super-chunk container for output (Y values)
    const size_t isize = NITEMS_CHUNK * sizeof(double);
    blosc2_frame frame_y = BLOSC_EMPTY_FRAME;
    caterva_array *cta_y = caterva_new_array(cparams, dparams, &frame_y, pparams);
    data->cta_y = cta_y;
    blosc2_schunk *sc_x = cta_x->sc, *sc_y = cta_y->sc;
    for (int nchunk = 0; nchunk < sc_x->nchunks; nchunk++) {
        int dsize = blosc2_schunk_decompress_chunk(sc_x, nchunk, data->buffer_x, isize);
        if (dsize < 0) {
            INA_TEST_MSG("Decompression error.  Error code: %d\n", dsize);
            INA_TEST_ASSERT_TRUE(0);
        }
        int nitems = (nchunk < NCHUNKS - 1) ? NITEMS_CHUNK : NELEM - nchunk * NITEMS_CHUNK;
        fill_buffer_y(data->buffer_x, data->buffer_y, nitems);
        blosc2_schunk_append_buffer(sc_y, data->buffer_y, nitems * sizeof(double));
    }

    // Create a caterva container for eval output (OUT values)
    blosc2_frame frame_out = BLOSC_EMPTY_FRAME;
    caterva_array *cta_out = caterva_new_array(cparams, dparams, &frame_out, pparams);
    data->cta_out = cta_out;
}

INA_TEST_TEARDOWN(eval)
{
    caterva_free_array(data->cta_x);
    caterva_free_array(data->cta_y);
    caterva_free_array(data->cta_out);
    ina_mem_free(data->buffer_x);
    ina_mem_free(data->buffer_y);
}

INA_TEST_FIXTURE(eval, chunk1)
{
    iarray_context_t *iactx;
    iarray_config_t cfg = { .max_num_threads = NTHREADS, .flags = IARRAY_EXPR_EVAL_CHUNK };
    iarray_ctx_new(&cfg, &iactx);
    iarray_expression_t* e;
    iarray_expr_new(iactx, &e);
    iarray_container_t* c_x;
    iarray_from_ctarray(iactx, data->cta_x, IARRAY_DATA_TYPE_DOUBLE, &c_x);
    iarray_expr_bind(e, "x", c_x);
    iarray_container_t* c_out;
    iarray_from_ctarray(iactx, data->cta_out, IARRAY_DATA_TYPE_DOUBLE, &c_out);

    iarray_expr_compile(e, "(x - 1.35) * (x - 4.45) * (x - 8.5)");
    iarray_eval(e, c_out);
    
    INA_TEST_ASSERT_TRUE(test_schunks_equal_double(data->cta_y->sc, data->cta_out->sc));

    iarray_expr_free(iactx, &e);
    iarray_ctx_free(&iactx);
}

INA_TEST_FIXTURE(eval, block1)
{
    iarray_context_t *iactx;
    iarray_config_t cfg = { .max_num_threads = NTHREADS,.flags = IARRAY_EXPR_EVAL_BLOCK };
    iarray_ctx_new(&cfg, &iactx);
    iarray_expression_t* e;
    iarray_expr_new(iactx, &e);
    iarray_container_t* c_x;
    iarray_from_ctarray(iactx, data->cta_x, IARRAY_DATA_TYPE_DOUBLE, &c_x);
    iarray_expr_bind(e, "x", c_x);
    iarray_container_t* c_out;
    iarray_from_ctarray(iactx, data->cta_out, IARRAY_DATA_TYPE_DOUBLE, &c_out);

    iarray_expr_compile(e, "(x - 1.35) * (x - 4.45) * (x - 8.5)");
    iarray_eval(e, c_out);
    
    INA_TEST_ASSERT_TRUE(test_schunks_equal_double(data->cta_y->sc, data->cta_out->sc));

    iarray_expr_free(iactx, &e);
    iarray_ctx_free(&iactx);
}
