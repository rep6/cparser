#include <config.h>

#include <assert.h>
#include <stdarg.h>

#include "parser.h"
#include "lexer.h"
#include "token_t.h"
#include "type_t.h"
#include "type_hash.h"
#include "ast_t.h"
#include "adt/bitfiddle.h"
#include "adt/error.h"
#include "adt/array.h"

//#define PRINT_TOKENS
#define MAX_LOOKAHEAD 2

struct environment_entry_t {
	symbol_t      *symbol;
	declaration_t *old_declaration;
	const void    *old_context;
};

static token_t               token;
static token_t               lookahead_buffer[MAX_LOOKAHEAD];
static int                   lookahead_bufpos;
static struct obstack        environment_obstack;
static environment_entry_t **environment_stack = NULL;
static context_t            *context           = NULL;
static declaration_t        *last_declaration  = NULL;

static
statement_t *parse_compound_statement(void);
static
statement_t *parse_statement(void);

static
expression_t *parse_sub_expression(unsigned precedence);
static
expression_t *parse_expression(void);

static inline
void *allocate_ast_zero(size_t size)
{
	void *res = allocate_ast(size);
	memset(res, 0, size);
	return res;
}

static inline
void *allocate_type_zero(size_t size)
{
	void *res = obstack_alloc(type_obst, size);
	memset(res, 0, size);
	return res;
}

/**
 * returns the top element of the environment stack
 */
static inline
size_t environment_top()
{
	return ARR_LEN(environment_stack);
}



static inline
void next_token(void)
{
	token                              = lookahead_buffer[lookahead_bufpos];
	lookahead_buffer[lookahead_bufpos] = lexer_token;
	lexer_next_token();

	lookahead_bufpos = (lookahead_bufpos+1) % MAX_LOOKAHEAD;

#ifdef PRINT_TOKENS
	print_token(stderr, &token);
	fprintf(stderr, "\n");
#endif
}

static inline
const token_t *la(int num)
{
	assert(num > 0 && num <= MAX_LOOKAHEAD);
	int pos = (num-1) % MAX_LOOKAHEAD;
	return & lookahead_buffer[pos];
}

static inline
void eat(token_type_t type)
{
	assert(token.type == type);
	next_token();
}

void parser_print_error_prefix_pos(const source_position_t source_position)
{
    fputs(source_position.input_name, stderr);
    fputc(':', stderr);
    fprintf(stderr, "%d", source_position.linenr);
    fputs(": error: ", stderr);
}

void parser_print_error_prefix(void)
{
	parser_print_error_prefix_pos(token.source_position);
}

static
void parse_error(const char *message)
{
	parser_print_error_prefix();
	fprintf(stderr, "parse error: %s\n", message);
}

static
void parse_error_expected(const char *message, ...)
{
	va_list args;
	int first = 1;

	if(message != NULL) {
		parser_print_error_prefix();
		fprintf(stderr, "%s\n", message);
	}
	parser_print_error_prefix();
	fputs("Parse error: got ", stderr);
	print_token(stderr, &token);
	fputs(", expected ", stderr);

	va_start(args, message);
	token_type_t token_type = va_arg(args, token_type_t);
	while(token_type != 0) {
		if(first == 1) {
			first = 0;
		} else {
			fprintf(stderr, ", ");
		}
		print_token_type(stderr, token_type);
		token_type = va_arg(args, token_type_t);
	}
	va_end(args);
	fprintf(stderr, "\n");
}

static
void eat_until(int token_type)
{
	while(token.type != token_type) {
		if(token.type == T_EOF)
			return;
		next_token();
	}
	next_token();
}

#define expect(expected)                           \
    if(UNLIKELY(token.type != (expected))) {       \
        parse_error_expected(NULL, (expected), 0); \
        eat_until(';');                            \
        return NULL;                               \
    }                                              \
    next_token();

#define expect_void(expected)                      \
    if(UNLIKELY(token.type != (expected))) {       \
        parse_error_expected(NULL, (expected), 0); \
        eat_until(';');                            \
        return;                                    \
    }                                              \
    next_token();

static void set_context(context_t *new_context)
{
	context = new_context;

	declaration_t *declaration = new_context->declarations;
	if(declaration != NULL) {
		while(1) {
			if(declaration->next == NULL)
				break;
			declaration = declaration->next;
		}
	}

	last_declaration = declaration;
}

/**
 * pushs an environment_entry on the environment stack and links the
 * corresponding symbol to the new entry
 */
static inline
void environment_push(declaration_t *declaration, const void *context)
{
	environment_entry_t *entry
		= obstack_alloc(&environment_obstack, sizeof(entry[0]));
	memset(entry, 0, sizeof(entry[0]));

	int top = ARR_LEN(environment_stack);
	ARR_RESIZE(environment_stack, top + 1);
	environment_stack[top] = entry;

	assert(declaration->source_position.input_name != NULL);

	symbol_t *symbol = declaration->symbol;
	assert(declaration != symbol->declaration);

	if(symbol->context == context) {
		if(symbol->declaration != NULL) {
			assert(symbol->declaration != NULL);
			parser_print_error_prefix_pos(declaration->source_position);
			fprintf(stderr, "multiple definitions for symbol '%s'.\n",
					symbol->string);
			parser_print_error_prefix_pos(symbol->declaration->source_position);
			fprintf(stderr, "this is the location of the previous declaration.\n");
		}
	}

	entry->old_declaration = symbol->declaration;
	entry->old_context     = symbol->context;
	entry->symbol          = symbol;
	symbol->declaration    = declaration;
	symbol->context        = context;
}

/**
 * pops symbols from the environment stack until @p new_top is the top element
 */
static inline
void environment_pop_to(size_t new_top)
{
	environment_entry_t *entry = NULL;
	size_t top = ARR_LEN(environment_stack);
	size_t i;

	if(new_top == top)
		return;

	assert(new_top < top);
	i = top;
	do {
		entry = environment_stack[i - 1];

		symbol_t *symbol = entry->symbol;

		symbol->declaration = entry->old_declaration;
		symbol->context     = entry->old_context;

		--i;
	} while(i != new_top);
	obstack_free(&environment_obstack, entry);

	ARR_SHRINKLEN(environment_stack, (int) new_top);
}



static expression_t *parse_constant_expression(void)
{
	/* TODO: not correct yet */
	return parse_expression();
}

static expression_t *parse_assignment_expression(void)
{
	/* TODO: not correct yet */
	return parse_expression();
}

static void parse_compound_type_entries(void);
static void parse_declarator(declaration_t *declaration,
                             storage_class_t storage_class, type_t *type,
                             int may_omit_identifier);
static void maybe_push_declaration(declaration_t *declaration);
static void record_declaration(declaration_t *declaration);

typedef struct declaration_specifiers_t  declaration_specifiers_t;
struct declaration_specifiers_t {
	storage_class_t  storage_class;
	type_t          *type;
};

