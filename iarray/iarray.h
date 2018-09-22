//
// Created by Francesc Alted on 11/09/2018.
//

#ifndef PROJECT_IARRAY_H
#define PROJECT_IARRAY_H

#define INA_API(x)
typedef int ina_rc_t;

typedef struct iarray_context_s iarray_context_t;

typedef struct iarray_container_s iarray_container_t;

typedef struct iarray_expression_s iarray_expression_t;

typedef struct iarray_config_s {
	void *cparams;

} iarray_config_t;

typedef enum iarray_rng_e {
	IARRAY_RNG_MERSENNE_TWISTER,
	IARRAY_RNG_SOBOL,
} iarray_rng_t;

typedef enum iarray_data_type_e {
	IARRAY_DATA_TYPE_DOUBLE,
	IARRAY_DATA_TYPE_FLOAT
} iarray_data_type_t;

typedef struct iarray_dtshape_s {
	iarray_data_type_t dtype;
	int ndim; /* IF ndim = 0 THEN it is a scalar */
	int *dims;
} iarray_dtshape_t;

INA_API(ina_rc_t) iarray_ctx_new(iarray_config_t *cfg, iarray_context_t **ctx);
INA_API(ina_rc_t) iarray_ctx_free(iarray_context_t **ctx);

INA_API(ina_rc_t) iarray_arange(iarray_context_t *ctx, int start, int stop, int step, iarray_data_type_t dtype, iarray_container_t **container);
INA_API(ina_rc_t) iarray_zeros(iarray_context_t *ctx, iarray_dtshape_t *dtshape, iarray_data_type_t dtype, iarray_container_t **container);
INA_API(ina_rc_t) iarray_ones(iarray_context_t *ctx, iarray_dtshape_t *dtshape, iarray_data_type_t dtype, iarray_container_t **container);
INA_API(ina_rc_t) iarray_fill_float(iarray_context_t *ctx, iarray_dtshape_t *dtshape, float value, iarray_container_t **container);
INA_API(ina_rc_t) iarray_fill_double(iarray_context_t *ctx, iarray_dtshape_t *dtshape, double value, iarray_container_t **container);
INA_API(ina_rc_t) iarray_rand(iarray_context_t *ctx, int shape[], iarray_rng_t rng, iarray_data_type_t dtype, iarray_container_t **container);

INA_API(ina_rc_t) iarray_expr_new(iarray_context_t *ctx, iarray_expression_t **e);
INA_API(ina_rc_t) iarray_expr_free(iarray_context_t *ctx, iarray_expression_t **e);
INA_API(ina_rc_t) iarray_expr_bind(iarray_expression_t *e, const char *var, iarray_container_t *val);
INA_API(ina_rc_t) iarray_expr_bind(iarray_expression_t *e, const char *var, iarray_container_t *val);
INA_API(ina_rc_t) iarray_expr_bind_scalar_float(iarray_expression_t *e, const char *var, float val);
INA_API(ina_rc_t) iarray_expr_bind_scalar_double(iarray_expression_t *e, const char *var, double val);

INA_API(ina_rc_t) iarray_expr_compile(iarray_expression_t *e, const char *expr);

INA_API(ina_rc_t) iarray_eval(iarray_context_t *ctx, iarray_expression_t *e, iarray_container_t **ret);

#endif //PROJECT_IARRAY_H
