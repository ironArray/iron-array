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

#define NELEM_BYTES(nelem) (nelem * sizeof(double))
#define NTHREADS 1

/* Check that the values of a super-chunk are equal to a C matrix */
int test_mat_equal(int nelems, double *c1, double *c2) {
    for (int nelem=0; nelem < nelems; nelem++) {
        double vdiff = fabs((c1[nelem] - c2[nelem]) / c1[nelem]);
        if (vdiff > 1e-6) {
            printf("%f, %f\n", c1[nelem], c2[nelem]);
            printf("Values differ in (%d nelem) (diff: %f)\n", nelem, vdiff);
            return 0;
        }
    }
    return 1;
}

static double *mat_x = NULL;
static double *mat_y = NULL;
static double *mat_out = NULL;
static double *mat_res = NULL;

static void ina_cleanup_handler(int error, int *exitcode)
{
    iarray_destroy();
}

int main(int argc, char** argv)
{
    ina_stopwatch_t *w = NULL;
    iarray_context_t *ctx = NULL;
    const char *mat_x_name = NULL;
    const char *mat_y_name = NULL;
    const char *mat_out_name = NULL;

    uint64_t nbytes = 0;
    uint64_t cbytes = 0;
    double nbytes_mb = 0;
    double cbytes_mb = 0;

    uint64_t shape_x[] = {4056, 3230};
    uint64_t pshape_x[] = {675, 300};
    uint64_t bshape_x[] = {800, 400};

    uint64_t size_x = shape_x[0] * shape_x[1];
    uint64_t shape_y[] = {3230, 3712};
    uint64_t pshape_y[] = {300, 478};
    uint64_t bshape_y [] = {400, 600};
    uint64_t size_y = shape_y[0] * shape_y[1];

    uint64_t shape_out[] = {shape_x[0], shape_y[1]};
    uint64_t pshape_out[] = {bshape_x[0], bshape_y[1]};
    uint64_t size_out = shape_out[0] * shape_out[1];

    uint64_t flops = (2 * shape_x[1] - 1) * shape_x[0] * shape_y[1];

    INA_OPTS(opt,
             INA_OPT_FLAG("p", "persistence", "Use persistent containers"),
             INA_OPT_FLAG("r", "remove", "Remove the previous persistent containers (only valid w/ -p)")
    );

    if (!INA_SUCCEED(ina_app_init(argc, argv, opt))) {
        return EXIT_FAILURE;
    }
    ina_set_cleanup_handler(ina_cleanup_handler);

    if (INA_SUCCEED(ina_opt_isset("p"))) {
        mat_x_name = "mat_x.b2frame";
        mat_y_name = "mat_y.b2frame";
        mat_out_name = "mat_out.b2frame";
        if (INA_SUCCEED(ina_opt_isset("r"))) {
            remove(mat_x_name);
            remove(mat_y_name);
            remove(mat_out_name);
            printf("Storage for iarray matrices: *memory*\n");
        } else {
            printf("Storage for iarray matrices: *disk*\n");
        }
    } else {
        printf("Storage for iarray matrices: *memory*\n");
    }

    iarray_store_properties_t mat_x_prop = {.id = mat_x_name};
    iarray_store_properties_t mat_y_prop = {.id = mat_y_name};
    iarray_store_properties_t mat_out_prop = {.id = mat_out_name};

    printf("\n");
    printf("Measuring time for multiplying matrices X and Y\n");

    printf("\n");
    printf("Matrix X has a shape of (%lld, %lld) with a partition of (%lld, %lld) \n",
           shape_x[0], shape_x[1], pshape_x[0], pshape_x[1]);
    printf("Matrix Y has a shape of (%lld, %lld) with a partition of (%lld, %lld) \n",
           shape_y[0], shape_y[1], pshape_y[0], pshape_y[1]);

    printf("\n");
    printf("Working set for the 4 uncompressed matrices: %.1f MB\n", (size_x + size_y + size_out * 2) * sizeof(double) / (double)_IARRAY_SIZE_MB);

    INA_MUST_SUCCEED(iarray_init());

    iarray_config_t config = IARRAY_CONFIG_DEFAULTS;
    config.compression_codec = IARRAY_COMPRESSION_LZ4;
    config.compression_level = 5;
    config.max_num_threads = NTHREADS;
    config.eval_flags = IARRAY_EXPR_EVAL_CHUNK;

    INA_MUST_SUCCEED(iarray_context_new(&config, &ctx));

    double elapsed_sec = 0;
    INA_STOPWATCH_NEW(-1, -1, &w);

    iarray_container_t *con_x;
    iarray_container_t *con_y;

    int flags = INA_SUCCEED(ina_opt_isset("p"))? IARRAY_CONTAINER_PERSIST : 0;

    bool allocated = false;

    mat_x = (double *) ina_mem_alloc((sizeof(double) * size_x));
    mat_y = (double *) ina_mem_alloc((sizeof(double) * size_y));

    printf("\n");
    if (INA_SUCCEED(ina_opt_isset("p")) && _iarray_file_exists(mat_x_prop.id) && _iarray_file_exists(mat_y_prop.id)) {
        INA_STOPWATCH_START(w);
        INA_MUST_SUCCEED(iarray_from_file(ctx, &mat_x_prop, &con_x));
        INA_MUST_SUCCEED(iarray_from_file(ctx, &mat_y_prop, &con_y));
        INA_STOPWATCH_STOP(w);
        INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
        printf("Time for *opening* X and Y values: %.3g s, %.1f GB/s\n",
               elapsed_sec, NELEM_BYTES(size_x + size_y) / (elapsed_sec * _IARRAY_SIZE_GB));
    } else {

        allocated = true;

        INA_STOPWATCH_START(w);
        double incx = 10. / size_x;
        for (uint64_t i = 0; i < size_x; i++) {
            mat_x[i] = i * incx;
        }
        double incy = 10. / size_y;
        for (uint64_t i = 0; i < size_y; i++) {
            mat_y[i] = i * incy;
        }
        INA_STOPWATCH_STOP(w);

        INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
        printf("Time for filling X and Y matrices: %.3g s, %.1f MB/s\n",
               elapsed_sec, NELEM_BYTES(size_x + size_y) / (elapsed_sec * _IARRAY_SIZE_MB));

        iarray_dtshape_t xdtshape;
        xdtshape.ndim = 2;
        xdtshape.dtype = IARRAY_DATA_TYPE_DOUBLE;
        for (int i = 0; i < xdtshape.ndim; ++i) {
            xdtshape.shape[i] = shape_x[i];
            xdtshape.pshape[i] = pshape_x[i];
        }

        iarray_dtshape_t ydtshape;
        ydtshape.ndim = 2;
        ydtshape.dtype = IARRAY_DATA_TYPE_DOUBLE;
        for (int i = 0; i < ydtshape.ndim; ++i) {
            ydtshape.shape[i] = shape_y[i];
            ydtshape.pshape[i] = pshape_y[i];
        }


        INA_STOPWATCH_START(w);
        INA_MUST_SUCCEED(iarray_from_buffer(ctx, &xdtshape, mat_x, size_x, &mat_x_prop, flags, &con_x));
        INA_MUST_SUCCEED(iarray_from_buffer(ctx, &ydtshape, mat_y, size_y, &mat_y_prop, flags, &con_y));
        INA_STOPWATCH_STOP(w);
        INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));

        iarray_container_info(con_x, &nbytes, &cbytes);
        printf("Time for filling X and Y iarray-containers: %.3g s, %.1f MB/s\n",
               elapsed_sec, NELEM_BYTES(size_x + size_y) / (elapsed_sec * _IARRAY_SIZE_MB));
        nbytes_mb = ((double) nbytes / _IARRAY_SIZE_MB);
        cbytes_mb = ((double) cbytes / _IARRAY_SIZE_MB);
        printf("Compression for X iarray-container: %.1f MB -> %.1f MB (%.1fx)\n",
               nbytes_mb, cbytes_mb, ((double) nbytes / cbytes));
    }

    if (allocated == false) {
        INA_MUST_SUCCEED(iarray_to_buffer(ctx, con_x, mat_x, NELEM_BYTES(size_x)));
        INA_MUST_SUCCEED(iarray_to_buffer(ctx, con_y, mat_y, NELEM_BYTES(size_y)));
    }

    mat_out = (double *) ina_mem_alloc((sizeof(double) * size_out));
    mat_res = (double *) ina_mem_alloc((sizeof(double) * size_out));

    /* Compute naive matrix-matrix multiplication */
    INA_STOPWATCH_START(w);
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (int) shape_x[0], (int) shape_y[1], (int) shape_x[1],
                1.0, mat_x, (int) shape_x[1], mat_y, (int) shape_y[1], 0.0, mat_res, (int) shape_y[1]);
    INA_STOPWATCH_STOP(w);
    INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));

    printf("\n");
    printf("Time for multiplying two matrices (pure C): %.3g s, %.1f GFLOPs\n",
           elapsed_sec, flops / (elapsed_sec * 10e9));


    iarray_dtshape_t outdtshape;
    outdtshape.ndim = 2;
    outdtshape.dtype = IARRAY_DATA_TYPE_DOUBLE;
    for (int i = 0; i < outdtshape.ndim; ++i) {
        outdtshape.shape[i] = shape_out[i];
        outdtshape.pshape[i] = pshape_out[i];
    }

    iarray_container_t *con_out;
    iarray_container_new(ctx, &outdtshape, &mat_out_prop, 0, &con_out);

    INA_STOPWATCH_START(w);
    iarray_linalg_matmul(ctx, con_x, con_y, con_out, bshape_x, bshape_y, IARRAY_OPERATOR_GENERAL); /* FIXME: error handling */
    INA_STOPWATCH_STOP(w);
    INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));

    iarray_container_info(con_out, &nbytes, &cbytes);
    printf("\n");
    printf("Time for multiplying two matrices (iarray):  %.3g s, %.1f GFLOPs\n",
           elapsed_sec, flops / (elapsed_sec * 10e9));

    nbytes_mb = ((double) nbytes / _IARRAY_SIZE_MB);
    cbytes_mb = ((double) cbytes / _IARRAY_SIZE_MB);
    printf("Compression for OUT values: %.1f MB -> %.1f MB (%.1fx)\n",
           nbytes_mb, cbytes_mb, (1.*nbytes) / cbytes);

    /* Check that we are getting the same results than through manual computation */
    ina_mem_set(mat_out, 0, NELEM_BYTES(size_out));
    iarray_to_buffer(ctx, con_out, mat_out, NELEM_BYTES(size_out));

    if (!test_mat_equal((int) size_out, mat_res, mat_out)) {
        return EXIT_FAILURE; /* FIXME: error-handling */
    } else {
        printf("\nThe multiplication has been done correctly!");
    }

    iarray_container_free(ctx, &con_x);
    iarray_container_free(ctx, &con_y);
    iarray_container_free(ctx, &con_out);

    iarray_context_free(&ctx);

    ina_mem_free(mat_x);
    ina_mem_free(mat_y);
    ina_mem_free(mat_out);
    ina_mem_free(mat_res);

    INA_STOPWATCH_FREE(&w);

    return EXIT_SUCCESS;
}