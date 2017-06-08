/*
 * Copyright 2012      Ecole Normale Superieure
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege,
 * Ecole Normale Superieure, 45 rue d’Ulm, 75230 Paris, France
 */

#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include <isl/aff.h>

#include "gpu_print.h"
#include "print.h"
#include "schedule.h"

/* Print declarations to "p" for arrays that are local to "prog"
 * but that are used on the host and therefore require a declaration.
 */
__isl_give isl_printer *gpu_print_local_declarations(__isl_take isl_printer *p,
	struct gpu_prog *prog)
{
	int i;

	if (!prog)
		return isl_printer_free(p);

	for (i = 0; i < prog->n_array; ++i) {
		struct gpu_array_info *array = &prog->array[i];
		isl_ast_expr *size;

		if (!array->declare_local)
			continue;
		size = array->declared_size;
		p = ppcg_print_declaration_with_size(p, array->type, size);
	}

	return p;
}

/* Print an expression for the size of "array" in bytes.
 */
__isl_give isl_printer *gpu_array_info_print_size(__isl_take isl_printer *prn,
	struct gpu_array_info *array)
{
	int i;

	for (i = 0; i < array->n_index; ++i) {
		isl_ast_expr *bound;

		prn = isl_printer_print_str(prn, "(");
		bound = isl_ast_expr_get_op_arg(array->bound_expr, 1 + i);
		prn = isl_printer_print_ast_expr(prn, bound);
		isl_ast_expr_free(bound);
		prn = isl_printer_print_str(prn, ") * ");
	}
	prn = isl_printer_print_str(prn, "sizeof(");
	prn = isl_printer_print_str(prn, array->type);
	prn = isl_printer_print_str(prn, ")");

	return prn;
}

/* Print the declaration of a non-linearized array argument.
 */
static __isl_give isl_printer *print_non_linearized_declaration_argument(
	__isl_take isl_printer *p, struct gpu_array_info *array)
{
	p = isl_printer_print_str(p, array->type);
	p = isl_printer_print_str(p, " ");

	p = isl_printer_print_ast_expr(p, array->bound_expr);

	return p;
}

/* Print the declaration of an array argument.
 * "memory_space" allows to specify a memory space prefix.
 */
__isl_give isl_printer *gpu_array_info_print_declaration_argument(
	__isl_take isl_printer *p, struct gpu_array_info *array,
	const char *memory_space)
{
	if (gpu_array_is_read_only_scalar(array)) {
		p = isl_printer_print_str(p, array->type);
		p = isl_printer_print_str(p, " ");
		p = isl_printer_print_str(p, array->name);
		return p;
	}

	if (memory_space) {
		p = isl_printer_print_str(p, memory_space);
		p = isl_printer_print_str(p, " ");
	}

	if (array->n_index != 0 && !array->linearize)
		return print_non_linearized_declaration_argument(p, array);

	p = isl_printer_print_str(p, array->type);
	p = isl_printer_print_str(p, " ");
	p = isl_printer_print_str(p, "*");
	p = isl_printer_print_str(p, array->name);

	return p;
}

/* Print the call of an array argument.
 */
__isl_give isl_printer *gpu_array_info_print_call_argument(
	__isl_take isl_printer *p, struct gpu_array_info *array)
{
	if (gpu_array_is_read_only_scalar(array))
		return isl_printer_print_str(p, array->name);

	p = isl_printer_print_str(p, "dev_");
	p = isl_printer_print_str(p, array->name);

	return p;
}

/* Print an access to the element in the private/shared memory copy
 * described by "stmt".  The index of the copy is recorded in
 * stmt->local_index as an access to the array.
 */
static __isl_give isl_printer *stmt_print_local_index(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	return isl_printer_print_ast_expr(p, stmt->u.c.local_index);
}

/* Print an access to the element in the global memory copy
 * described by "stmt".  The index of the copy is recorded in
 * stmt->index as an access to the array.
 */
static __isl_give isl_printer *stmt_print_global_index(
	__isl_take isl_printer *p, struct ppcg_kernel_stmt *stmt)
{
	struct gpu_array_info *array = stmt->u.c.array;
	isl_ast_expr *index;

	if (gpu_array_is_scalar(array)) {
		if (!gpu_array_is_read_only_scalar(array))
			p = isl_printer_print_str(p, "*");
		p = isl_printer_print_str(p, array->name);
		return p;
	}

	index = isl_ast_expr_copy(stmt->u.c.index);

	p = isl_printer_print_ast_expr(p, index);
	isl_ast_expr_free(index);

	return p;
}