static type_t *parse_struct_specifier(void)
{
	eat(T_struct);

	compound_type_t *struct_type = allocate_type_zero(sizeof(struct_type[0]));
	struct_type->type.type       = TYPE_COMPOUND_STRUCT;
	struct_type->source_position = token.source_position;

	int         top          = environment_top();
	context_t  *last_context = context;
	set_context(&struct_type->context);

	if(token.type == T_IDENTIFIER) {
		next_token();
		if(token.type == '{') {
			parse_compound_type_entries();
		}
	} else if(token.type == '{') {
		parse_compound_type_entries();
	} else {
		parse_error_expected("problem while parsing struct type specifiers",
		                     T_IDENTIFIER, '{', 0);
		struct_type = NULL;
	}

	assert(context == &struct_type->context);
	set_context(last_context);
	environment_pop_to(top);

	return (type_t*) struct_type;
}

static type_t *parse_union_specifier(void)
{
	eat(T_union);

	compound_type_t *union_type = allocate_type_zero(sizeof(union_type[0]));
	union_type->type.type       = TYPE_COMPOUND_UNION;
	union_type->source_position = token.source_position;

	int         top          = environment_top();
	context_t  *last_context = context;
	set_context(&union_type->context);

	if(token.type == T_IDENTIFIER) {
		union_type->symbol = token.v.symbol;
		next_token();
		if(token.type == '{') {
			parse_compound_type_entries();
		}
	} else if(token.type == '{') {
		parse_compound_type_entries();
	} else {
		parse_error_expected("problem while parsing union type specifiers",
		                     T_IDENTIFIER, '{');
		union_type = NULL;
	}

	assert(context == &union_type->context);
	set_context(last_context);
	environment_pop_to(top);

	return (type_t*) union_type;
}

static void parse_enum_type_entries()
{
	eat('{');

	if(token.type == '}') {
		next_token();
		parse_error("empty enum not allowed");
		return;
	}

	do {
		if(token.type != T_IDENTIFIER) {
			parse_error_expected("problem while parsing enum entry",
			                     T_IDENTIFIER, 0);
			eat_until('}');
			return;
		}
		next_token();

		if(token.type == '=') {
			parse_constant_expression();
		}

		if(token.type != ',')
			break;
		next_token();
	} while(token.type != '}');

	expect_void('}');
}

static type_t *parse_enum_specifier(void)
{
	eat(T_enum);

	enum_type_t *enum_type     = allocate_type_zero(sizeof(enum_type[0]));
	enum_type->type.type       = TYPE_ENUM;
	enum_type->source_position = token.source_position;

	if(token.type == T_IDENTIFIER) {
		enum_type->symbol = token.v.symbol;
		next_token();
		if(token.type == '{') {
			parse_enum_type_entries();
		}
	} else if(token.type == '{') {
		parse_enum_type_entries();
	} else {
		parse_error_expected("problem while parsing enum type specifiers",
		                     T_IDENTIFIER, '{');
	}

	return (type_t*) enum_type;
}

typedef enum {
	SPECIFIER_SIGNED    = 1 << 0,
	SPECIFIER_UNSIGNED  = 1 << 1,
	SPECIFIER_LONG      = 1 << 2,
	SPECIFIER_INT       = 1 << 3,
	SPECIFIER_DOUBLE    = 1 << 4,
	SPECIFIER_CHAR      = 1 << 5,
	SPECIFIER_SHORT     = 1 << 6,
	SPECIFIER_LONG_LONG = 1 << 7,
	SPECIFIER_FLOAT     = 1 << 8,
	SPECIFIER_BOOL      = 1 << 9,
	SPECIFIER_VOID      = 1 << 10,
#ifdef PROVIDE_COMPLEX
	SPECIFIER_COMPLEX   = 1 << 11,
#endif
#ifdef PROVIDE_IMAGINARY
	SPECIFIER_IMAGINARY = 1 << 12,
#endif
} specifiers_t;

#define STORAGE_CLASSES     \
	case T_typedef:         \
	case T_extern:          \
	case T_static:          \
	case T_auto:            \
	case T_register:

#define TYPE_QUALIFIERS     \
	case T_const:           \
	case T_restrict:        \
	case T_volatile:        \
	case T_inline:          \
	case T___extension__:

#ifdef PROVIDE_COMPLEX
#define COMPLEX_SPECIFIERS  \
	case T__Complex:
#else
#define COMPLEX_SPECIFIERS
#endif

#ifdef PROVIDE_IMAGINARY
#define IMAGINARY_SPECIFIERS \
	case T__Imaginary:
#else
#define IMAGINARY_SPECIFIERS
#endif

#define TYPE_SPECIFIERS     \
	case T_void:            \
	case T_char:            \
	case T_short:           \
	case T_int:             \
	case T_long:            \
	case T_float:           \
	case T_double:          \
	case T_signed:          \
	case T_unsigned:        \
	case T__Bool:           \
	case T_struct:          \
	case T_union:           \
	case T_enum:            \
	COMPLEX_SPECIFIERS      \
	IMAGINARY_SPECIFIERS

#define DECLARATION_START   \
	STORAGE_CLASSES         \
	TYPE_QUALIFIERS         \
	TYPE_SPECIFIERS

static
type_t *create_builtin_type(symbol_t *symbol)
{
	builtin_type_t *type = allocate_type_zero(sizeof(type[0]));
	type->type.type      = TYPE_BUILTIN;
	type->symbol         = symbol;

	type_t *result = typehash_insert((type_t*) type);
	if(result != (type_t*) type) {
		obstack_free(type_obst, type);
	}

	return result;
}

