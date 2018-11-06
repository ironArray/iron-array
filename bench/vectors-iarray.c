//
// Created by Francesc Alted on 6/11/2018.
//

/*
  Example program demonstrating how to execute an expression with super-chunks as operands.
  This is the version for using frames (either in-memory or on-disk) backing the super-chunks.

  To compile this program:

  $ gcc -O3 vectors-iarray.c -o vectors-iarray -lblosc

  To run:

  $ ./vectors-iarray memory
  ...
  $ ./vectors-iarray disk
  ...

*/

#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <libiarray/iarray.h>

#define KB (1024.)
#define MB (1024 * KB)
#define GB (1024 * MB)

#define NCHUNKS  100
#define NITEMS_CHUNK (200 * 1000)  // fits well in modern L3 caches
#define NELEM (NCHUNKS * NITEMS_CHUNK)  // multiple of NITEMS_CHUNKS for now
#define NTHREADS 1

// Fill X values in regular array
int fill_x(double* x)
{
    double incx = 10./NELEM;

    /* Fill even values between 0 and 10 */
    for (int i = 0; i<NELEM; i++) {
        x[i] = incx*i;
    }
    return 0;
}

// Compute and fill X values in a buffer
void fill_buffer(double* x, int nchunk)
{
    double incx = 10./NELEM;

    for (int i=0; i<NITEMS_CHUNK; i++) {
        x[i] = incx*(nchunk * NITEMS_CHUNK + i);
    }
}

void fill_sc_x(blosc2_schunk* sc_x, const size_t isize)
{
    static double buffer_x[NITEMS_CHUNK];

    /* Fill with even values between 0 and 10 */
    for (int nchunk = 0; nchunk<NCHUNKS; nchunk++) {
        fill_buffer(buffer_x, nchunk);
        blosc2_schunk_append_buffer(sc_x, buffer_x, isize);
    }
}

void fill_cta_x(caterva_array* cta_x, const size_t isize)
{
    static double buffer_x[NITEMS_CHUNK];

    /* Fill with even values between 0 and 10 */
    for (int nchunk = 0; nchunk<NCHUNKS; nchunk++) {
        fill_buffer(buffer_x, nchunk);
        blosc2_schunk_append_buffer(cta_x->sc, buffer_x, isize);
    }
}

double poly(const double x)
{
    return (x - 1.35) * (x - 4.45) * (x - 8.5);
}

// Compute and fill Y values in regular array
void compute_y(const double* x, double* y)
{
    for (int i = 0; i<NELEM; i++) {
        y[i] = poly(x[i]);
    }
}

// Compute and fill Y values in a buffer
void fill_buffer_y(const double* x, double* y)
{
    for (int i = 0; i<NITEMS_CHUNK; i++) {
        y[i] = poly(x[i]);
    }
}

// Check that two super-chunks with the same partitions are equal
bool test_schunks_equal(blosc2_schunk* sc1, blosc2_schunk* sc2) {
    size_t chunksize = (size_t)sc1->chunksize;
    int nitems_in_chunk = (int)chunksize / sc1->typesize;
    double *buffer_sc1 = malloc(chunksize);
    double *buffer_sc2 = malloc(chunksize);
    for (int nchunk=0; nchunk < sc1->nchunks; nchunk++) {
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
        for (int nelem=0; nelem < nitems_in_chunk; nelem++) {
            double vdiff = fabs(buffer_sc1[nelem] - buffer_sc2[nelem]);
            if (vdiff > 1e-6) {
                printf("Values differ in (%d nchunk, %d nelem) (diff: %f)\n", nchunk, nelem, vdiff);
                free(buffer_sc1);
                free(buffer_sc2);
                return false;
            }
        }
    }
    free(buffer_sc1);
    free(buffer_sc2);
    return true;
}

