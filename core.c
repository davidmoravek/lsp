#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "core.h"

#define SYMBOL_MAX_LENGTH 128

const char symbol_special_chars[] = "+-_<>=?*";

/**
 * PREDEFINED ATOMS
 */

Object *True = &(Object) { OT_TRUE };
Object *Nil = &(Object) { OT_NIL };

/**
 * HELPERS
 */

static Object *new_object(ObjectType t) {
	Object *o = (Object *) malloc(sizeof(Object));
	o->type = t;
	return o;
}

static Object *cons(Object *car, Object *cdr) {
	Object *o = new_object(OT_CONS);
	o->car = car;
	o->cdr = cdr;
	return o;
}

static int count(Object *list) {
	int cnt = 0;
	for (; list->type == OT_CONS; list = list->cdr) {
		cnt++;
	}
	return list == Nil ? cnt : -1;
}

static void err(char *msg, ...) {
	printf("%s\n", msg);
	exit(1);
}

/**
 * RECURSIVE DESCENT PARSER
 */

static int peek();
static Object *read_number();
static Object *read_symbol();
static Object *read_quoted_symbol();
static Object *read_list();
static Object *read_quote();

Object *read() {
	for (;;) {
		int c = getchar();
		if (isspace(c)) {
			continue;
		}
		if (c == EOF) {
			return NULL;
		}
		if (isdigit(c)) {
			ungetc(c, stdin);
			return read_number();
		}
		if (c == '-' && isdigit(peek())) {
			return read_number(0);
		}
		if (isalpha(c) || strchr(symbol_special_chars, c)) {
			ungetc(c, stdin);
			return read_symbol();
		}
		if (c == '"') {
			return read_quoted_symbol();
		}
		if (c == '(') {
			return read_list();
		}
		if (c == '\'') {
			return read_quote();
		}
		err("Syntax error\n");
	}
}

// PRIVATE

static int peek() {
	int c = getchar();
	ungetc(c, stdin);
	return c;
}

static Object *read_number(int isPositive) {
	int v = 0;
	while (isdigit(peek()))
		v = v * 10 + (getchar() - '0');
	Object *o = new_object(OT_INT);
	o->value = isPositive ? v : -v;
	return o;
}

static Object *read_symbol() {
	char buf[SYMBOL_MAX_LENGTH + 1];
	int i = 0;
	while (isalnum(peek()) || strchr(symbol_special_chars, peek())) {
		if (i >= SYMBOL_MAX_LENGTH) {
			err("Symbol name is too long");
		}
		buf[i++] = getchar();
	}
	buf[i] = '\0';
	Object *o = new_object(OT_SYMBOL);
	o->name = malloc(strlen(buf) + 1);
	strcpy(o->name, buf);
	return o;
}

static Object *read_quoted_symbol() {
	char buf[SYMBOL_MAX_LENGTH + 1];
	int i = 0;
	int c;
	while ((c = getchar()) != '"') {
		if (i >= SYMBOL_MAX_LENGTH) {
			err("Symbol name is too long");
		}
		buf[i++] = c;
	}
	buf[i] = '\0';
	Object *o = new_object(OT_SYMBOL);
	o->name = malloc(strlen(buf) + 1);
	strcpy(o->name, buf);
	return o;
}

static Object *read_list() {
	Object *first = Nil;
	Object *last = Nil;
	for (;;) {
		int c = getchar();
		if (isspace(c)) {
			continue;
		}
		if (c == ')') {
			return first;
		}
		ungetc(c, stdin);
		if (first == Nil) {
			first = last = cons(read(), Nil);
		} else {
			last = last->cdr = cons(read(), Nil);
		}
	}
}

static Object *read_quote() {
	Object *o = new_object(OT_SYMBOL);
	o->name = "quote";
	return cons(o, cons(read(), Nil));
}

/**
 * EVAL
 */

static Object *apply(Env *, Object *, Object*);
static Env *env_create();
static Object *env_lookup(Env *env, char *name);
static Object *progn(Env *, Object *);

Object *eval(Env *env, Object *o) {
	if (o == NULL) {
		return NULL;
	}
	switch (o->type) {
		case OT_INT:
		case OT_FUNCTION:
		case OT_TRUE:
		case OT_NIL:
			return o;
		case OT_CONS: {
			Object *fn = eval(env, o->car);
			Object *args = o->cdr;
			if (fn->type != OT_PRIMITIVE && fn->type != OT_FUNCTION) {
				err("The first element of list must be a function");
			}
			if (args->type != OT_NIL && args->type != OT_CONS) {
				err("Function argument must be a list");
			}
			return apply(env, fn, args);
		}
		case OT_SYMBOL: {
			return env_lookup(env, o->name);
		}
		default:
			return NULL;
	}
}