/* Print a copy statement.
 *
 * A read copy statement is printed as
 *
 *	local = global;
 *
 * while a write copy statement is printed as
 *
 *	global = local;
 */
__isl_give isl_printer *ppcg_kernel_print_copy(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	p = isl_printer_start_line(p);
	if (stmt->u.c.read) {
		p = stmt_print_local_index(p, stmt);
		p = isl_printer_print_str(p, " = ");
		p = stmt_print_global_index(p, stmt);
	} else {
		p = stmt_print_global_index(p, stmt);
		p = isl_printer_print_str(p, " = ");
		p = stmt_print_local_index(p, stmt);
	}
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);

	return p;
}

/* Print break statement for a dynamic counted loop. It first check the condition
 * of the saved statement in a dyanic counted loop (its loop body with an if condition)
 * If found, try to match the loop iterator with the condition. When matched, print
 * the loop iterator and correct condition.
 */
__isl_give isl_printer *ppcg_kernel_print_break(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	pet_tree *tree = stmt->u.b.stmt->u.d.stmt->stmt->body;
	isl_id *id = stmt->u.b.loop_id;
	int is_inner =  stmt->u.b.is_inner;
	pet_expr *expr;

	if(pet_tree_get_type(tree) == pet_tree_if){
		expr = pet_tree_if_get_cond(tree);
		expr = condition_has_loop_iterator(expr, id, stmt->u.b.stmt->u.d.ref2expr);
		if(!expr)
			return p;

		const char* loop_iterator = isl_id_get_name(id);
		char* operator;
		int op = pet_expr_op_get_type(expr);
		if(op == pet_op_le)
			operator = ">";
		else if(op == pet_op_ge)
			operator = "<";
		else if(op == pet_op_lt)
			operator = ">=";
		else if(op == pet_op_gt)
			operator = "<=";
		else if(op == pet_op_eq)
			operator = "!=";
		else if(op == pet_op_ne)
			operator = "==";
		//expr = pet_expr_get_arg(expr, pet_expr_get_n_arg(expr) - 1);
		
	
		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p, "if(");
		if(is_inner)
			p = print_pet_expr(p, pet_expr_get_arg(expr, 0), 1, stmt->u.b.stmt->u.d.ref2expr);
		else
			p = isl_printer_print_str(p, loop_iterator);
		p = isl_printer_print_str(p, " ");
		p = isl_printer_print_str(p, operator);
		p = isl_printer_print_str(p, " ");
		p = print_pet_expr(p, pet_expr_get_arg(expr, pet_expr_get_n_arg(expr) - 1), 1, stmt->u.b.stmt->u.d.ref2expr);
		p = isl_printer_print_str(p, ")");
		p = isl_printer_end_line(p);
		p = isl_printer_start_line(p); 
		//p = isl_printer_print_str(p, "break;");
		p = isl_printer_print_str(p, "  goto label_for_");
		p = isl_printer_print_str(p, loop_iterator);
		p = isl_printer_print_str(p, ";");
		p = isl_printer_end_line(p);
		return p;
	}
	
	return p;
}

__isl_give isl_printer *ppcg_kernel_print_domain(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	return pet_stmt_print_body(stmt->u.d.stmt->stmt, p, stmt->u.d.ref2expr);
}

/* This function is called for each node in a GPU AST.
 * In case of a user node, print the macro definitions required
 * for printing the AST expressions in the annotation, if any.
 * For other nodes, return true such that descendants are also
 * visited.
 *
 * In particular, for a kernel launch, print the macro definitions
 * needed for the grid size.
 * For a copy statement, print the macro definitions needed
 * for the two index expressions.
 * For an original user statement, print the macro definitions
 * needed for the substitutions.
 */