int main(int argc, char** argv)
{
    printf("Blosc version info: %s (%s)\n",
            BLOSC_VERSION_STRING, BLOSC_VERSION_DATE);

    ina_app_init(argc, argv, NULL);

    bool diskframes = false;
    if (argc > 1) {
        if (*argv[1] == 'd') {
            diskframes = true;
        }
    }

    blosc_init();

    const size_t isize = NITEMS_CHUNK * sizeof(double);
    static double buffer_x [NITEMS_CHUNK];
    static double buffer_y[NITEMS_CHUNK];
    blosc2_cparams cparams = BLOSC_CPARAMS_DEFAULTS;
    blosc2_dparams dparams = BLOSC_DPARAMS_DEFAULTS;
    blosc_timestamp_t last, current;
    double ttotal;

    /* Create a super-chunk container for input (X values) */
    cparams.typesize = sizeof(double);
    cparams.compcode = BLOSC_LZ4;
    cparams.clevel = 9;
    cparams.filters[0] = BLOSC_TRUNC_PREC;
    cparams.filters_meta[0] = 23;  // treat doubles as floats
    cparams.blocksize = 16 * (int)KB;  // 16 KB seems optimal for evaluating expressions
    cparams.nthreads = NTHREADS;
    dparams.nthreads = NTHREADS;

    // Fill the plain x operand
    static double x[NELEM];
    blosc_set_timestamp(&last);
    fill_x(x);
    blosc_set_timestamp(&current);
//	ttotal = blosc_elapsed_secs(last, current);
//	printf("Time for filling X values: %.3g s, %.1f MB/s\n",
//			ttotal, sizeof(x)/(ttotal*MB));

    // Create and fill a super-chunk for the x operand
    blosc2_frame frame_x = BLOSC_EMPTY_FRAME;
    if (diskframes) frame_x.fname = "x.b2frame";
    //sc_x = blosc2_new_schunk(cparams, dparams, &frame_x);
    caterva_pparams pparams;
    for (int i = 0; i < CATERVA_MAXDIM; i++) {
        pparams.shape[i] = 1;
        pparams.cshape[i] = 1;
    }
    pparams.shape[CATERVA_MAXDIM - 1] = NELEM;  // FIXME: 1's at the beginning should be removed
    pparams.cshape[CATERVA_MAXDIM - 1] = NITEMS_CHUNK;  // FIXME: 1's at the beginning should be removed
    pparams.ndims = 1;

    caterva_array *cta_x = caterva_new_array(cparams, dparams, &frame_x, pparams);
    blosc_set_timestamp(&last);
    fill_cta_x(cta_x, isize);
    blosc_set_timestamp(&current);
//	ttotal = blosc_elapsed_secs(last, current);
//	printf("Time for filling X values (compressed): %.3g s, %.1f MB/s\n",
//			ttotal, (sc_x->nbytes/(ttotal*MB)));
//	printf("Compression for X values: %.1f MB -> %.1f MB (%.1fx)\n",
//			(sc_x->nbytes/MB), (sc_x->cbytes/MB),
//			((double) sc_x->nbytes/sc_x->cbytes));

    // Compute the plain y vector
    static double y[NELEM];
    blosc_set_timestamp(&last);
    compute_y(x, y);
    blosc_set_timestamp(&current);
    ttotal = blosc_elapsed_secs(last, current);
    printf("Time for computing and filling Y values: %.3g s, %.1f MB/s\n",
            ttotal, sizeof(y)/(ttotal*MB));
    // To prevent the optimizer going too smart and removing 'dead' code
    int retcode = y[0] > y[1];

    // Create a super-chunk container and compute y values
    blosc2_frame frame_y = BLOSC_EMPTY_FRAME;
    if (diskframes) frame_y.fname = "y.b2frame";
    //sc_y = blosc2_new_schunk(cparams, dparams, &frame_y);
    caterva_array *cta_y = caterva_new_array(cparams, dparams, &frame_y, pparams);
    blosc2_schunk *sc_x = cta_x->sc;
    blosc2_schunk *sc_y = cta_y->sc;
    blosc_set_timestamp(&last);
    for (int nchunk = 0; nchunk < sc_x->nchunks; nchunk++) {
        int dsize = blosc2_schunk_decompress_chunk(sc_x, nchunk, buffer_x, isize);
        if (dsize < 0) {
            printf("Decompression error.  Error code: %d\n", dsize);
            return dsize;
        }
        fill_buffer_y(buffer_x, buffer_y);
        blosc2_schunk_append_buffer(sc_y, buffer_y, isize);
    }
    blosc_set_timestamp(&current);
    ttotal = blosc_elapsed_secs(last, current);
    printf("Time for computing and filling Y values (compressed): %.3g s, %.1f MB/s\n",
            ttotal, sc_y->nbytes/(ttotal*MB));
    printf("Compression for Y values: %.1f MB -> %.1f MB (%.1fx)\n",
            (sc_y->nbytes/MB), (sc_y->cbytes/MB),
            (1.*sc_y->nbytes)/sc_y->cbytes);

    // Check IronArray performance
    // First for the chunk evaluator

    /* Create a super-chunk backed by an in-memory frame */
    blosc2_frame frame_out = BLOSC_EMPTY_FRAME;
    if (diskframes) frame_out.fname = "out.b2frame";
    caterva_array *cta_out = caterva_new_array(cparams, dparams, &frame_out, pparams);

    // Create context for evaluating the expressions and iarray containers for operands
    iarray_context_t *iactx;
    iarray_config_t cfg = {.max_num_threads = NTHREADS, .flags = IARRAY_EXPR_EVAL_CHUNK,
        .cparams = &cparams, .dparams = &dparams, .pparams = &pparams};
    iarray_ctx_new(&cfg, &iactx);

    iarray_container_t *iac_x, *iac_y, *iac_out;
    iarray_from_ctarray(iactx, cta_x, IARRAY_DATA_TYPE_DOUBLE, &iac_x);
    iarray_from_ctarray(iactx, cta_y, IARRAY_DATA_TYPE_DOUBLE, &iac_y);
    iarray_from_ctarray(iactx, cta_out, IARRAY_DATA_TYPE_DOUBLE, &iac_out);

    iarray_expression_t *e;
    iarray_expr_new(iactx, &e);
    iarray_expr_bind(e, "x", iac_x);
    iarray_expr_compile(e, "(x - 1.35) * (x - 4.45) * (x - 8.5)");

    blosc_set_timestamp(&last);
    iarray_eval(e, iac_out);
    blosc_set_timestamp(&current);
    ttotal = blosc_elapsed_secs(last, current);
    blosc2_schunk *sc_out = cta_out->sc;
    printf("\n");
    printf("Time for computing and filling OUT values using iarray (chunk eval):  %.3g s, %.1f MB/s\n",
            ttotal, sc_out->nbytes / (ttotal * MB));
    printf("Compression for OUT values: %.1f MB -> %.1f MB (%.1fx)\n",
            (sc_out->nbytes/MB), (sc_out->cbytes/MB),
            (1.*sc_out->nbytes)/sc_out->cbytes);

    // Check that we are getting the same results than through manual computation
    if (!test_schunks_equal(sc_y, sc_out)) {
        return -1;
    }

    iarray_expr_free(iactx, &e);
    iarray_ctx_free(&iactx);

    // Then for the block evaluator
    iarray_config_t cfg2 = {.max_num_threads = NTHREADS, .flags = IARRAY_EXPR_EVAL_BLOCK,
                            .cparams = &cparams, .dparams = &dparams};
    iarray_context_t *iactx2;
    iarray_ctx_new(&cfg2, &iactx2);
    iarray_container_t *iac_x2;
    iarray_from_ctarray(iactx2, cta_x, IARRAY_DATA_TYPE_DOUBLE, &iac_x2);

    /* Create a super-chunk backed by an in-memory frame */
    blosc2_frame frame_out2 = BLOSC_EMPTY_FRAME;
    if (diskframes) frame_out2.fname = "out2.b2frame";
    caterva_array *cta_out2 = caterva_new_array(cparams, dparams, &frame_out2, pparams);
    iarray_container_t *iac_out2;
    iarray_from_ctarray(iactx2, cta_out2, IARRAY_DATA_TYPE_DOUBLE, &iac_out2);

    iarray_expr_new(iactx2, &e);
    iarray_expr_bind(e, "x", iac_x2);
    iarray_expr_compile(e, "(x - 1.35) * (x - 4.45) * (x - 8.5)");

    blosc_set_timestamp(&last);
    iarray_eval(e, iac_out2);
    blosc_set_timestamp(&current);
    ttotal = blosc_elapsed_secs(last, current);
    blosc2_schunk *sc_out2 = cta_out2->sc;
    printf("\n");
    printf("Time for computing and filling OUT values using iarray (block eval):  %.3g s, %.1f MB/s\n",
            ttotal, sc_out2->nbytes / (ttotal * MB));
    printf("Compression for OUT values: %.1f MB -> %.1f MB (%.1fx)\n",
            (sc_out2->nbytes/MB), (sc_out2->cbytes/MB),
            (1.*sc_out2->nbytes)/sc_out2->cbytes);

    // Check that we are getting the same results than through manual computation
    if (!test_schunks_equal(sc_y, sc_out2)) {
        return -1;
    }

    iarray_expr_free(iactx2, &e);
    iarray_ctx_free(&iactx2);

    // Free resources
    caterva_free_array(cta_x);
    caterva_free_array(cta_y);
    caterva_free_array(cta_out);
    caterva_free_array(cta_out2);

    blosc_destroy();

    return retcode;
}