static Object *eval_args(Env *env, Object *o) {
	Object *first = Nil;
	Object *last = Nil;
	for (; o != Nil; o = o->cdr) {
		if (first == Nil) {
			first = last = cons(eval(env, o->car), Nil);
		} else {
			last = last->cdr = cons(eval(env, o->car), Nil);
		}
	}
	return first;
}

static Object *progn(Env *env, Object *args) {
	Object *last;
	for (; args != Nil; args = args->cdr) {
		last = eval(env, args->car);
	}
	return last;
}

static Object *apply(Env *env, Object* o, Object *args) {
	if (o->type == OT_PRIMITIVE) {
		return o->function(env, args);
	}
	if (o->type == OT_FUNCTION) {
		Object *params = o->params;
		args = eval_args(env, args);
		Env *env_child = env_create();
		env_child->parent = env;
		for (; params->type == OT_CONS; params = params->cdr, args = args->cdr) {
			ht_insert(env_child->ht, params->car->name, args->car);
		}
		return progn(env_child, o->body);
	}
	return NULL;
}

/**
 * Print
 */

void print(Object *o) {
	switch(o->type) {
		case OT_INT:
			printf("%d", o->value);
			break;
		case OT_SYMBOL:
			printf("%s", o->name);
			break;
		case OT_CONS:
			printf("(");
			for (;;) {
				print(o->car);
				if (o->cdr == Nil) {
					break;
				}
				printf(" ");
				o = o->cdr;
			}
			printf(")");
			break;
		case OT_NIL:
			printf("Nil");
			break;
		case OT_TRUE:
			printf("True");
			break;
		case OT_PRIMITIVE:
			printf("<primitive>");
			break;
		case OT_FUNCTION:
			printf("<function>");
			break;
		default:
			printf("unhandled");
	}
}

/**
 * Primitives
 */

static Object *create_function(Object *params, Object *body) {
	for (Object *p = params; p->type == OT_CONS; p = p->cdr) {
		if (p->car->type != OT_SYMBOL) {
			err("function parameter must be a symbol");
		}
	}
	Object *fn = new_object(OT_FUNCTION);
	fn->params = params;
	fn->body = body;
	return fn;
}

static Object *primitive_and(Env *env, Object *args) {
	for (; args != Nil; args = args->cdr) {
		if (eval(env, args->car) == Nil) {
			return Nil;
		}
	}
	return True;
}

static Object *primitive_or(Env *env, Object *args) {
	for (; args != Nil; args = args->cdr) {
		if (eval(env, args->car) != Nil) {
			return True;
		}
	}
	return Nil;
}

static Object *primitive_car(Env *env, Object *args) {
	args = eval_args(env, args);
	if (args->car->type != OT_CONS || args->cdr != Nil) {
		err("car accepts single list argument only");
	}
	return args->car->car;
}

static Object *primitive_cdr(Env *env, Object *args) {
	args = eval_args(env, args);
	if (args->car->type != OT_CONS|| args->cdr != Nil) {
		err("cdr accepts single list argument only");
	}
	return args->car->cdr;
}

static Object *primitive_cons(Env *env, Object *args) {
	if (count(args) != 2) {
		err("cons accepts two arguments only");
	}
	args = eval_args(env, args);
	args->cdr = args->cdr->car;
	return args;
}

static Object *primitive_define(Env *env, Object *args) {
	if (count(args) != 2 || args->car->type != OT_SYMBOL) {
		err("define accepts two arguments only, with first one being a symbol");
	}
	Object *value = eval(env, args->cdr->car);
	ht_insert(env->ht, args->car->name, value);
	return value;
}

static Object *primitive_eq(Env *env, Object *args) {
	if (count(args) != 2) {
		err("= accepts two arguments only");
	}
	args = eval_args(env, args);
	if (args->car->type != OT_INT || args->cdr->car->type != OT_INT) {
		err("= accepts integers only");
	}
	return args->car->value == args->cdr->car->value ? True : Nil;
}

static Object *primitive_gt(Env *env, Object *args) {
	if (count(args) != 2) {
		err("> accepts two arguments only");
	}
	args = eval_args(env, args);
	if (args->car->type != OT_INT || args->cdr->car->type != OT_INT) {
		err("> accepts integers only");
	}
	return args->car->value > args->cdr->car->value ? True : Nil;
}

static Object *primitive_if(Env *env, Object *args) {
	int cnt = count(args);
	if (cnt != 2 && cnt != 3) {
		err("if needs two or three arguments");
	}
	if (eval(env, args->car) != Nil) {
		return eval(env, args->cdr->car);
	}
	return cnt == 2 ? Nil : eval(env, args->cdr->cdr->car);
}

static Object *primitive_lt(Env *env, Object *args) {
	if (count(args) != 2) {
		err("< accepts two arguments only");
	}
	args = eval_args(env, args);
	if (args->car->type != OT_INT || args->cdr->car->type != OT_INT) {
		err("< accepts integers only");
	}
	return args->car->value < args->cdr->car->value ? True : Nil;
}

