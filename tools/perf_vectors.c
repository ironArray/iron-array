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

#define NCHUNKS  100
#define NITEMS_CHUNK (200 * 1000)  // fits well in modern L3 caches
#define NELEM (NCHUNKS * NITEMS_CHUNK)  // multiple of NITEMS_CHUNKS for now
#define PART_SIZE NITEMS_CHUNK
#define NTHREADS 1
#define XMAX 10.

static double _poly(const double x)
{
    return (x - 1.35) * (x - 4.45) * (x - 8.5);
}

// Fill X values in regular array
static int _fill_x(double* x)
{
    double incx = XMAX / NELEM;

    /* Fill even values between 0 and 10 */
    for (int i = 0; i<NELEM; i++) {
        x[i] = incx * i;
    }
    return 0;
}

// Compute and fill Y values in regular array
static void _compute_y(const double* x, double* y)
{
    for (int i = 0; i<NELEM; i++) {
        y[i] = _poly(x[i]);
    }
}

static void ina_cleanup_handler(int error, int *exitcode)
{
    iarray_destroy();
}

static double *x = NULL;
static double *y = NULL;

int main(int argc, char** argv)
{
    ina_stopwatch_t *w;
    iarray_context_t *ctx = NULL;
    const char *mat_x_name = NULL;
    const char *mat_y_name = NULL;
    const char *mat_out_name = NULL;
    const char *eval_method = NULL;

    INA_OPTS(opt,
             INA_OPT_INT("e", "eval-method", 1,
                         "EVAL_BLOCK = 1, EVAL_CHUNK = 2, EVAL_ITERBLOCK = 3, EVAL_ITERCHUNK = 4"),
             INA_OPT_INT("c", "clevel", 5, "Compression level"),
             INA_OPT_INT("l", "codec", 1, "Compression codec"),
             INA_OPT_FLAG("i", "iter", "Use iterator for filling values"),
             INA_OPT_FLAG("I", "iter-part", "Use partition iterator for filling values"),
             INA_OPT_FLAG("p", "persistence", "Use persistent containers"),
             INA_OPT_FLAG("r", "remove", "Remove the previous persistent containers (only valid w/ -p)")
    );

    if (!INA_SUCCEED(ina_app_init(argc, argv, opt))) {
        return EXIT_FAILURE;
    }
    ina_set_cleanup_handler(ina_cleanup_handler);

    int eval_flag;
    INA_MUST_SUCCEED(ina_opt_get_int("e", &eval_flag));
    int clevel;
    INA_MUST_SUCCEED(ina_opt_get_int("c", &clevel));
    int codec;
    INA_MUST_SUCCEED(ina_opt_get_int("l", &codec));

    if (INA_SUCCEED(ina_opt_isset("p"))) {
        mat_x_name = "mat_x.b2frame";
        mat_y_name = "mat_y.b2frame";
        mat_out_name = "mat_out.b2frame";
        if (INA_SUCCEED(ina_opt_isset("r"))) {
            remove(mat_x_name);
            remove(mat_y_name);
            remove(mat_out_name);
        }
    }
    iarray_store_properties_t mat_x = {.id = mat_x_name};
    iarray_store_properties_t mat_y = {.id = mat_y_name};
    iarray_store_properties_t mat_out = {.id = mat_out_name};

    int flags = INA_SUCCEED(ina_opt_isset("p"))? IARRAY_CONTAINER_PERSIST : 0;

    INA_MUST_SUCCEED(iarray_init());

    iarray_config_t config = IARRAY_CONFIG_DEFAULTS;
    config.compression_level = clevel;
    config.compression_codec = codec;
    config.max_num_threads = NTHREADS;
    if (eval_flag == 1) {
        eval_method = "EVAL_BLOCK";
        config.eval_flags = IARRAY_EXPR_EVAL_BLOCK;
    }
    else if (eval_flag == 2) {
        eval_method = "EVAL_CHUNK";
        config.eval_flags = IARRAY_EXPR_EVAL_CHUNK;
    }
    else if (eval_flag == 3) {
        eval_method = "EVAL_ITERBLOCK";
        config.eval_flags = IARRAY_EXPR_EVAL_ITERBLOCK;
    }
    else if (eval_flag == 4) {
        eval_method = "EVAL_ITERCHUNK";
        config.eval_flags = IARRAY_EXPR_EVAL_ITERCHUNK;
    }
    else {
        printf("eval_flag must be 1, 2, 3 or 4\n");
        return EXIT_FAILURE;
    }
    config.blocksize = 16 * _IARRAY_SIZE_KB;  // 16 KB seems optimal for evaluating expressions

    INA_MUST_SUCCEED(iarray_context_new(&config, &ctx));

    double elapsed_sec = 0;
    INA_STOPWATCH_NEW(-1, -1, &w);

    size_t buffer_len = sizeof(double) * NELEM;

    iarray_dtshape_t shape;
    shape.ndim = 1;
    shape.dtype = IARRAY_DATA_TYPE_DOUBLE;
    shape.shape[0] = NELEM;
    shape.pshape[0] = PART_SIZE;

    uint64_t nbytes = 0;
    uint64_t cbytes = 0;
    double nbytes_mb = 0;
    double cbytes_mb = 0;

    iarray_container_t *con_x;
    iarray_container_t *con_y;

    bool x_allocated = false, y_allocated = false;

    if (INA_SUCCEED(ina_opt_isset("p")) && _iarray_file_exists(mat_x.id)) {
        INA_STOPWATCH_START(w);
        INA_MUST_SUCCEED(iarray_from_file(ctx, &mat_x, &con_x));
        INA_STOPWATCH_STOP(w);
        INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
        printf("Time for *opening* X values: %.3g s, %.1f GB/s\n",
               elapsed_sec, buffer_len / (elapsed_sec * _IARRAY_SIZE_GB));
    }
    else {
        if (INA_SUCCEED(ina_opt_isset("i"))) {
            INA_STOPWATCH_START(w);
            iarray_container_new(ctx, &shape, &mat_x, flags, &con_x);
            iarray_iter_write_t *I;
            iarray_iter_write_new(ctx, con_x, &I);
            double incx = XMAX / NELEM;
            for (iarray_iter_write_init(I); !iarray_iter_write_finished(I); iarray_iter_write_next(I)) {
                iarray_iter_write_value_t val;
                iarray_iter_write_value(I, &val);
                double value = incx * (double) val.nelem;
                memcpy(val.pointer, &value, sizeof(double));
            }
            iarray_iter_write_free(I);
            INA_STOPWATCH_STOP(w);
            INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
            printf("Time for computing and filling X values via iterator: %.3g s, %.1f MB/s\n",
                   elapsed_sec, buffer_len / (elapsed_sec * _IARRAY_SIZE_MB));
        }
        else if (INA_SUCCEED(ina_opt_isset("I"))) {
            INA_STOPWATCH_START(w);
            iarray_container_new(ctx, &shape, &mat_x, flags, &con_x);
            iarray_iter_write_part_t *I;
            iarray_iter_write_part_new(ctx, con_x, &I);
            double incx = XMAX / NELEM;
            for (iarray_iter_write_part_init(I); !iarray_iter_write_part_finished(I); iarray_iter_write_part_next(I)) {
                iarray_iter_write_part_value_t val;
                iarray_iter_write_part_value(I, &val);
                uint64_t part_size = val.part_shape[0];  // 1-dim vector
                for (uint64_t i = 0; i < part_size; ++i) {
                    ((double *)val.pointer)[i] = incx * (double) (i + val.nelem * part_size);
                }
            }
            iarray_iter_write_part_free(I);
            INA_STOPWATCH_STOP(w);
            INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
            printf("Time for computing and filling X values via partition iterator: %.3g s, %.1f MB/s\n",
                   elapsed_sec, buffer_len / (elapsed_sec * _IARRAY_SIZE_MB));
        }
        else {
            INA_STOPWATCH_START(w);
            x = (double *) ina_mem_alloc(buffer_len);
            x_allocated = true;
            // Fill the plain x operand
            _fill_x(x);
            INA_STOPWATCH_STOP(w);
            INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
            printf("Time for computing and filling X values: %.3g s, %.1f MB/s\n",
                   elapsed_sec, buffer_len / (elapsed_sec * _IARRAY_SIZE_MB));
            INA_STOPWATCH_START(w);
            INA_MUST_SUCCEED(iarray_from_buffer(ctx, &shape, x, buffer_len, &mat_x, flags, &con_x));
            INA_STOPWATCH_STOP(w);
            INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
            printf("Time for compressing and *storing* X values: %.3g s, %.1f MB/s\n",
                   elapsed_sec, buffer_len / (elapsed_sec * _IARRAY_SIZE_MB));
        }
    }

    iarray_container_info(con_x, &nbytes, &cbytes);
    nbytes_mb = ((double)nbytes / _IARRAY_SIZE_MB);
    cbytes_mb = ((double)cbytes / _IARRAY_SIZE_MB);
    printf("Compression for X values: %.1f MB -> %.1f MB (%.1fx)\n",
           nbytes_mb, cbytes_mb, (1.*nbytes)/cbytes);

    if (INA_SUCCEED(ina_opt_isset("p")) && _iarray_file_exists(mat_y.id)) {
        INA_STOPWATCH_START(w);
        INA_MUST_SUCCEED(iarray_from_file(ctx, &mat_y, &con_y));
        INA_STOPWATCH_STOP(w);
        INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
        printf("Time for *opening* Y values: %.3g s, %.1f GB/s\n",
               elapsed_sec, buffer_len / (elapsed_sec * _IARRAY_SIZE_GB));
    }
    else {
        if (INA_SUCCEED(ina_opt_isset("i"))) {
            INA_STOPWATCH_START(w);
            iarray_container_new(ctx, &shape, &mat_y, flags, &con_y);
            iarray_iter_write_t *I;
            iarray_iter_write_new(ctx, con_y, &I);
            double incx = XMAX / NELEM;
            for (iarray_iter_write_init(I); !iarray_iter_write_finished(I); iarray_iter_write_next(I)) {
                iarray_iter_write_value_t val;
                iarray_iter_write_value(I, &val);
                double value = _poly(incx * (double) val.nelem);
                memcpy(val.pointer, &value, sizeof(double));
            }
            iarray_iter_write_free(I);
            INA_STOPWATCH_STOP(w);
            INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
            printf("Time for computing and filling Y values via iterator: %.3g s, %.1f MB/s\n",
                   elapsed_sec, buffer_len / (elapsed_sec * _IARRAY_SIZE_MB));
        }
        else if (INA_SUCCEED(ina_opt_isset("I"))) {
            INA_STOPWATCH_START(w);
            iarray_container_new(ctx, &shape, &mat_y, flags, &con_y);
            iarray_iter_write_part_t *I;
            iarray_iter_write_part_new(ctx, con_y, &I);
            double incx = XMAX / NELEM;
            for (iarray_iter_write_part_init(I); !iarray_iter_write_part_finished(I);
                 iarray_iter_write_part_next(I)) {
                iarray_iter_write_part_value_t val;
                iarray_iter_write_part_value(I, &val);
                uint64_t part_size = val.part_shape[0];  // 1-dim vector
                for (uint64_t i = 0; i < part_size; ++i) {
                    ((double *) val.pointer)[i] = _poly(incx * (double) (i + val.nelem * part_size));
                }
            }
            iarray_iter_write_part_free(I);
            INA_STOPWATCH_STOP(w);
            INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
            printf(
                "Time for computing and filling Y values via partition iterator: %.3g s, %.1f MB/s\n",
                elapsed_sec, buffer_len / (elapsed_sec * _IARRAY_SIZE_MB));
        }
        else {
            // Compute the plain y vector
            INA_STOPWATCH_START(w);
            y = (double*)ina_mem_alloc(buffer_len);
            y_allocated = true;
            _compute_y(x, y);
            INA_STOPWATCH_STOP(w);
            INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
            printf("Time for computing and filling Y values: %.3g s, %.1f MB/s\n",
                   elapsed_sec, buffer_len/(elapsed_sec*_IARRAY_SIZE_MB));
            INA_STOPWATCH_START(w);
            INA_MUST_SUCCEED(iarray_from_buffer(ctx, &shape, y, buffer_len, &mat_y, flags, &con_y));
            INA_STOPWATCH_STOP(w);
            INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
            printf("Time for compressing and *storing* Y values: %.3g s, %.1f MB/s\n",
                   elapsed_sec, buffer_len / (elapsed_sec * _IARRAY_SIZE_MB));
        }
    }

    iarray_container_info(con_y, &nbytes, &cbytes);
    nbytes_mb = ((double)nbytes / _IARRAY_SIZE_MB);
    cbytes_mb = ((double)cbytes / _IARRAY_SIZE_MB);
    printf("Compression for Y values: %.1f MB -> %.1f MB (%.1fx)\n",
           nbytes_mb, cbytes_mb, (1.*nbytes) / cbytes);

    // Check IronArray performance
    iarray_expression_t *e;
    iarray_expr_new(ctx, &e);
    iarray_expr_bind(e, "x", con_x);
    iarray_expr_compile(e, "(x - 1.35) * (x - 4.45) * (x - 8.5)");

    iarray_container_t *con_out;
    INA_MUST_SUCCEED(iarray_container_new(ctx, &shape, &mat_out, flags, &con_out));

    INA_STOPWATCH_START(w);
    iarray_eval(e, con_out);
    INA_STOPWATCH_STOP(w);
    INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
    iarray_container_info(con_out, &nbytes, &cbytes);
    printf("\n");
    printf("Time for computing and filling OUT values using iarray (%s):  %.3g s, %.1f MB/s\n",
           eval_method, elapsed_sec, nbytes / (elapsed_sec * _IARRAY_SIZE_MB));
    nbytes_mb = ((double)nbytes / (double)_IARRAY_SIZE_MB);
    cbytes_mb = ((double)cbytes / (double)_IARRAY_SIZE_MB);
    printf("Compression for OUT values: %.1f MB -> %.1f MB (%.1fx)\n",
           nbytes_mb, cbytes_mb, (1.*nbytes)/cbytes);

    iarray_expr_free(ctx, &e);

    printf("Checking that the outcome of the expression is correct...");
    fflush(stdout);
    INA_STOPWATCH_START(w);
    if (iarray_container_almost_equal(con_y, con_out, 1e-06) == INA_ERR_FAILED) {
        printf(" No!\n");
        return 1;
    }
    INA_STOPWATCH_STOP(w);
    INA_MUST_SUCCEED(ina_stopwatch_duration(w, &elapsed_sec));
    printf(" Yes!\n");
    printf("Time for checking that two iarrays are equal:  %.3g s, %.1f MB/s\n",
           elapsed_sec, (nbytes * 2) / (elapsed_sec * _IARRAY_SIZE_MB));


    iarray_container_free(ctx, &con_x);
    iarray_container_free(ctx, &con_y);
    iarray_container_free(ctx, &con_out);

    iarray_context_free(&ctx);

    if (x_allocated) ina_mem_free(x);
    if (y_allocated) ina_mem_free(y);

    INA_STOPWATCH_FREE(&w);

    return 0;
}