static
void parse_declaration_specifiers(declaration_specifiers_t *specifiers)
{
	declaration_t *declaration;
	type_t        *type            = NULL;
	unsigned       type_qualifiers = 0;
	unsigned       type_specifiers = 0;
	int            newtype         = 0;

	while(1) {
		switch(token.type) {

		/* storage class */
#define MATCH_STORAGE_CLASS(token, class)                                \
		case token:                                                      \
			if(specifiers->storage_class != STORAGE_CLASS_NONE) {        \
				parse_error("multiple storage classes in declaration "   \
				            "specifiers");                               \
			}                                                            \
			specifiers->storage_class = class;                           \
			next_token();                                                \
			break;

		MATCH_STORAGE_CLASS(T_typedef,  STORAGE_CLASS_TYPEDEF)
		MATCH_STORAGE_CLASS(T_extern,   STORAGE_CLASS_EXTERN)
		MATCH_STORAGE_CLASS(T_static,   STORAGE_CLASS_STATIC)
		MATCH_STORAGE_CLASS(T_auto,     STORAGE_CLASS_AUTO)
		MATCH_STORAGE_CLASS(T_register, STORAGE_CLASS_REGISTER)

		/* type qualifiers */
#define MATCH_TYPE_QUALIFIER(token, qualifier)                          \
		case token:                                                     \
			type_qualifiers |= qualifier;                               \
			next_token();                                               \
			break;

		MATCH_TYPE_QUALIFIER(T_const,    TYPE_QUALIFIER_CONST);
		MATCH_TYPE_QUALIFIER(T_restrict, TYPE_QUALIFIER_RESTRICT);
		MATCH_TYPE_QUALIFIER(T_volatile, TYPE_QUALIFIER_VOLATILE);
		MATCH_TYPE_QUALIFIER(T_inline,   TYPE_QUALIFIER_INLINE);

		case T___extension__:
			/* TODO */
			next_token();
			break;

		/* type specifiers */
#define MATCH_SPECIFIER(token, specifier, name)                         \
		case token:                                                     \
			next_token();                                               \
			if(type_specifiers & specifier) {                           \
				parse_error("multiple " name " type specifiers given"); \
			} else {                                                    \
				type_specifiers |= specifier;                           \
			}                                                           \
			break;

		MATCH_SPECIFIER(T_void,       SPECIFIER_VOID,      "void")
		MATCH_SPECIFIER(T_char,       SPECIFIER_CHAR,      "char")
		MATCH_SPECIFIER(T_short,      SPECIFIER_SHORT,     "short")
		MATCH_SPECIFIER(T_int,        SPECIFIER_INT,       "int")
		MATCH_SPECIFIER(T_float,      SPECIFIER_FLOAT,     "float")
		MATCH_SPECIFIER(T_double,     SPECIFIER_DOUBLE,    "double")
		MATCH_SPECIFIER(T_signed,     SPECIFIER_SIGNED,    "signed")
		MATCH_SPECIFIER(T_unsigned,   SPECIFIER_UNSIGNED,  "unsigned")
		MATCH_SPECIFIER(T__Bool,      SPECIFIER_BOOL,      "_Bool")
#ifdef PROVIDE_COMPLEX
		MATCH_SPECIFIER(T__Complex,   SPECIFIER_COMPLEX,   "_Complex")
#endif
#ifdef PROVIDE_IMAGINARY
		MATCH_SPECIFIER(T__Imaginary, SPECIFIER_IMAGINARY, "_Imaginary")
#endif
		case T_long:
			next_token();
			if(type_specifiers & SPECIFIER_LONG_LONG) {
				parse_error("multiple type specifiers given");
			} else if(type_specifiers & SPECIFIER_LONG) {
				type_specifiers |= SPECIFIER_LONG_LONG;
			} else {
				type_specifiers |= SPECIFIER_LONG;
			}
			break;

		/* TODO: if type != NULL for the following rules issue an error */
		case T_struct:
			type = parse_struct_specifier();
			break;
		case T_union:
			type = parse_union_specifier();
			break;
		case T_enum:
			type = parse_enum_specifier();
			break;
		case T___builtin_va_list:
			type = create_builtin_type(token.v.symbol);
			next_token();
			break;

		case T_IDENTIFIER:
			declaration = token.v.symbol->declaration;
			if(declaration == NULL ||
					declaration->storage_class != STORAGE_CLASS_TYPEDEF) {
				goto finish_specifiers;
			}

			type = declaration->type;
			assert(type != NULL);
			next_token();
			break;

		/* function specifier */
		default:
			goto finish_specifiers;
		}
	}

finish_specifiers:

	if(type == NULL) {
		atomic_type_type_t atomic_type;

		/* match valid basic types */
		switch(type_specifiers) {
		case SPECIFIER_VOID:
			atomic_type = ATOMIC_TYPE_VOID;
			break;
		case SPECIFIER_CHAR:
			atomic_type = ATOMIC_TYPE_CHAR;
			break;
		case SPECIFIER_SIGNED | SPECIFIER_CHAR:
			atomic_type = ATOMIC_TYPE_SCHAR;
			break;
		case SPECIFIER_UNSIGNED | SPECIFIER_CHAR:
			atomic_type = ATOMIC_TYPE_UCHAR;
			break;
		case SPECIFIER_SHORT:
		case SPECIFIER_SIGNED | SPECIFIER_SHORT:
		case SPECIFIER_SHORT | SPECIFIER_INT:
		case SPECIFIER_SIGNED | SPECIFIER_SHORT | SPECIFIER_INT:
			atomic_type = ATOMIC_TYPE_SHORT;
			break;
		case SPECIFIER_UNSIGNED | SPECIFIER_SHORT:
		case SPECIFIER_UNSIGNED | SPECIFIER_SHORT | SPECIFIER_INT:
			atomic_type = ATOMIC_TYPE_USHORT;
			break;
		case SPECIFIER_INT:
		case SPECIFIER_SIGNED:
		case SPECIFIER_SIGNED | SPECIFIER_INT:
			atomic_type = ATOMIC_TYPE_INT;
			break;
		case SPECIFIER_UNSIGNED:
		case SPECIFIER_UNSIGNED | SPECIFIER_INT:
			atomic_type = ATOMIC_TYPE_UINT;
			break;
		case SPECIFIER_LONG:
		case SPECIFIER_SIGNED | SPECIFIER_LONG:
		case SPECIFIER_LONG | SPECIFIER_INT:
		case SPECIFIER_SIGNED | SPECIFIER_LONG | SPECIFIER_INT:
			atomic_type = ATOMIC_TYPE_LONG;
			break;
		case SPECIFIER_UNSIGNED | SPECIFIER_LONG:
		case SPECIFIER_UNSIGNED | SPECIFIER_LONG | SPECIFIER_INT:
			atomic_type = ATOMIC_TYPE_ULONG;
			break;
		case SPECIFIER_LONG | SPECIFIER_LONG_LONG:
		case SPECIFIER_SIGNED | SPECIFIER_LONG | SPECIFIER_LONG_LONG:
		case SPECIFIER_LONG | SPECIFIER_LONG_LONG | SPECIFIER_INT:
		case SPECIFIER_SIGNED | SPECIFIER_LONG | SPECIFIER_LONG_LONG
			| SPECIFIER_INT:
			atomic_type = ATOMIC_TYPE_LONGLONG;
			break;
		case SPECIFIER_UNSIGNED | SPECIFIER_LONG | SPECIFIER_LONG_LONG:
		case SPECIFIER_UNSIGNED | SPECIFIER_LONG | SPECIFIER_LONG_LONG
			| SPECIFIER_INT:
			atomic_type = ATOMIC_TYPE_ULONGLONG;
			break;
		case SPECIFIER_FLOAT:
			atomic_type = ATOMIC_TYPE_FLOAT;
			break;
		case SPECIFIER_DOUBLE:
			atomic_type = ATOMIC_TYPE_DOUBLE;
			break;
		case SPECIFIER_LONG | SPECIFIER_DOUBLE:
			atomic_type = ATOMIC_TYPE_LONG_DOUBLE;
			break;
		case SPECIFIER_BOOL:
			atomic_type = ATOMIC_TYPE_BOOL;
			break;
#ifdef PROVIDE_COMPLEX
		case SPECIFIER_FLOAT | SPECIFIER_COMPLEX:
			atomic_type = ATOMIC_TYPE_FLOAT_COMPLEX;
			break;
		case SPECIFIER_DOUBLE | SPECIFIER_COMPLEX:
			atomic_type = ATOMIC_TYPE_DOUBLE_COMPLEX;
			break;
		case SPECIFIER_LONG | SPECIFIER_DOUBLE | SPECIFIER_COMPLEX:
			atomic_type = ATOMIC_TYPE_LONG_DOUBLE_COMPLEX;
			break;
#endif
#ifdef PROVIDE_IMAGINARY
		case SPECIFIER_FLOAT | SPECIFIER_IMAGINARY:
			atomic_type = ATOMIC_TYPE_FLOAT_IMAGINARY;
			break;
		case SPECIFIER_DOUBLE | SPECIFIER_IMAGINARY:
			atomic_type = ATOMIC_TYPE_DOUBLE_IMAGINARY;
			break;
		case SPECIFIER_LONG | SPECIFIER_DOUBLE | SPECIFIER_IMAGINARY:
			atomic_type = ATOMIC_TYPE_LONG_DOUBLE_IMAGINARY;
			break;
#endif
		default:
			/* invalid specifier combination, give an error message */
			if(type_specifiers == 0) {
				parse_error("no type specifiers given in declaration");
			} else if((type_specifiers & SPECIFIER_SIGNED) &&
			          (type_specifiers & SPECIFIER_UNSIGNED)) {
				parse_error("signed and unsigned specifiers gives");
			} else if(type_specifiers & (SPECIFIER_SIGNED | SPECIFIER_UNSIGNED)) {
				parse_error("only integer types can be signed or unsigned");
			} else {
				parse_error("multiple datatypes in declaration");
			}
			atomic_type = ATOMIC_TYPE_INVALID;
		}

		atomic_type_t *atype = allocate_type_zero(sizeof(atype[0]));
		atype->type.type     = TYPE_ATOMIC;
		atype->atype         = atomic_type;
		newtype              = 1;

		type = (type_t*) atype;
	} else {
		if(type_specifiers != 0) {
			parse_error("multiple datatypes in declaration");
		}
	}

	type->qualifiers = type_qualifiers;

	type_t *result = typehash_insert(type);
	if(newtype && result != (type_t*) type) {
		obstack_free(type_obst, type);
	}

	specifiers->type = result;
}