static Object *primitive_lambda(Env *env, Object *args) {
	return create_function(args->car, args->cdr);
}

static Object *primitive_minus(Env *env, Object *args) {
	int diff;
	int first = 1;
	for (args = eval_args(env, args); args != Nil; args = args->cdr) {
		if (args->car->type != OT_INT) {
			err("+ accepts only integers");
		}
		if (first) {
			diff = args->car->value;
			first = 0;
			if (args->cdr == Nil) {
				diff = -diff;
				break;
			}
		} else {
			diff -= args->car->value;
		}
	}
	Object *o = new_object(OT_INT);
	o->value = diff;
	return o;
}

static Object *primitive_multi(Env *env, Object *args) {
	if (count(args) < 2) {
		err("* accepts at least two arguments");
	}
	int first = 1;
	int total;
	for (args = eval_args(env, args); args != Nil; args = args->cdr) {
		if (args->car->type != OT_INT) {
			err("* accepts only integers");
		}
		if (first) {
			total = args->car->value;
			first = 0;
		} else {
			total *= args->car->value;
		}
	}
	Object *o = new_object(OT_INT);
	o->value = total;
	return o;
}

static Object *primitive_obj_eq(Env *env, Object *args) {
	if (count(args) != 2) {
		err("eq accepts two arguments only");
	}
	args = eval_args(env, args);
	return args->car == args->cdr->car ? True : Nil;
}

static Object *primitive_plus(Env *env, Object *args) {
	int sum = 0;
	for (args = eval_args(env, args); args != Nil; args = args->cdr) {
		if (args->car->type != OT_INT) {
			err("+ accepts only integers");
		}
		sum += args->car->value;
	}
	Object *o = new_object(OT_INT);
	o->value = sum;
	return o;
}

static Object *primitive_println(Env *env, Object *args) {
	if (count(args) != 1) {
		err("println takes one argument only");
	}
	print(eval(env, args->car));
	printf("\n");
	return Nil;
}

static Object *primitive_progn(Env *env, Object *args) {
	return progn(env, args);
}

static Object *primitive_setcar(Env *env, Object *args) {
	args = eval_args(env, args);
	if (count(args) != 2 || args->car->type != OT_CONS) {
		err ("setcar accepts two arguments only, with first being a cons cell");
	}
	args->car->car = args->cdr->car;
	return args->car;
}

static Object *primitive_quote(Env *env, Object *args) {
	if (count(args) != 1) {
		err("quote accepts one argument only");
	}
	return args->car;
}

static Object *primitive_while(Env *env, Object *args) {
	if (count(args) < 2) {
		err("while needs at least two arguments");
	}
	while (eval(env, args->car) != Nil) {
		eval_args(env, args->cdr);
	}
	return Nil;
}

/**
 * Composite primitives :)
 */

static Object *primitive_defun(Env *env, Object *args) {
	Object *fn = primitive_lambda(env, args->cdr);
	args->cdr = cons(fn, Nil);
	return primitive_define(env, args);
}

/**
 * ENV
 */

static void add_primitive(Env *env, char *name, Primitive *fn) {
	Object *o = new_object(OT_PRIMITIVE);
	o->function = fn;
	ht_insert(env->ht, name, o);
}

static Env *env_create() {
	Env *env = (Env *) malloc(sizeof(Env));
	env->ht = ht_create(128);
	env->parent = NULL;
	return env;
}

Env *env_init() {
	Env *env = env_create();
	ht_insert(env->ht, "Nil", Nil);
	ht_insert(env->ht, "True", True);
	add_primitive(env, "and", primitive_and);
	add_primitive(env, "car", primitive_car);
	add_primitive(env, "cdr", primitive_cdr);
	add_primitive(env, "cons", primitive_cons);
	add_primitive(env, "define", primitive_define);
	add_primitive(env, "defun", primitive_defun);
	add_primitive(env, "=", primitive_eq);
	add_primitive(env, ">", primitive_gt);
	add_primitive(env, "if", primitive_if);
	add_primitive(env, "<", primitive_lt);
	add_primitive(env, "lambda", primitive_lambda);
	add_primitive(env, "-", primitive_minus);
	add_primitive(env, "*", primitive_multi);
	add_primitive(env, "eq", primitive_obj_eq);
	add_primitive(env, "or", primitive_or);
	add_primitive(env, "+", primitive_plus);
	add_primitive(env, "println", primitive_println);
	add_primitive(env, "progn", primitive_progn);
	add_primitive(env, "setcar", primitive_setcar);
	add_primitive(env, "quote", primitive_quote);
	add_primitive(env, "while", primitive_while);
	return env;
}

static Object *env_lookup(Env *env, char *name) {
	HashTableList *list = ht_lookup(env->ht, name);
	if (list == NULL) {
		if (env->parent) {
			return env_lookup(env->parent, name);
		}
		printf("Undefined symbol: %s\n", name);
		exit(1);
	}
	return list->data;
}