static isl_bool at_node(__isl_keep isl_ast_node *node, void *user)
{
	const char *name;
	isl_id *id;
	int is_kernel;
	struct ppcg_kernel *kernel;
	struct ppcg_kernel_stmt *stmt;
	isl_printer **p = user;

	if (isl_ast_node_get_type(node) != isl_ast_node_user)
		return isl_bool_true;

	id = isl_ast_node_get_annotation(node);
	if (!id)
		return isl_bool_false;

	name = isl_id_get_name(id);
	if (!name)
		return isl_bool_error;
	is_kernel = !strcmp(name, "kernel");
	kernel = is_kernel ? isl_id_get_user(id) : NULL;
	stmt = is_kernel ? NULL : isl_id_get_user(id);
	isl_id_free(id);

	if ((is_kernel && !kernel) || (!is_kernel && !stmt))
		return isl_bool_error;

	if (is_kernel) {
		*p = ppcg_ast_expr_print_macros(kernel->grid_size_expr, *p);
	} else if (stmt->type == ppcg_kernel_copy) {
		*p = ppcg_ast_expr_print_macros(stmt->u.c.index, *p);
		*p = ppcg_ast_expr_print_macros(stmt->u.c.local_index, *p);
	} else if (stmt->type == ppcg_kernel_domain) {
		*p = ppcg_print_body_macros(*p, stmt->u.d.ref2expr);
	}
	if (!*p)
		return isl_bool_error;

	return isl_bool_false;
}

/* Print the required macros for the GPU AST "node" to "p",
 * including those needed for the user statements inside the AST.
 */
__isl_give isl_printer *gpu_print_macros(__isl_take isl_printer *p,
	__isl_keep isl_ast_node *node)
{
	if (isl_ast_node_foreach_descendant_top_down(node, &at_node, &p) < 0)
		return isl_printer_free(p);
	p = ppcg_print_macros(p, node);
	return p;
}

/* Was the definition of "type" printed before?
 * That is, does its name appear in the list of printed types "types"?
 */
static int already_printed(struct gpu_types *types,
	struct pet_type *type)
{
	int i;

	for (i = 0; i < types->n; ++i)
		if (!strcmp(types->name[i], type->name))
			return 1;

	return 0;
}

/* Print the definitions of all types prog->scop that have not been
 * printed before (according to "types") on "p".
 * Extend the list of printed types "types" with the newly printed types.
 */
__isl_give isl_printer *gpu_print_types(__isl_take isl_printer *p,
	struct gpu_types *types, struct gpu_prog *prog)
{
	int i, n;
	isl_ctx *ctx;
	char **name;

	n = prog->scop->pet->n_type;

	if (n == 0)
		return p;

	ctx = isl_printer_get_ctx(p);
	name = isl_realloc_array(ctx, types->name, char *, types->n + n);
	if (!name)
		return isl_printer_free(p);
	types->name = name;

	for (i = 0; i < n; ++i) {
		struct pet_type *type = prog->scop->pet->types[i];

		if (already_printed(types, type))
			continue;

		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p, type->definition);
		p = isl_printer_print_str(p, ";");
		p = isl_printer_end_line(p);

		types->name[types->n++] = strdup(type->name);
	}

	return p;
}

static __isl_give isl_printer *print_prog_name(__isl_take isl_printer *p, struct gpu_prog *prog) {
	const char *funcname = pet_scop_get_function_name(prog->scop->pet);
	if (funcname) {
		p = isl_printer_print_str(p, "__ppcg_");
		p = isl_printer_print_str(p, funcname);
		p = isl_printer_print_str(p, "_prog");
	} else
		p = isl_printer_print_str(p, "__ppcg_prog");
	p = isl_printer_print_int(p, prog->id);
	return p;
}