static
unsigned parse_type_qualifiers()
{
	unsigned type_qualifiers = 0;

	while(1) {
		switch(token.type) {
		/* type qualifiers */
		MATCH_TYPE_QUALIFIER(T_const,    TYPE_QUALIFIER_CONST);
		MATCH_TYPE_QUALIFIER(T_restrict, TYPE_QUALIFIER_RESTRICT);
		MATCH_TYPE_QUALIFIER(T_volatile, TYPE_QUALIFIER_VOLATILE);
		MATCH_TYPE_QUALIFIER(T_inline,   TYPE_QUALIFIER_INLINE);

		default:
			return type_qualifiers;
		}
	}
}

static
int parse_pointers(void)
{
	int result = 0;

	while(token.type == '*') {
		next_token();
		result++;
	}

	return result;
}

static
type_t *make_pointers(type_t *type, int count)
{
	for(int i = 0; i < count; ++i) {
		pointer_type_t *pointer_type
			= allocate_type_zero(sizeof(pointer_type[0]));
		pointer_type->type.type = TYPE_POINTER;
		pointer_type->points_to = type;

		pointer_type->type.qualifiers = parse_type_qualifiers();

		type_t *result = typehash_insert((type_t*) pointer_type);
		if(result != (type_t*) pointer_type) {
			obstack_free(type_obst, pointer_type);
		}

		type = result;
	}

	return type;
}

static
void parse_identifier_list(void)
{
	while(1) {
		if(token.type != T_IDENTIFIER) {
			parse_error_expected("problem while parsing parameter identifier "
			                     "list", T_IDENTIFIER, 0);
			return;
		}
		next_token();
		if(token.type != ',')
			break;
		next_token();
	}
}

static
declaration_t *parse_parameter(void)
{
	declaration_specifiers_t specifiers;
	memset(&specifiers, 0, sizeof(specifiers));

	parse_declaration_specifiers(&specifiers);

	declaration_t *declaration = allocate_ast_zero(sizeof(declaration[0]));
	parse_declarator(declaration, specifiers.storage_class,
	                 specifiers.type, 1);

#if 0
	if(declaration->symbol != NULL) {
		maybe_push_declaration(declaration);
		record_declaration(declaration);
	}
#endif

	return declaration;
}

static
void parse_parameters(method_type_t *type)
{
	if(token.type == T_IDENTIFIER) {
		symbol_t      *symbol      = token.v.symbol;
		declaration_t *declaration = symbol->declaration;
		if(declaration == NULL
				|| declaration->storage_class != STORAGE_CLASS_TYPEDEF) {
			parse_identifier_list();
			return;
		}
	}

	if(token.type == ')') {
		type->unspecified_parameters = 1;
		return;
	}
	if(token.type == T_void && la(1)->type == ')') {
		next_token();
		return;
	}

	declaration_t *declaration;
	method_parameter_type_t *parameter_type;
	method_parameter_type_t *last_parameter_type = NULL;

	while(1) {
		switch(token.type) {
		case T_DOTDOTDOT:
			next_token();
			type->variadic = 1;
			return;

		case T_IDENTIFIER:
		DECLARATION_START
			declaration = parse_parameter();

			parameter_type = allocate_type_zero(sizeof(parameter_type[0]));
			parameter_type->type   = declaration->type;
			parameter_type->symbol = declaration->symbol;

			if(last_parameter_type != NULL) {
				last_parameter_type->next = parameter_type;
			} else {
				type->parameter_types = parameter_type;
			}
			last_parameter_type = parameter_type;
			break;

		default:
			return;
		}
		if(token.type != ',')
			return;
		next_token();
	}
}

static
void parse_attributes(void)
{
	while(token.type == T___attribute__) {
		next_token();

		expect_void('(');
		int depth = 1;
		while(depth > 0) {
			switch(token.type) {
			case T_EOF:
				parse_error("EOF while parsing attribute");
				break;
			case '(':
				next_token();
				depth++;
				break;
			case ')':
				next_token();
				depth--;
				break;
			default:
				next_token();
			}
		}
	}
}

typedef struct declarator_part declarator_part;
struct declarator_part {
	int              pointers;
	method_type_t   *method_type;
	declarator_part *inner;
};

static struct obstack  temp_obst;

static
declarator_part *parse_inner_declarator(declaration_t *declaration)
{
	declarator_part *part = obstack_alloc(&temp_obst, sizeof(part[0]));
	memset(part, 0, sizeof(part[0]));

	part->pointers = parse_pointers();

	switch(token.type) {
	case T_IDENTIFIER:
		declaration->symbol          = token.v.symbol;
		declaration->source_position = token.source_position;
		next_token();
		break;
	case '(':
		next_token();
		part->inner = parse_inner_declarator(declaration);
		expect(')');
		break;
	default:
		parse_error_expected("problem while parsing declarator", T_IDENTIFIER,
		                     '(', 0);
	}

	while(1) {
		switch(token.type) {
		case '(':
			next_token();

#if 0
			int         top          = environment_top();
			context_t  *last_context = context;
			set_context(&declaration->context);
#endif

			method_type_t *method_type
				= allocate_type_zero(sizeof(method_type[0]));
			method_type->type.type   = TYPE_METHOD;

			parse_parameters(method_type);

#if 0
			assert(context == &declaration->context);
			set_context(last_context);
			environment_pop_to(top);
#endif

			part->method_type = method_type;

			expect(')');
			break;
		case '[':
			next_token();

			if(token.type == T_static) {
				next_token();
			}

			unsigned type_qualifiers = parse_type_qualifiers();
			if(type_qualifiers != 0) {
				if(token.type == T_static) {
					next_token();
				}
			}

			/* TODO */

			if(token.type == '*' && la(1)->type == ']') {
				next_token();
			} else if(token.type != ']') {
				parse_assignment_expression();
			}

			expect(']');
			break;
		default:
			goto declarator_finished;
		}
	}

declarator_finished:
	parse_attributes();

	return part;
}

static
type_t *construct_declarator_type(declarator_part *part, type_t *type)
{
	do {
		type = make_pointers(type, part->pointers);

		method_type_t *method_type = part->method_type;
		if(method_type != NULL) {
			method_type->result_type = type;

			type = (type_t*) method_type;
		}

		part = part->inner;
	} while(part != NULL);

	return type;
}

static
void parse_declarator(declaration_t *declaration, storage_class_t storage_class,
                      type_t *type, int may_omit_identifier)
{
	(void) may_omit_identifier;
	declarator_part *part = parse_inner_declarator(declaration);

	declaration->type          = construct_declarator_type(part, type);
	declaration->storage_class = storage_class;
	obstack_free(&temp_obst, part);
}

#if 0
static
void parse_declarator(declaration_t *declaration, storage_class_t storage_class,
                      type_t *type, int may_omit_identifier)
{
	ir_type *outer_type = parse_pointers(type);

	declaration->storage_class = storage_class;
	declaration->type          = outer_type;

	switch(token.type) {
	case T_IDENTIFIER:
		declaration->symbol          = token.v.symbol;
		declaration->source_position = token.source_position;
		next_token();
		break;
	case '(':
		next_token();
		parse_declarator(declaration, storage_class, type, 0);
		expect_void(')');
		break;
	default:
		if(may_omit_identifier)
			goto declarator_finished;
		parse_error_expected("problem while parsing declarator", T_IDENTIFIER,
		                     '(', 0);
	}

	while(1) {
		switch(token.type) {
		case '(':
			next_token();

			int         top          = environment_top();
			context_t  *last_context = context;
			set_context(&declaration->context);

			method_type_t *method_type
				= allocate_type_zero(sizeof(method_type[0]));
			method_type->type.type   = TYPE_METHOD;
			method_type->result_type = outer_type;

			parse_parameters(method_type);

			assert(context == &declaration->context);
			set_context(last_context);
			environment_pop_to(top);

			replace_type(&declaration->type, outer_type,
			             method_type);

			expect_void(')');
			break;
		case '[':
			next_token();

			if(token.type == T_static) {
				next_token();
			}

			unsigned type_qualifiers = parse_type_qualifiers();
			if(type_qualifiers != 0) {
				if(token.type == T_static) {
					next_token();
				}
			}

			if(token.type == '*' && la(1)->type == ']') {
				next_token();
			} else if(token.type != ']') {
				parse_assignment_expression();
			}

			expect_void(']');
			break;
		default:
			goto declarator_finished;
		}
	}

declarator_finished:
	parse_attributes();
}
#endif

static void record_declaration(declaration_t *declaration)
{
	if(last_declaration != NULL) {
		last_declaration->next = declaration;
	} else {
		if(context != NULL)
			context->declarations = declaration;
	}
	last_declaration = declaration;
}

static
void maybe_push_declaration(declaration_t *declaration)
{
	symbol_t *symbol = declaration->symbol;

	if(symbol != NULL) {
		environment_push(declaration, context);
	}
}

static
void parse_init_declarators(const declaration_specifiers_t *specifiers)
{
	while(1) {
		declaration_t *declaration = allocate_ast_zero(sizeof(declaration[0]));

		parse_declarator(declaration, specifiers->storage_class,
		                 specifiers->type, 0);
		maybe_push_declaration(declaration);
		record_declaration(declaration);
		if(token.type == '=') {
			next_token();
			if(token.type == '{') {
				// TODO
				expect_void('}');
			} else {
				parse_assignment_expression();
			}
		} else if(token.type == '{') {
			statement_t *statement = parse_compound_statement();
			declaration->statement = statement;
			return;
		}

		if(token.type != ',')
			break;
		next_token();
	}
	expect_void(';');
}

static
void parse_struct_declarators(const declaration_specifiers_t *specifiers)
{
	while(1) {
		if(token.type == ':') {
			next_token();
			parse_constant_expression();
			/* TODO (bitfields) */
		} else {
			declaration_t *declaration
				= allocate_ast_zero(sizeof(declaration[0]));
			parse_declarator(declaration, specifiers->storage_class,
			                 specifiers->type, 0);
			maybe_push_declaration(declaration);
			record_declaration(declaration);

			if(token.type == ':') {
				next_token();
				parse_constant_expression();
				/* TODO (bitfields) */
			}
		}

		if(token.type != ',')
			break;
		next_token();
	}
	expect_void(';');
}

static void parse_compound_type_entries(void)
{
	eat('{');

	while(token.type != '}' && token.type != T_EOF) {
		declaration_specifiers_t specifiers;
		memset(&specifiers, 0, sizeof(specifiers));
		/* TODO not correct as this allows storage class stuff... but only
		 * specifiers and qualifiers sould be allowed here */
		parse_declaration_specifiers(&specifiers);

		parse_struct_declarators(&specifiers);
	}
	if(token.type == T_EOF) {
		parse_error("unexpected error while parsing struct");
	}
	next_token();
}