static __isl_give isl_printer* foreach_prog_arg(__isl_take isl_printer *p, struct gpu_prog *prog,  __isl_give isl_printer *(*callback)(__isl_take isl_printer *p, struct gpu_prog *prog, bool first, isl_id *param, struct gpu_array_info *array,  void *user), void *user) {
	bool firstarg = true;

	// Parameter arguments
	int n_params = isl_set_n_param(prog->context);
	for (int i = 0; i < n_params; ++i) {
		isl_id *id = isl_set_get_dim_id(prog->context, isl_dim_param, i);
		p = (*callback)(p, prog, firstarg, id, NULL, user);
		isl_id_free(id);
		firstarg = false;
	}

	// Array arguments
	for (int i = 0; i < prog->n_array; ++i) {
		struct gpu_array_info *array = &prog->array[i];
		//pet_array_dump(array->);
		//printf("ARRAY %5s %4s [%d]=%d read_only_scalar=%d local=%d declare_local=%d global=%d linearize=%d\n", array->type, array->name, array->n_index, array->size, array->read_only_scalar, array->local, array->declare_local, array->global, array->linearize);
		if (array->local)
			continue;
		if (!array->accessed)
			continue;
		if (gpu_array_is_read_only_scalar(array) && (isl_set_find_dim_by_name(prog->context, isl_dim_param, array->name)>=0))
			continue; // Already declared as parameter
		//assert((gpu_array_is_read_only_scalar(array) || gpu_array_requires_device_allocation(array)) && "non-device allocated written scalar might be miscompiled (need to pass by pointer)");
		assert(array->linearize && "TODO: Implement passing arrays of fixed size as arrays (Compatiable with C and C++)");

		p = (*callback)(p, prog, firstarg, NULL, array, user);
		firstarg = false;
	}

	return p;
}

static __isl_give isl_printer * callback_print_prog_parameter(__isl_take isl_printer *p, struct gpu_prog *prog, bool first, __isl_keep isl_id *param, struct gpu_array_info *array, void *user) {
	isl_bool has_custom_types = *((isl_bool*)user);

	if (!first)
		p = isl_printer_print_str(p, ", ");

	if (param) {
		p = isl_printer_print_str(p, "int ");
		p = isl_printer_print_str(p, isl_id_get_name(param));
	}
	if (array) {
		if (gpu_array_is_read_only_scalar(array)) {
			p = isl_printer_print_str(p, array->type);
			p = isl_printer_print_str(p, " ");
			p = isl_printer_print_str(p, array->name);
		} else {
			if (has_custom_types || strcmp(array->type, "float")==0 || strcmp(array->type, "double")==0 || strcmp(array->type, "int")==0 || strcmp(array->type, "char")==0 || strcmp(array->type, "short")==0 )
				p = isl_printer_print_str(p, array->type);
			else
				p = isl_printer_print_str(p, "void"); // To avoid requiring including any headers/declaring types
			p = isl_printer_print_str(p, " *");
			p = isl_printer_print_str(p, array->name);
		}
	}

	return p;
}

static __isl_give isl_printer * callback_print_prog_argument(__isl_take isl_printer *p, struct gpu_prog *prog, bool first, __isl_keep isl_id *param, struct gpu_array_info *array, void *user) {
	if (!first)
		p = isl_printer_print_str(p, ", ");

	if (param) {
		p = isl_printer_print_str(p, isl_id_get_name(param));
	}
	if (array) {
		if (gpu_array_is_read_only_scalar(array)) {
			p = isl_printer_print_str(p, array->name);
		} else if (gpu_array_is_scalar(array)) {
			p = isl_printer_print_str(p, "(");
			p = isl_printer_print_str(p, array->type); // cast away e.g. const
			p = isl_printer_print_str(p, "*)&");
			p = isl_printer_print_str(p, array->name);
		} else {
			p = isl_printer_print_str(p, "(");
			p = isl_printer_print_str(p, array->type); // cast away e.g. const
			p = isl_printer_print_str(p, "*)&");
			p = isl_printer_print_str(p, array->name);
			p = isl_printer_print_str(p, "[0]"); // This might be a pointer or decay-to-pointer array; this should handle both
		}
	}

	return p;
}

__isl_give isl_printer *gpu_print_prog_declaration(__isl_take isl_printer *p, struct gpu_prog *prog, isl_bool has_custom_types) {
	p = isl_printer_print_str(p, "void ");
	p = print_prog_name(p, prog);
	p = isl_printer_print_str(p, "(");
	p = foreach_prog_arg(p, prog, &callback_print_prog_parameter, &has_custom_types);
	p = isl_printer_print_str(p, ")");
	return p;
}

__isl_give isl_printer *gpu_print_prog_invocation(__isl_take isl_printer *p, struct gpu_prog *prog) {
	p = isl_printer_start_line(p);
	p = print_prog_name(p, prog);
	p = isl_printer_print_str(p, "(");
	p = foreach_prog_arg(p, prog, &callback_print_prog_argument, NULL);
	p = isl_printer_print_str(p, ");");
	p = isl_printer_end_line(p);
	return p;
}