void parse_declaration(void)
{
	declaration_specifiers_t specifiers;
	memset(&specifiers, 0, sizeof(specifiers));
	parse_declaration_specifiers(&specifiers);

	if(token.type == ';') {
		next_token();
		return;
	}
	parse_init_declarators(&specifiers);
}

type_t *parse_typename(void)
{
	declaration_specifiers_t specifiers;
	memset(&specifiers, 0, sizeof(specifiers));
	/* TODO not correct storage class elements are not allowed here */
	parse_declaration_specifiers(&specifiers);

	int pointers = parse_pointers();
	specifiers.type = make_pointers(specifiers.type, pointers);

	return specifiers.type;
}




typedef expression_t* (*parse_expression_function) (unsigned precedence);
typedef expression_t* (*parse_expression_infix_function) (unsigned precedence,
                                                          expression_t *left);

typedef struct expression_parser_function_t expression_parser_function_t;
struct expression_parser_function_t {
	unsigned                         precedence;
	parse_expression_function        parser;
	unsigned                         infix_precedence;
	parse_expression_infix_function  infix_parser;
};

expression_parser_function_t expression_parsers[T_LAST_TOKEN];

static
expression_t *expected_expression_error(void)
{
	parser_print_error_prefix();
	fprintf(stderr, "expected expression, got token ");
	print_token(stderr, & token);
	fprintf(stderr, "\n");

	expression_t *expression = allocate_ast_zero(sizeof(expression[0]));
	expression->type = EXPR_INVALID;
	next_token();

	return expression;
}

static
expression_t *parse_string_const(void)
{
	string_literal_t *cnst = allocate_ast_zero(sizeof(cnst[0]));

	cnst->expression.type = EXPR_STRING_LITERAL;
	cnst->value           = token.v.string;

	next_token();

	return (expression_t*) cnst;
}

static
expression_t *parse_int_const(void)
{
	const_t *cnst = allocate_ast_zero(sizeof(cnst[0]));

	cnst->expression.type = EXPR_CONST;
	cnst->value           = token.v.intvalue;

	next_token();

	return (expression_t*) cnst;
}

static
expression_t *parse_reference(void)
{
	reference_expression_t *ref = allocate_ast_zero(sizeof(ref[0]));

	ref->expression.type            = EXPR_REFERENCE;
	ref->symbol                     = token.v.symbol;

	next_token();

	return (expression_t*) ref;
}

static
expression_t *parse_brace_expression(void)
{
	eat('(');

	expression_t *result = parse_expression();
	expect(')');

	return result;
}

static
expression_t *parse_primary_expression(void)
{
	switch(token.type) {
	case T_INTEGER:
		return parse_int_const();
	case T_STRING_LITERAL:
		return parse_string_const();
	case T_IDENTIFIER:
		return parse_reference();
	case '(':
		return parse_brace_expression();
	}

	/* TODO: error message */
	return NULL;
}

static
expression_t *parse_array_expression(unsigned precedence,
                                     expression_t *array_ref)
{
	(void) precedence;

	eat('[');

	array_access_expression_t *array_access
		= allocate_ast_zero(sizeof(array_access[0]));

	array_access->expression.type = EXPR_ARRAY_ACCESS;
	array_access->array_ref       = array_ref;
	array_access->index           = parse_expression();

	if(token.type != ']') {
		parse_error_expected("Problem while parsing array access", ']', 0);
		return NULL;
	}
	next_token();

	return (expression_t*) array_access;
}

static
type_t *get_expression_type(const expression_t *expression)
{
	(void) expression;
	/* TODO */
	return NULL;
}

static
expression_t *parse_sizeof(unsigned precedence)
{
	eat(T_sizeof);

	sizeof_expression_t *sizeof_expression
		= allocate_ast_zero(sizeof(sizeof_expression[0]));
	sizeof_expression->expression.type = EXPR_SIZEOF;

	if(token.type == '(' /* && LA1 is type_specifier */) {
		next_token();
		sizeof_expression->type = parse_typename();
		expect(')');
	} else {
		expression_t *expression = parse_sub_expression(precedence);
		sizeof_expression->type  = get_expression_type(expression);
	}

	return (expression_t*) sizeof_expression;
}

static
expression_t *parse_select_expression(unsigned precedence,
                                      expression_t *compound)
{
	(void) precedence;

	assert(token.type == '.' || token.type == T_SELECT);
	next_token();

	select_expression_t *select = allocate_ast_zero(sizeof(select[0]));

	select->expression.type = EXPR_SELECT;
	select->compound        = compound;

	if(token.type != T_IDENTIFIER) {
		parse_error_expected("Problem while parsing compound select",
		                     T_IDENTIFIER, 0);
		return NULL;
	}
	select->symbol = token.v.symbol;
	next_token();

	return (expression_t*) select;
}

static
expression_t *parse_call_expression(unsigned precedence,
                                    expression_t *expression)
{
	(void) precedence;
	call_expression_t *call = allocate_ast_zero(sizeof(call[0]));

	call->expression.type            = EXPR_CALL;
	call->method                     = expression;

	/* parse arguments */
	eat('(');

	if(token.type != ')') {
		call_argument_t *last_argument = NULL;

		while(1) {
			call_argument_t *argument = allocate_ast_zero(sizeof(argument[0]));

			argument->expression = parse_expression();
			if(last_argument == NULL) {
				call->arguments = argument;
			} else {
				last_argument->next = argument;
			}
			last_argument = argument;

			if(token.type != ',')
				break;
			next_token();
		}
	}
	expect(')');

	return (expression_t*) call;
}

#define CREATE_UNARY_EXPRESSION_PARSER(token_type, unexpression_type)     \
static                                                                    \
expression_t *parse_##unexpression_type(unsigned precedence)              \
{                                                                         \
	eat(token_type);                                                      \
                                                                          \
	unary_expression_t *unary_expression                                  \
		= allocate_ast_zero(sizeof(unary_expression[0]));                 \
	unary_expression->expression.type = EXPR_UNARY;                       \
	unary_expression->type            = unexpression_type;                \
	unary_expression->value           = parse_sub_expression(precedence); \
                                                                          \
	return (expression_t*) unary_expression;                              \
}

CREATE_UNARY_EXPRESSION_PARSER('-', UNEXPR_NEGATE)
CREATE_UNARY_EXPRESSION_PARSER('+', UNEXPR_PLUS)
CREATE_UNARY_EXPRESSION_PARSER('!', UNEXPR_NOT)
CREATE_UNARY_EXPRESSION_PARSER('*', UNEXPR_DEREFERENCE)
CREATE_UNARY_EXPRESSION_PARSER('&', UNEXPR_TAKE_ADDRESS)
CREATE_UNARY_EXPRESSION_PARSER('~', UNEXPR_BITWISE_NEGATE)
CREATE_UNARY_EXPRESSION_PARSER(T_PLUSPLUS,   UNEXPR_PREFIX_INCREMENT)
CREATE_UNARY_EXPRESSION_PARSER(T_MINUSMINUS, UNEXPR_PREFIX_DECREMENT)

#define CREATE_UNARY_POSTFIX_EXPRESSION_PARSER(token_type, unexpression_type) \
static                                                                        \
expression_t *parse_##unexpression_type(unsigned precedence,                  \
                                        expression_t *left)                   \
{                                                                             \
	(void) precedence;                                                        \
	eat(token_type);                                                          \
                                                                              \
	unary_expression_t *unary_expression                                      \
		= allocate_ast_zero(sizeof(unary_expression[0]));                     \
	unary_expression->expression.type = EXPR_UNARY;                           \
	unary_expression->type            = unexpression_type;                    \
	unary_expression->value           = left;                                 \
                                                                              \
	return (expression_t*) unary_expression;                                  \
}

CREATE_UNARY_POSTFIX_EXPRESSION_PARSER(T_PLUSPLUS,   UNEXPR_POSTFIX_INCREMENT)
CREATE_UNARY_POSTFIX_EXPRESSION_PARSER(T_MINUSMINUS, UNEXPR_POSTFIX_DECREMENT)

#define CREATE_BINEXPR_PARSER(token_type, binexpression_type)    \
static                                                           \
expression_t *parse_##binexpression_type(unsigned precedence,    \
                                         expression_t *left)     \
{                                                                \
	eat(token_type);                                             \
                                                                 \
	expression_t *right = parse_sub_expression(precedence);      \
                                                                 \
	binary_expression_t *binexpr                                 \
		= allocate_ast_zero(sizeof(binexpr[0]));                 \
	binexpr->expression.type            = EXPR_BINARY;           \
	binexpr->type                       = binexpression_type;    \
	binexpr->left                       = left;                  \
	binexpr->right                      = right;                 \
                                                                 \
	return (expression_t*) binexpr;                              \
}

CREATE_BINEXPR_PARSER('*', BINEXPR_MUL)
CREATE_BINEXPR_PARSER('/', BINEXPR_DIV)
CREATE_BINEXPR_PARSER('+', BINEXPR_ADD)
CREATE_BINEXPR_PARSER('-', BINEXPR_SUB)
CREATE_BINEXPR_PARSER('<', BINEXPR_LESS)
CREATE_BINEXPR_PARSER('>', BINEXPR_GREATER)
CREATE_BINEXPR_PARSER('=', BINEXPR_ASSIGN)
CREATE_BINEXPR_PARSER(T_EQUALEQUAL, BINEXPR_EQUAL)
CREATE_BINEXPR_PARSER(T_SLASHEQUAL, BINEXPR_NOTEQUAL)
CREATE_BINEXPR_PARSER(T_LESSEQUAL, BINEXPR_LESSEQUAL)
CREATE_BINEXPR_PARSER(T_GREATEREQUAL, BINEXPR_GREATEREQUAL)
CREATE_BINEXPR_PARSER('&', BINEXPR_BITWISE_AND)
CREATE_BINEXPR_PARSER('|', BINEXPR_BITWISE_OR)
CREATE_BINEXPR_PARSER('^', BINEXPR_BITWISE_XOR)
CREATE_BINEXPR_PARSER(T_LESSLESS, BINEXPR_SHIFTLEFT)
CREATE_BINEXPR_PARSER(T_GREATERGREATER, BINEXPR_SHIFTRIGHT)

static
expression_t *parse_sub_expression(unsigned precedence)
{
	if(token.type < 0) {
		return expected_expression_error();
	}

	expression_parser_function_t *parser
		= &expression_parsers[token.type];
	source_position_t             source_position = token.source_position;
	expression_t                 *left;

	if(parser->parser != NULL) {
		left = parser->parser(parser->precedence);
	} else {
		left = parse_primary_expression();
	}
	if(left != NULL)
		left->source_position = source_position;

	while(1) {
		if(token.type < 0) {
			return expected_expression_error();
		}

		parser = &expression_parsers[token.type];
		if(parser->infix_parser == NULL)
			break;
		if(parser->infix_precedence < precedence)
			break;

		left = parser->infix_parser(parser->infix_precedence, left);
		if(left != NULL)
			left->source_position = source_position;
	}

	return left;
}

static
expression_t *parse_expression(void)
{
	return parse_sub_expression(1);
}



void register_expression_parser(parse_expression_function parser,
                                int token_type, unsigned precedence)
{
	expression_parser_function_t *entry = &expression_parsers[token_type];

	if(entry->parser != NULL) {
		fprintf(stderr, "for token ");
		print_token_type(stderr, token_type);
		fprintf(stderr, "\n");
		panic("trying to register multiple expression parsers for a token");
	}
	entry->parser     = parser;
	entry->precedence = precedence;
}

void register_expression_infix_parser(parse_expression_infix_function parser,
                                      int token_type, unsigned precedence)
{
	expression_parser_function_t *entry = &expression_parsers[token_type];

	if(entry->infix_parser != NULL) {
		fprintf(stderr, "for token ");
		print_token_type(stderr, token_type);
		fprintf(stderr, "\n");
		panic("trying to register multiple infix expression parsers for a "
		      "token");
	}
	entry->infix_parser     = parser;
	entry->infix_precedence = precedence;
}

static
void init_expression_parsers(void)
{
	memset(&expression_parsers, 0, sizeof(expression_parsers));

	register_expression_infix_parser(parse_BINEXPR_MUL,       '*', 16);
	register_expression_infix_parser(parse_BINEXPR_DIV,       '/', 16);
	register_expression_infix_parser(parse_BINEXPR_SHIFTLEFT,
	                           T_LESSLESS, 16);
	register_expression_infix_parser(parse_BINEXPR_SHIFTRIGHT,
	                           T_GREATERGREATER, 16);
	register_expression_infix_parser(parse_BINEXPR_ADD,       '+', 15);
	register_expression_infix_parser(parse_BINEXPR_SUB,       '-', 15);
	register_expression_infix_parser(parse_BINEXPR_LESS,      '<', 14);
	register_expression_infix_parser(parse_BINEXPR_GREATER,   '>', 14);
	register_expression_infix_parser(parse_BINEXPR_LESSEQUAL, T_LESSEQUAL, 14);
	register_expression_infix_parser(parse_BINEXPR_GREATEREQUAL,
	                           T_GREATEREQUAL, 14);
	register_expression_infix_parser(parse_BINEXPR_EQUAL,     T_EQUALEQUAL, 13);
	register_expression_infix_parser(parse_BINEXPR_NOTEQUAL,
	                                 T_EXCLAMATIONMARKEQUAL, 13);
	register_expression_infix_parser(parse_BINEXPR_BITWISE_AND, '&',        12);
	register_expression_infix_parser(parse_BINEXPR_BITWISE_XOR, '^',        11);
	register_expression_infix_parser(parse_BINEXPR_BITWISE_OR,  '|',        10);
	register_expression_infix_parser(parse_BINEXPR_ASSIGN,      T_EQUAL,     2);

	register_expression_infix_parser(parse_array_expression,        '[',    30);
	register_expression_infix_parser(parse_call_expression,         '(',    30);
	register_expression_infix_parser(parse_select_expression,       '.',    30);
	register_expression_infix_parser(parse_select_expression,  T_SELECT,    30);
	register_expression_infix_parser(parse_UNEXPR_POSTFIX_INCREMENT,
	                                 T_PLUSPLUS, 30);
	register_expression_infix_parser(parse_UNEXPR_POSTFIX_DECREMENT,
	                                 T_MINUSMINUS, 30);

	register_expression_parser(parse_UNEXPR_NEGATE,           '-',          25);
	register_expression_parser(parse_UNEXPR_PLUS,             '+',          25);
	register_expression_parser(parse_UNEXPR_NOT,              '!',          25);
	register_expression_parser(parse_UNEXPR_BITWISE_NEGATE,   '~',          25);
	register_expression_parser(parse_UNEXPR_DEREFERENCE,      '*',          25);
	register_expression_parser(parse_UNEXPR_TAKE_ADDRESS,     '&',          25);
	register_expression_parser(parse_UNEXPR_PREFIX_INCREMENT, T_PLUSPLUS,   25);
	register_expression_parser(parse_UNEXPR_PREFIX_DECREMENT, T_MINUSMINUS, 25);
	register_expression_parser(parse_sizeof,                  T_sizeof,     25);
}


static
statement_t *parse_case_statement(void)
{
	eat(T_case);
	parse_expression();
	expect(':');
	parse_statement();

	return NULL;
}

static
statement_t *parse_default_statement(void)
{
	eat(T_default);
	expect(':');
	parse_statement();

	return NULL;
}

static
statement_t *parse_label_statement(void)
{
	eat(T_IDENTIFIER);
	expect(':');
	parse_statement();

	return NULL;
}

static
statement_t *parse_if(void)
{
	eat(T_if);
	expect('(');
	parse_expression();
	expect(')');

	parse_statement();
	if(token.type == T_else) {
		next_token();
		parse_statement();
	}

	return NULL;
}

static
statement_t *parse_switch(void)
{
	eat(T_switch);
	expect('(');
	parse_expression();
	expect(')');
	parse_statement();

	return NULL;
}

static
statement_t *parse_while(void)
{
	eat(T_while);
	expect('(');
	parse_expression();
	expect(')');
	parse_statement();

	return NULL;
}

static
statement_t *parse_do(void)
{
	eat(T_do);
	parse_statement();
	expect(T_while);
	expect('(');
	parse_expression();
	expect(')');

	return NULL;
}

static
statement_t *parse_for(void)
{
	eat(T_for);
	expect('(');
	if(token.type != ';') {
		/* TODO not correct... this could also be a declaration */
		parse_expression();
	}
	expect(';');
	if(token.type != ';') {
		parse_expression();
	}
	expect(';');
	if(token.type != ')') {
		parse_expression();
	}
	expect(')');
	parse_statement();

	return NULL;
}

static
statement_t *parse_goto(void)
{
	eat(T_goto);
	expect(T_IDENTIFIER);
	expect(';');

	return NULL;
}

static
statement_t *parse_continue(void)
{
	eat(T_continue);
	expect(';');

	return NULL;
}

static
statement_t *parse_break(void)
{
	eat(T_break);
	expect(';');

	return NULL;
}

static
statement_t *parse_return(void)
{
	eat(T_return);
	parse_expression();
	expect(';');

	return NULL;
}

static
statement_t *parse_declaration_statement(void)
{
	parse_declaration();
	return NULL;
}

static
statement_t *parse_expression_statement(void)
{
	parse_expression();
	return NULL;
}

static
statement_t *parse_statement(void)
{
	declaration_t *declaration;
	statement_t   *statement = NULL;

	/* declaration or statement */
	switch(token.type) {
	case T_case:
		statement = parse_case_statement();
		break;

	case T_default:
		statement = parse_default_statement();
		break;

	case '{':
		statement = parse_compound_statement();
		break;

	case T_if:
		statement = parse_if();
		break;

	case T_switch:
		statement = parse_switch();
		break;

	case T_while:
		statement = parse_while();
		break;

	case T_do:
		statement = parse_do();
		break;

	case T_for:
		statement = parse_for();
		break;

	case T_goto:
		statement = parse_goto();
		break;

	case T_continue:
		statement = parse_continue();
		break;

	case T_break:
		statement = parse_break();
		break;

	case T_return:
		statement = parse_return();
		break;

	case ';':
		statement = NULL;
		break;

	case T_IDENTIFIER:
		if(la(1)->type == ':') {
			statement = parse_label_statement();
			break;
		}

		declaration = token.v.symbol->declaration;
		if(declaration != NULL &&
				declaration->storage_class == STORAGE_CLASS_TYPEDEF) {
			statement = parse_declaration_statement();
			break;
		}

		statement = parse_expression_statement();
		break;

	DECLARATION_START
		statement = parse_declaration_statement();
		break;
	}

	return statement;
}

static
statement_t *parse_compound_statement(void)
{
	eat('{');

	compound_statement_t *compound_statement
		= allocate_ast_zero(sizeof(compound_statement[0]));
	compound_statement->statement.type = STATEMENT_COMPOUND;

	int        top          = environment_top();
	context_t *last_context = context;
	set_context(&compound_statement->context);

	while(token.type != '}') {
		parse_statement();
	}

	assert(context == &compound_statement->context);
	set_context(last_context);
	environment_pop_to(top);

	next_token();

	return (statement_t*) compound_statement;
}

static
translation_unit_t *parse_translation_unit(void)
{
	translation_unit_t *unit = allocate_ast_zero(sizeof(unit[0]));

	assert(context == NULL);
	set_context(&unit->context);

	while(token.type != T_EOF) {
		parse_declaration();
	}

	assert(context == &unit->context);
	context          = NULL;
	last_declaration = NULL;

	return unit;
}

translation_unit_t *parse(void)
{
	obstack_init(&environment_obstack);
	environment_stack = NEW_ARR_F(environment_entry_t*, 0);

	lookahead_bufpos = 0;
	for(int i = 0; i < MAX_LOOKAHEAD + 2; ++i) {
		next_token();
	}
	translation_unit_t *unit = parse_translation_unit();

	DEL_ARR_F(environment_stack);
	obstack_free(&environment_obstack, NULL);

	return unit;
}

void init_parser(void)
{
	init_expression_parsers();
	obstack_init(&temp_obst);
}

void exit_parser(void)
{
	obstack_free(&temp_obst, NULL);
}
