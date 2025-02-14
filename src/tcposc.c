/* tcposc -- A compiler for TinyCoPoOS   Copyright (C) 2024 Frans Faase

   This is pased on https://github.com/FransFaase/IParse
   
*/


#define SAFE_CASTING

/* 
	First some standard includes and definitions.
*/

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdarg.h>

#ifndef NULL
#define NULL 0
#endif

typedef int bool;
#define TRUE 1
#define FALSE 0

typedef unsigned char byte;

#if TRACE_ALLOCATIONS

void *my_malloc(size_t size, unsigned int line)
{
	void *p = malloc(size);
	fprintf(stdout, "At line %u: allocated %lu bytes %p\n", line, size, p);
	return p;
}

void my_free(void *p, unsigned int line)
{

	fprintf(stdout, "At line %u: freed %p\n", line, p);
	free(p);
}

#else

#define my_malloc(X,L) malloc(X)
#define my_free(X,L) free(X)

#endif

#define MALLOC(T) (T*)my_malloc(sizeof(T), __LINE__)
#define MALLOC_N(N,T)  (T*)my_malloc((N)*sizeof(T), __LINE__)
#define STR_MALLOC(N) (char*)my_malloc((N)+1, __LINE__)
#define STRCPY(D,S) D = (char*)my_malloc(strlen(S)+1, __LINE__); strcpy(D,S)
#define FREE(X) my_free(X, __LINE__)


/*
	Internal representation parsing rules
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	The following section of the code deals with the internal representation of
	the parsing rules as they are used by the parsing routines.
	
	The grammar is an extended BNF grammar, which supports optional elements,
	sequences of elements (with an optional chain rule) and grouping within the
	grammar rules. Because the scanner is intergrated with the parser, the
	terminals are defined with characters and character sets. The grammar does
	support direct left recursion. (The parsing algorithm cannot deal with
	indirect left recursion.) For a non-terminal these left recursive grammar
	rules are stored separately without mentioning the recursive non-terminal
	in the rule.
	
	The grammar consists thus of a list of non-terminals, where each
	non-terminal has two list of rules (one for non-left recursive rules
	and one for left recursive rules). Each rule defines a rule, which
	consists of list of grammar elements. An element can be one of:
	- character,
	- character set,
	- end of text,
	- non-terminal, or
	- grouping of rules.
	An element can have modifiers for making the element optional or a sequence.
	It is also possible to specify that an optional and/or sequential element
	should be avoided in favour of the remaining rule.
	With a sequential element it is possible to define a chain rule, which is
	to appear between the elements. An example of this is a comma separated
	list of elements, where the comma (and possible white space) is the chain
	rule.
	Each element has a number of function pointers, which can be used to specify
	functions that should be called to process the parsing results. Furthermore,
	each rule has a function pointer, to specify the function that should
	be called at the end of the rule to process the result.
	
	An example for a white space grammar will follow.
*/

/*  Forward declarations of types of the grammar definition.  */

typedef struct non_terminal non_terminal_t, *non_terminal_p;
typedef struct rule *rule_p;
typedef struct element *element_p;
typedef struct char_set *char_set_p;
typedef struct result result_t, *result_p;
typedef struct text_pos text_pos_t, *text_pos_p;

/*  Definition for a non-terminal  */

struct non_terminal
{
	const char *name;     /* Name of the non-terminal */
	rule_p normal;       /* Normal rules */
	rule_p recursive;    /* Left-recursive rules */
};

typedef struct non_terminal_dict *non_terminal_dict_p;
struct non_terminal_dict
{
	non_terminal_t elem;
	non_terminal_dict_p next;
};

/*  - Function to find a non-terminal on a name or add a new to end of list */

non_terminal_p find_nt(const char *name, non_terminal_dict_p *p_nt)
{
   while (*p_nt != NULL && (*p_nt)->elem.name != name && strcmp((*p_nt)->elem.name, name) != 0)
		p_nt = &((*p_nt)->next);

   if (*p_nt == NULL)
   {   *p_nt = MALLOC(struct non_terminal_dict);
	   (*p_nt)->elem.name = name;
	   (*p_nt)->elem.normal = NULL;
	   (*p_nt)->elem.recursive = NULL;
	   (*p_nt)->next = NULL;
   }
   return &(*p_nt)->elem;
}

/*  Definition of an rule  */

typedef bool (*end_function_p)(const result_p rule_result, void* data, result_p result);

struct rule
{
	element_p elements;            /* The rule definition */

	/* Function pointer to an optional function that is to be called when rule
	   is parsed. Input arguments are the result of the rule and a pointer to
	   some additional data. The output is the result to be returned by the
	   rule. When the function pointer is null, the result of the rule is
	   taken as the result of the rule. */
	end_function_p end_function;
	void *end_function_data;      /* Pointer to additional data which is passed to end_function */

	/* (Only for left-recursive rules.) Function pointer to an optional
	   Boolean function that is called at the start of the rule to add the
	   already parsed left-recursive rule to the result to be passed to the
	   remained of the rule (of this rule). of this rule. When the
	   function returns false, parsing fails. When the function pointer is null,
	   it is equivalent with a function that always returns true and does not
	   set the result, thus discarding the already parsed left-recursive rule.
	*/
	bool (*rec_start_function)(result_p rec_result, result_p result);

	rule_p next;           /* Next rule */
};

/*  - Function to create a new rule */

rule_p new_rule()
{
	rule_p rule = MALLOC(struct rule);
	rule->elements = NULL;
	rule->end_function = NULL;
	rule->end_function_data = NULL;
	rule->rec_start_function = NULL;
	rule->next = NULL;
	return rule;
}

/*  
	Defintion of an element of a rule.
*/

enum element_kind_t
{
	rk_nt,       /* A non-terminal */
	rk_grouping, /* Grouping of one or more rules */
	rk_char,     /* A character */
	rk_charset,  /* A character set */
	rk_end,      /* End of input */
	rk_term      /* User defined terminal scan function */
};

struct element
{
	enum element_kind_t kind;   /* Kind of element */
	bool optional;              /* Whether the element is optional */
	bool sequence;              /* Whether the element is a sequenct */
	bool back_tracking;         /* Whether a sequence is back-tracking */
	bool avoid;                 /* Whether the elmeent should be avoided when it is optional and/or sequential. */
	element_p chain_rule;       /* Chain rule, for between the sequential elements */
	union 
	{   non_terminal_p non_terminal; /* rk_nt: Pointer to non-terminal */
		rule_p rules;                /* rk_grouping: Pointer to the rules */
		char ch;                     /* rk_char: The character */
		char_set_p char_set;         /* rk_charset: Pointer to character set definition */
		const char *(*terminal_function)(const char *input, result_p result);
		                             /* rk_term: Pointer to user defined terminal scan function */
	} info;

	/* Function pointer to an optional Boolean function that is called after the
	   character is parsed, to combine the result of the previous elements with
	   the character into the result passed to the remainder of the rule. (When
	   the element is a sequence, the previous result is the result of the
	   previous characters in the sequence. For more details see the description
	   of the function pointers begin_seq_function and add_seq_function.) When
	   the function returns false, parsing fails. When the function pointer is
	   null, it is equivalent with a function that always returns true and simple
	   sets the result as the result of the previous element, thus discarding the
	   result of the element. This is, for example used, when the element is a
	   literal character. */
	bool (*add_char_function)(result_p prev, char ch, result_p result);

	/* Function pointer to an optional Boolean function that is called after the
	   element is parsed. When the function returns false, parsing fails. The
	   function is called with the result of the element and a pointer to an
	   additional argument. When the function pointer is null, it is equivalent
	   with a function that always returns true.
	   A typical usage of this function is to check if a parsed identified is
	   a certain keyword or not a keyword at all. */
	bool (*condition)(result_p result, const void *argument);
	const void *condition_argument;

	/* Function pointer to an optional Boolean function that is called after the
	   element is parsed and after the optional condition function has been
	   called, to combine the result of the previous elements with the element
	   into the result to be passed to the remainder of the rule. (When the
	   element is a sequence, the previous result is the result of the previous
	   elements in the sequence. For more details see the description of the
	   function pointers begin_seq_function and add_seq_function.) When the
	   function returns false, parsing fails. When the function pointer is null,
	   it is equivalent with a function that always returns true and simple sets
	   the result as the result of the previous element, thus discarding the
	   result of the element. */
	bool (*add_function)(result_p prev, result_p elem, result_p result);

	/* Function pointer to an optional Boolean function that is called when an
	   optional element is skipped, to apply this to the result of the
	   previous elements into the result to be passed to the remainder of the
	   rule. When the function returns false, parsing fails. When the function
	   pointer is null, the add_function with an empty result acts as a
	   fallback. */
	bool (*add_skip_function)(result_p prev, result_p result);

	/* Function pointer to an optional void function that is called at the
	   start of parsing an element that is a sequence which is given the result
	   of the previous elements and which result is passed to the first a
	   call of to function processing the elements of the sequence, for
	   example add_char_function or add_function. When the function pointer
	   is null, an initial result is passed to the first element of the
	   sequence. */
	void (*begin_seq_function)(result_p prev, result_p seq);

    /* Function pointer to an optional Boolean function that is called after
       the complete sequence of elements has been parsed, to combine it with
       the result of the previous elements into the result to be passed to
       the remainder of the rule. When the function returns false, parsing
       fails. When the function pointer is null, it is equivalent with a
       function that always returns true and simple sets the result as the
       result of the previous element, thus discarding the result of the
       element.*/
	bool (*add_seq_function)(result_p prev, result_p seq, void *data, result_p result);
	void *add_seq_function_data;

	/* Function pointer to an optional void function that is called with
	   the position (line, column numbers) at the start of parsing the
	   element with the result that is passed to the remainder of the
	   rule, thus after the previous functions have been called. */
	void (*set_pos)(result_p result, text_pos_p ps);

	const char *expect_msg;     /* For error reporting */
	
	element_p next;             /* Next element in the rule */
};

/*
	- Function to create new element
*/

void element_init(element_p element, enum element_kind_t kind)
{
	element->kind = kind;
	element->next = NULL;
	element->optional = FALSE;
	element->sequence = FALSE;
	element->back_tracking = FALSE;
	element->avoid = FALSE;
	element->chain_rule = NULL;
	element->add_char_function = 0;
	element->condition = 0;
	element->condition_argument = NULL;
	element->add_function = 0;
	element->add_skip_function = 0;
	element->begin_seq_function = 0;
	element->add_seq_function = 0;
	element->add_seq_function_data = NULL;
	element->set_pos = 0;
}
	
element_p new_element(enum element_kind_t kind)
{
	element_p element = MALLOC(struct element);
	element_init(element, kind);
	return element;
}

/*  Definition of a character set (as a bit vector)  */

struct char_set
{
	byte bitvec[32];
};

/*
	- Function to create new character set
*/

char_set_p new_char_set()
{
	char_set_p char_set = MALLOC(struct char_set);
	for (int i = 0; i < 32; i++)
		char_set->bitvec[i] = 0;
	return char_set;
}

/*
	- Functions belonging to character sets
*/

bool char_set_contains(char_set_p char_set, const char ch) { return (char_set->bitvec[((byte)ch) >> 3] & (1 << (((byte)ch) & 0x7))) != 0; }
void char_set_add_char(char_set_p char_set, char ch) { char_set->bitvec[((byte)ch) >> 3] |= 1 << (((byte)ch) & 0x7); }
void char_set_remove_char(char_set_p char_set, char ch) { char_set->bitvec[((byte)ch) >> 3] &= ~(1 << (((byte)ch) & 0x7)); }
void char_set_add_range(char_set_p char_set, char first, char last)
{
	byte ch = (byte)first;
	for (; ((byte)first) <= ch && ch <= ((byte)last); ch++)
		char_set_add_char(char_set, ch);
}


/*
	- Functions for printing representation parsing rules
*/

void element_print(FILE *f, element_p element);

void rules_print(FILE *f, rule_p rule)
{
	bool first = TRUE;

	for (; rule; rule = rule->next)
	{   
		if (!first)
			fprintf(f, "|");
		first = FALSE;
		element_print(f, rule->elements);
	}
}

void print_c_string_char(FILE *f, char ch)
{
	switch (ch)
	{
		case '\0': fprintf(f, "\\0"); break;
		case '\a': fprintf(f, "\\a"); break;
		case '\b': fprintf(f, "\\b"); break;
		case '\n': fprintf(f, "\\n"); break;
		case '\r': fprintf(f, "\\r"); break;
		case '\t': fprintf(f, "\\t"); break;
		case '\v': fprintf(f, "\\v"); break;
		case '\\': fprintf(f, "\\\\"); break;
		case '-':  fprintf(f, "\\-"); break;
		case ']':  fprintf(f, "\\]"); break;
		default:
			if (ch < ' ')
				fprintf(f, "\\%03o", ch);
			else
				fprintf(f, "%c", ch);
	}
}

void element_print(FILE *f, element_p element)
{   
	if (element == NULL)
		return;

	switch(element->kind)
	{
		case rk_nt:
			fprintf(f, "%s ", element->info.non_terminal->name);
			break;
		case rk_grouping:
			fprintf(f, "(");
			rules_print(f, element->info.rules);
			fprintf(f, ")");
			break;
		case rk_char:
			fprintf(f, "'%c' ", element->info.ch);
			break;
		case rk_charset:
			fprintf(f, "[");
			unsigned char from = 255;
			for (unsigned char ch = 0; ; ch++)
			{
				if (char_set_contains(element->info.char_set, ch))
				{
					if (from == 255)
					{
						from = ch;
						print_c_string_char(f, ch);
					}
				}
				else if (from < 255)
				{
					if (ch > from+1)
					{
						if (ch > from+2)
							fprintf(f, "-");
						print_c_string_char(f, ch-1);
					}
					from = 255;
				}
				if (ch == 255)
					break;
			}
			if (from < 255)
				fprintf(f, "-\\377");
			fprintf(f, "] ");
			break;
		case rk_end:
			fprintf(f, "<eof> ");
			break;
		case rk_term:
			fprintf(f, "<term> ");
			break;
	}

	if (element->sequence)
	{
		if (element->chain_rule == NULL)
			fprintf(f, "SEQ ");
		else
		{
			fprintf(f, "CHAIN (");
			element_print(f, element->chain_rule);
			fprintf(f, ")");
		}
		if (element->back_tracking)
			fprintf(f, "BACK_TRACKING ");
	}
	if (element->optional)
		fprintf(f, "OPT ");
	if (element->avoid)
		fprintf(f, "AVOID ");
	element_print(f, element->next);
}

/*  Some macro definitions for defining a grammar more easily.  */

#define HEADER(N) non_terminal_dict_p *_nt = N; non_terminal_p nt; rule_p* ref_rule; rule_p* ref_rec_rule; rule_p rules; element_p* ref_element; element_p element;
#define NT_DEF(N) nt = find_nt(N, _nt); ref_rule = &nt->normal; ref_rec_rule = &nt->recursive;
#define RULE rules = *ref_rule = new_rule(); ref_rule = &rules->next; ref_element = &rules->elements;
#define REC_RULE(E) rules = *ref_rec_rule = new_rule(); rules->rec_start_function = E; ref_rec_rule = &rules->next; ref_element = &rules->elements;
#define _NEW_GR(K) element = *ref_element = new_element(K); ref_element = &element->next;
#define NTF(N,F) _NEW_GR(rk_nt) element->info.non_terminal = find_nt(N, _nt); element->add_function = F;
#define END _NEW_GR(rk_end)
#define SEQ(S,E,D) element->sequence = TRUE; element->begin_seq_function = S; element->add_seq_function = E; element->add_seq_function_data = D;
#define CHAIN element_p* ref_element = &element->chain_rule; element_p element;
#define OPT(F) element->optional = TRUE; element->add_skip_function = F;
#define BACK_TRACKING element->back_tracking = TRUE;
#define AVOID element->avoid = TRUE;
#define SET_PS(F) element->set_pos = F;
#define CHAR(C) _NEW_GR(rk_char) element->info.ch = C;
#define CHARF(C,F) CHAR(C) element->add_char_function = F;
#define CHARSET(F) _NEW_GR(rk_charset) element->info.char_set = new_char_set(); element->add_char_function = F;
#define ADD_CHAR(C) char_set_add_char(element->info.char_set, C);
#define REMOVE_CHAR(C) char_set_remove_char(element->info.char_set, C);
#define ADD_RANGE(F,T) char_set_add_range(element->info.char_set, F, T);
#define END_FUNCTION(F) rules->end_function = F;
#define GROUPING _NEW_GR(rk_grouping) element->info.rules = new_rule(); rule_p* ref_rule = &element->info.rules; rule_p rules; element_p* ref_element; element_p element;
		


/*
	Example of defining white space grammar with comments
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	In this example, white space does not have a result, thus all function
	pointers can be left 0. White space is defined as a (possible empty)
	sequence of white space characters, the single line comment and the
	traditional C-comment. '{ GROUPING' and '}' are used to define a
	grouping. The grouping contains three rules.
*/

void white_space_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("white_space")
		RULE
			{ GROUPING
				RULE /* for the usual white space characters */
					CHARSET(0) ADD_CHAR(' ') ADD_CHAR('\t') ADD_CHAR('\n') ADD_CHAR('\r')
				RULE /* for the single line comment starting with two slashes */
					CHAR('/')
					CHAR('/')
					CHARSET(0) ADD_RANGE(' ', 255) ADD_CHAR('\t') SEQ(0, 0, NULL) OPT(0)
					CHAR('\r') OPT(0)
					CHAR('\n')
				RULE /* for the traditional C-comment (using avoid modifier) */
					CHAR('/')
					CHAR('*')
					CHARSET(0) ADD_RANGE(' ', 255) ADD_CHAR('\t') ADD_CHAR('\n') ADD_CHAR('\r') SEQ(0, 0, NULL) OPT(0) AVOID
					CHAR('*')
					CHAR('/')
			} SEQ(0, 0, NULL) OPT(0)
}


/*
	Example of defining a positive whole number grammar
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	In this example, a grammar is given for a positive whole number, and
	explained how a result can be returned. A whole number is represented
	by a sequence of characters in the range '0' to '9'. There are two
	functions needed: One that takes a character and calculates the
	resulting number when that character is added at the back of a number.
	One that transfers the result of the sequence to result of the rule.
	The first function needs to be passed as an argument to the CHARSET
	define. The second function needs to be passed as the second argument
	to the SEQ define.
	If no function is set for processing the result of a rule, then the
	result of the last element is returned.
*/

bool number_add_char(result_p prev, char ch, result_p result);
bool use_sequence_result(result_p prev, result_p seq, void *data, result_p result);

void number_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("number")
		RULE
			CHARSET(number_add_char) ADD_RANGE('0', '9') SEQ(0, use_sequence_result, NULL)
}

/*
	To implement the two functions, some definitions are needed, which
	will be explained below.
	
	
	Output stream
	~~~~~~~~~~~~~
	
	We first define an interface for an output stream, which later
	can be implemented as either outputting to a file or a string buffer.
*/

typedef struct ostream ostream_t, *ostream_p;
struct ostream
{
	void (*put)(ostream_p ostream, char ch);
};

void ostream_put(ostream_p ostream, char ch)
{
	ostream->put(ostream, ch);
}

void ostream_puts(ostream_p ostream, const char *s)
{
	while (*s != '\0')
		ostream_put(ostream, *s++);
}

/*
	Result
	~~~~~~

	Because the parser algorithm is agnostic to the types of results that
	are used by grammar rule, a void pointer is used. Reference counting
	is often used to manage dynamically allocated memory. It is a good
	idea to group the void pointer with functions to increment and decrement
	the reference count. The struct 'result' below also adds a function
	pointer to a print function.
*/

#define CHECK_LOCAL_RESULT

struct result
{	
	void *data;
	void (*inc)(void *data);
	void (*dec)(void *data);
	void (*print)(void *data, ostream_p ostream);
#ifdef CHECK_LOCAL_RESULT
	int line;
	const char *name;
	result_p context;
#endif
};

/*
	- Function to initialize a result
*/

#ifdef CHECK_LOCAL_RESULT
#define CHECK_LOCAL_PARAM(P) , P
#define RESULT_INIT(V) result_init(V, 0, __LINE__, "*")
#define RESULT_RELEASE(V) result_release(V, 0, __LINE__)
#else
#define CHECK_LOCAL_PARAM(P)
#define RESULT_INIT(V) result_init(V)
#define RESULT_RELEASE(V) result_release(V)
#endif

void result_init(result_p result CHECK_LOCAL_PARAM(result_p *context) CHECK_LOCAL_PARAM(int line) CHECK_LOCAL_PARAM(const char *name))
{
	result->data = NULL;
	result->inc = 0;
	result->dec = 0;
	result->print = 0;
#ifdef CHECK_LOCAL_RESULT
	result->line = line;
	result->name = name;
	if (context != NULL)
	{
		result->context = *context;
		*context = result;
	}
	else
		result->context = 0;
#endif
}

/*
	- Function to assign result to another result
*/

void result_assign(result_p trg, result_p src)
{
	void (*old_trg_dec)(void *data) = trg->dec;
	void *old_trg_data = trg->data;
	if (src->inc != 0 && src->data != 0)
		src->inc(src->data);
	trg->data = src->data;
	trg->inc = src->inc;
	trg->dec = src->dec;
	trg->print = src->print;
	if (old_trg_dec != 0)
		old_trg_dec(old_trg_data);
}

/*
	- Function to transfer the result to another result.
	  (The source will be initialized.)
*/

void result_transfer(result_p trg, result_p src)
{
	void (*old_trg_dec)(void *data) = trg->dec;
	void *old_trg_data = trg->data;
	trg->data = src->data;
	trg->inc = src->inc;
	trg->dec = src->dec;
	trg->print = src->print;
	RESULT_INIT(src);
	if (old_trg_dec != 0)
		old_trg_dec(old_trg_data);
}

/*
	- Function to release the result
*/

void result_release(result_p result CHECK_LOCAL_PARAM(result_p *context) CHECK_LOCAL_PARAM(int line))
{
#ifdef CHECK_LOCAL_RESULT
	if (context != NULL)
	{
		if (*context == NULL)
		{
			printf("Context already empty on line %d. Found from line %d.\n", line, result->line);
			exit(1);
		}
		if (*context != result)
		{
			printf("Wrong context on line %d. Found from line %d. Expect from line %s  (on %d)\n",
				line, result->line, (*context)->name, (*context)->line);
			exit(1);
		}
		*context = result->context;
	}
#endif
	if (result->dec != 0 && result->data != 0)
		result->dec(result->data);
	result->data = NULL;
	result->inc = 0;
	result->dec = 0;
	result->data = NULL;
	result->print = 0;
}

/*
	- Function to print the result
*/

void result_print(result_p result, ostream_p ostream)
{
	if (result->print == 0 || result->data == NULL)
		ostream_puts(ostream, "<>");
	else
		result->print(result->data, ostream);
}


/*
	- Two macro definitions which should be used a the start and end of
	  the scope of a result variable
*/

#ifdef CHECK_LOCAL_RESULT
#define ENTER_RESULT_CONTEXT result_p result_p_context = 0;
#define EXIT_RESULT_CONTEXT if (result_p_context != 0) { printf("On line %d context not closed for %s (on %d)\n", __LINE__, result_p_context->name, result_p_context->line); exit(1); }
#define DECL_RESULT(V) result_t V; result_init(&V, &result_p_context, __LINE__, #V);
#define DISP_RESULT(V) result_release(&V, &result_p_context, __LINE__);
#else
#define ENTER_RESULT_CONTEXT
#define INIT_RESULT(V) result_init(&V)
#define EXIT_RESULT_CONTEXT
#define DECL_RESULT(V) result_t V; result_init(&V);
#define DISP_RESULT(V) result_release(&V);
#endif

/*
	- Function for using result of a sequence
	  (to be used as value for the add_seq_function function pointer)
*/

bool use_sequence_result(result_p prev, result_p seq, void *data, result_p result)
{
	result_assign(result, seq);
	return TRUE;
}

/*
	Base for reference counting results
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	Because there is usually more than one type that needs to implement
	reference counting, it is a good idea to define a base struct for it.
	When this is used as the type of the first member in a struct
	for a result, then the reference counting functions can be called
	on these.
*/

bool debug_allocations = FALSE;

#ifdef SAFE_CASTING
typedef struct ref_counted_base_type ref_counted_base_type_t, *ref_counted_base_type_p;
struct ref_counted_base_type
{
	const char* name;
	ref_counted_base_type_p super;
};
#endif

typedef struct
{
	unsigned long cnt;     /* A reference count */

	/* Function pointer to an optional void function that is called
	   right before the data is freed. This is only needed when the
	   data contains pointers to other pieces of data for which
	   reference counts need to be decremented.
	*/
#ifdef SAFE_CASTING
	ref_counted_base_type_p base_type;
#endif
	void (*release)(void *);
} ref_counted_base_t, *ref_counted_base_p;

void ref_counted_base_inc(void *data) { ((ref_counted_base_p)data)->cnt++; }
void ref_counted_base_dec(void *data)
{
	if (--((ref_counted_base_p)data)->cnt == 0)
	{
		if (debug_allocations) fprintf(stdout, "Free %p\n", data);
		if (((ref_counted_base_p)data)->release != 0)
			((ref_counted_base_p)data)->release(data);
		else
			FREE(data);
	}
}

#ifdef SAFE_CASTING
#define DEFINE_BASE_TYPE(T) ref_counted_base_type_t T##_base_type = { #T, NULL };
#define DEFINE_SUB_BASE_TYPE(T,S) ref_counted_base_type_t T##_base_type = { #T, &S##_base_type };
#define SET_TYPE(T, X) ((ref_counted_base_p)X)->base_type = &T##_base_type;
#define CAST(T,X) ((T)check_type(&T##_base_type,X,__LINE__))

void *check_type(ref_counted_base_type_p base_type, void *value, int line)
{
	if (value == 0)
		return NULL;
	for (ref_counted_base_type_p value_base_type = ((ref_counted_base_p)value)->base_type; value_base_type != 0; value_base_type = value_base_type->super)
		if (value_base_type == base_type)
			return value;
	printf("line %d Error: casting %s to %s\n", line, ((ref_counted_base_p)value)->base_type->name, base_type->name); fflush(stdout);
	exit(1);
	return NULL;
}
#else
#define DEFINE_BASE_TYPE(T,S)
#define DEFINE_SUB_BASE_TYPE(T,S)
#define SET_TYPE(T, X)
#define CAST(T,X) ((T)(X))
#endif

void result_assign_ref_counted(result_p result, ref_counted_base_p ref_counted_base, void (*print)(void *data, ostream_p ostream))
{
	if (debug_allocations) fprintf(stdout, "Allocated %p\n", ref_counted_base);
	ref_counted_base->cnt = 1;
	result->data = ref_counted_base;
	result->inc = ref_counted_base_inc;
	result->dec = ref_counted_base_dec;
	result->print = print;
}

/*
	Number result
	~~~~~~~~~~~~~
	
	The struct for representing the number has but a single member
	(besides the member for the reference counting base).
*/

typedef struct number_data
{
	ref_counted_base_t _base;
	long num;
} *number_data_p;

#define NUMBER_DATA_NUM(R) (CAST(number_data_p,(R)->data)->num)

DEFINE_BASE_TYPE(number_data_p)

void number_print(void *data, ostream_p ostream)
{
	char buffer[41];
	snprintf(buffer, 40, "number %ld", CAST(number_data_p, data)->num);
	ostream_puts(ostream, buffer);
}

void new_number_data(result_p result)
{
	number_data_p number_data = MALLOC(struct number_data);
	number_data->_base.release = 0;
	result_assign_ref_counted(result, &number_data->_base, number_print);
	SET_TYPE(number_data_p, number_data);
}


/*
	There are actually two ways to implement the function for adding
	a character to a number. The first time the function is called
	the data pointer of the previous result, will be 0. The first
	solution, allocates new memory each time the function is called.
	The reference counting will take care that after the whole
	rule has been parsed all intermediate results will be freed again.
	The second solution, only allocates memory once. In this case this
	is possible, because the parser will not back-track during parsing
	the number. Both solutions are given in the function below.
*/

bool number_add_char(result_p prev, char ch, result_p result)
{
#if 0 /* Allocating a result for each intermediate result */
	new_number_data(result);
	long num = prev->data != NULL ? NUMBER_DATA_NUM(prev) : 0;
	NUMBER_DATA_NUM(result) = 10 * num + ch - '0';
#else /* Allocating the result but once */
	if (prev->data == NULL)
	{
		new_number_data(result);
		NUMBER_DATA_NUM(result) = ch - '0';
	}
	else
	{
		result_assign(result, prev);
		NUMBER_DATA_NUM(result) = 10 * NUMBER_DATA_NUM(result) + ch - '0';
	}
#endif
	return TRUE;
}

/*
	Implementing a back-tracking parser on a text buffer
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	The most 'simple' parsing algorithm, is a back-tracking recursive decent
	parser on a text buffer that is stored in memory. Nowadays memory is
	cheap and storing whole files as strings in memory is usually no problem
	at all. (The hardest part about this parser is the reference counting of
	the results.)
	
	This parser will simply take the grammar specification and try to parse
	the text in the buffer. If it fails at some point, it will simply
	back-track to where it started the current rule and try the next
	rule. It continues doing so until it parses the whole contents or
	fails after having tried all (nested) rules.
	
	Below, we first define a text position and a text buffer that will be
	used by the back-tracking parser.
	
*/

struct text_pos
{
	size_t pos        ;       /* Positive offset from the start of the file */
	unsigned int cur_line;    /* Line number (1-based) with the position */
	unsigned int cur_column;  /* Column number (1-based) with the position */
};

typedef struct
{
	const char *buffer;     /* String containting the input text */
	size_t buffer_len;      /* Length of the input text */
	text_pos_t pos;         /* Current position in the input text */
	const char *info;       /* Contents starting at the current position */
	unsigned int tab_size;  /* Tabs are on multiples of the tab_size */
} text_buffer_t, *text_buffer_p;

void text_buffer_assign_string(text_buffer_p text_buffer, const char* text)
{
	text_buffer->tab_size = 4;
	text_buffer->buffer_len = strlen(text);
	text_buffer->buffer = text;
	text_buffer->info = text_buffer->buffer;
	text_buffer->pos.pos = 0;
	text_buffer->pos.cur_line = 1;
	text_buffer->pos.cur_column = 1;
}

void text_buffer_from_file(text_buffer_p text_buffer, FILE *f)
{
	fseek(f, 0L, SEEK_END);
	size_t length = ftell(f);
	char *buffer = MALLOC_N(length, char);
	fseek(f, 0L, SEEK_SET);
	length = fread(buffer, 1, length, f);
	
	text_buffer->tab_size = 4;
	text_buffer->buffer_len = length;
	text_buffer->buffer = buffer;
	text_buffer->info = text_buffer->buffer;
	text_buffer->pos.pos = 0;
	text_buffer->pos.cur_line = 1;
	text_buffer->pos.cur_column = 1;
}

void text_buffer_next(text_buffer_p text_buffer)
{
	if (text_buffer->pos.pos < text_buffer->buffer_len)
	{
	  switch(*text_buffer->info)
	  {   case '\t':
			  text_buffer->pos.cur_column += text_buffer->tab_size - (text_buffer->pos.cur_column - 1) % text_buffer->tab_size;
			  break;
		  case '\n':
			  text_buffer->pos.cur_line++;
			  text_buffer->pos.cur_column = 1;
			  break;
		  default:
			  text_buffer->pos.cur_column++;
			  break;
	  }
	  text_buffer->pos.pos++;
	  text_buffer->info++;
	}
}

bool text_buffer_end(text_buffer_p text_buffer) {
 	return text_buffer->pos.pos >= text_buffer->buffer_len;
}

void text_buffer_set_pos(text_buffer_p text_file, text_pos_p text_pos)
{
	if (text_file->pos.pos == text_pos->pos)
		return;
	text_file->pos = *text_pos;
	text_file->info = text_file->buffer + text_pos->pos;
}

/*
	Caching intermediate parse states
	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	One way to improve the performance of back-tracking recursive-descent
	parser is to use a cache were intermediate results are stored.
	Because there are various caching strategies an abstract interface
	for caching is provided. The parser struct, to be defined below, has
	a pointer to a function that, if not NULL, is called to query the
	cache and return a cache item. As long as the success status is
	unknown, the cache item may not be freed from memory (during a
	successive call to the function).
*/

enum success_t { s_unknown, s_fail, s_success } ;

typedef struct
{
	enum success_t success;  /* Could said non-terminal be parsed from position */
	result_t result;         /* If so, what result did it produce */
	text_pos_t next_pos;     /* and from which position (with line and column numbers) should parsing continue */
} cache_item_t, *cache_item_p;

/*
	For debugging the parser
	~~~~~~~~~~~~~~~~~~~~~~~~
*/

int depth = 0;
bool debug_parse = FALSE;
bool debug_nt = FALSE;
ostream_p stdout_stream;

#define DEBUG_ENTER(X) if (debug_parse) { DEBUG_TAB; printf("Enter: %s", X); depth += 2; }
#define DEBUG_ENTER_P1(X,A) if (debug_parse) { DEBUG_TAB; printf("Enter: "); printf(X,A); depth += 2; }
#define DEBUG_ENTER_P2(X,A,B) if (debug_parse) { DEBUG_TAB; printf("Enter: "); printf(X,A,B); depth += 2; }
#define DEBUG_ENTER_P3(X,A,B,C) if (debug_parse) { DEBUG_TAB; printf("Enter: "); printf(X,A,B,C); depth += 2; }
#define DEBUG_EXIT(X) if (debug_parse) { depth -=2; DEBUG_TAB; printf("Leave: %s", X); }
#define DEBUG_EXIT_P1(X,A) if (debug_parse) { depth -=2; DEBUG_TAB; printf("Leave: "); printf(X,A); }
#define DEBUG_TAB if (debug_parse) printf("%*.*s", depth, depth, "")
#define DEBUG_NL if (debug_parse) printf("\n")
#define DEBUG_PT(X) if (debug_parse) result_print(X, stdout_stream);
#define DEBUG_PO(X) if (debug_parse) rules_print(stdout, X)
#define DEBUG_PR(X) if (debug_parse) element_print(stdout, X)
#define DEBUG_(X)  if (debug_parse) printf(X)
#define DEBUG_P1(X,A) if (debug_parse) printf(X,A)


/*
	Parser struct definition
	~~~~~~~~~~~~~~~~~~~~~~~~
*/

typedef struct nt_stack *nt_stack_p;

typedef struct
{
	text_buffer_p text_buffer;
	nt_stack_p nt_stack;
	cache_item_p (*cache_hit_function)(void *cache, size_t pos, const char *nt);
	void *cache;
} parser_t, *parser_p;

void parser_init(parser_p parser, text_buffer_p text_buffer)
{
	parser->text_buffer = text_buffer;
	parser->nt_stack = NULL;
	parser->cache_hit_function = 0;
	parser->cache = NULL;
}

nt_stack_p nt_stack_push(const char *name, parser_p parser);
nt_stack_p nt_stack_pop(nt_stack_p cur);

/*
	Parsing functions
	~~~~~~~~~~~~~~~~~
	
	The parsing functions are described top-down, starting with the function
	to parse a non-terminal, which is the top-level function to be called to
	parse a text buffer.
	
*/

bool parse_rule(parser_p parser, element_p element, const result_p prev_result, rule_p rules, result_p rule_result);

bool parse_nt(parser_p parser, non_terminal_p non_term, result_p result)
{
	ENTER_RESULT_CONTEXT
	const char *nt = non_term->name;

	DEBUG_ENTER_P3("parse_nt(%s) at %d.%d", nt, parser->text_buffer->pos.cur_line, parser->text_buffer->pos.cur_column); DEBUG_NL;

	/* First try the cache (if available) */
	cache_item_p cache_item = NULL;
	if (parser->cache_hit_function != NULL)
	{
		cache_item = parser->cache_hit_function(parser->cache, parser->text_buffer->pos.pos, nt);
		if (cache_item != NULL)
		{
			if (cache_item->success == s_success)
			{
				DEBUG_EXIT_P1("parse_nt(%s) CACHE SUCCESS = ", nt);  DEBUG_PT(&cache_item->result)  DEBUG_NL;
				result_assign(result, &cache_item->result);
				text_buffer_set_pos(parser->text_buffer, &cache_item->next_pos);
				EXIT_RESULT_CONTEXT
				return TRUE;
			}
			else if (cache_item->success == s_fail)
			{
				DEBUG_EXIT_P1("parse_nt(%s) CACHE FAIL", nt);  DEBUG_NL;
				EXIT_RESULT_CONTEXT
				return FALSE;
			}
			cache_item->success = s_fail; // To deal with indirect left-recurssion
		}
	}
	
	/* Push the current non-terminal on stack */
	parser->nt_stack = nt_stack_push(nt, parser);

	if (debug_nt)
	{   printf("%*.*s", depth, depth, "");
		printf("Enter: %s\n", nt);
		depth += 2; 
	}

	/* Try the normal rules in order of declaration */
	bool parsed_a_rule = FALSE;
	for (rule_p rule = non_term->normal; rule != NULL; rule = rule->next )
	{
		DECL_RESULT(start)
		if (parse_rule(parser, rule->elements, &start, rule, result))
		{
			parsed_a_rule = TRUE;
			DISP_RESULT(start)
			break;
		}
		DISP_RESULT(start)
	}
	
	if (!parsed_a_rule)
	{
		/* No rule was succesful */
		DEBUG_EXIT_P1("parse_nt(%s) - failed", nt);  DEBUG_NL;
		if (debug_nt)
		{   depth -= 2;
			printf("%*.*s", depth, depth, "");
			printf("Failed: %s\n", nt);
		}
		
		/* Pop the current non-terminal from the stack */
		parser->nt_stack = nt_stack_pop(parser->nt_stack);
		
		EXIT_RESULT_CONTEXT
		return FALSE;
	}
	
	/* Now that a normal rule was succesfull, repeatingly try left-recursive rules */
	while (parsed_a_rule)
	{
		parsed_a_rule = FALSE;
		for (rule_p rule = non_term->recursive; rule != NULL; rule = rule->next)
		{
			DECL_RESULT(start_result)
			if (rule->rec_start_function != NULL)
			{
				if (!rule->rec_start_function(result, &start_result))
				{
					DISP_RESULT(start_result)
					continue;
				}
			}
			DECL_RESULT(rule_result)
			if (parse_rule(parser, rule->elements, &start_result, rule, &rule_result))
			{
				parsed_a_rule = TRUE;
				result_assign(result, &rule_result);
				DISP_RESULT(rule_result)
				DISP_RESULT(start_result)
				break;
			}
			DISP_RESULT(rule_result)
			DISP_RESULT(start_result)
		}
	}

	DEBUG_EXIT_P1("parse_nt(%s) = ", nt);
	DEBUG_PT(result); DEBUG_NL;
	if (debug_nt)
	{   depth -= 2;
		printf("%*.*s", depth, depth, "");
		printf("Parsed: %s\n", nt);
	}
	
	/* Update the cache item, if available */
	if (cache_item != NULL)
	{
		result_assign(&cache_item->result, result);
		cache_item->success = s_success;
		cache_item->next_pos = parser->text_buffer->pos;
	}

	/* Pop the current non-terminal from the stack */
	parser->nt_stack = nt_stack_pop(parser->nt_stack);
	
	EXIT_RESULT_CONTEXT
	return TRUE;
}

/*
	Parsing a rule
	~~~~~~~~~~~~~~
	
	This function is called to parse (the remainder of) a rule. If it fails,
	the current position in text buffer is reset to the position it was at
	the start of the call. This function first tries to parse the first
	element of the rule. If this is succeeds, the function will be called
	recursively for the rest of the rule.
	
*/

bool parse_element(parser_p parser, element_p element, const result_p prev_result, result_p result);
bool parse_seq(parser_p parser, element_p element, const result_p prev_seq, const result_p prev, rule_p rule, result_p result);

bool parse_rule(parser_p parser, element_p element, const result_p prev_result, rule_p rule, result_p rule_result)
{
	ENTER_RESULT_CONTEXT
	DEBUG_ENTER_P2("parse_rule at %d.%d: ", parser->text_buffer->pos.cur_line, parser->text_buffer->pos.cur_column);
	DEBUG_PR(element); DEBUG_NL;

	if (element == NULL)
	{
		/* At the end of the rule: */
		if (rule == NULL || rule->end_function == 0)
			result_assign(rule_result, prev_result);
		else if (!rule->end_function(prev_result, rule->end_function_data, rule_result))
		{
			DEBUG_EXIT("parse_rule failed by end function "); DEBUG_NL;
			return FALSE;
		}
		DEBUG_EXIT("parse_rule = ");
		DEBUG_PT(rule_result); DEBUG_NL;
		EXIT_RESULT_CONTEXT
		return TRUE;
	}

	/* If the first element is optional and should be avoided, first an attempt
	   will be made to skip the element and parse the remainder of the rule */
	if (element->optional && element->avoid)
	{
		/* If a add skip function is defined, apply it. (An add skip function
		   can be used to process the absence of the element with the result.)
		   Otherwise, if a add function is defined, it will be called with an
		   'empty' result, signaling that no element was parsed.
		   Otherwise, the previous result is used. */
		DECL_RESULT(skip_result);
		if (element->add_skip_function != NULL)
		{
			if (!element->add_skip_function(prev_result, &skip_result))
			{
				DISP_RESULT(skip_result);
	            DEBUG_EXIT("parse_rule failed due to add skip function"); DEBUG_NL;
				EXIT_RESULT_CONTEXT
				return FALSE;
			}
		}
		else if (element->add_function != NULL)
		{
			DECL_RESULT(empty);
			if (!element->add_function(prev_result, &empty, &skip_result))
			{
				DISP_RESULT(empty);
				DISP_RESULT(skip_result);
	            DEBUG_EXIT("parse_rule failed due to add function"); DEBUG_NL;
				EXIT_RESULT_CONTEXT
				return FALSE;
			}
			DISP_RESULT(empty);
		}
		else
			result_assign(&skip_result, prev_result);
			
		if (parse_rule(parser, element->next, &skip_result, rule, rule_result))
		{
			DISP_RESULT(skip_result);
            DEBUG_EXIT("parse_rule = ");
            DEBUG_PT(rule_result); DEBUG_NL;
			EXIT_RESULT_CONTEXT
			return TRUE;
		}
		DISP_RESULT(skip_result);
	}
		
	/* Store the current position */
	text_pos_t sp = parser->text_buffer->pos;
	
	if (element->sequence)
	{
		/* The first element of the rule is a sequence. */
		DECL_RESULT(seq_begin);
		if (element->begin_seq_function != NULL)
			element->begin_seq_function(prev_result, &seq_begin);
		
		/* Try to parse the first element of the sequence. */
		DECL_RESULT(seq_elem);
		if (parse_element(parser, element, &seq_begin, &seq_elem))
		{
			if (element->back_tracking)
			{
				/* Now parse the remainder elements of the sequence (and thereafter the remainder of the rule. */
				if (parse_seq(parser, element, &seq_elem, prev_result, rule, rule_result))
				{
					DISP_RESULT(seq_elem);
					DISP_RESULT(seq_begin);
					DEBUG_EXIT("parse_rule = ");
					DEBUG_PT(rule_result); DEBUG_NL;
					EXIT_RESULT_CONTEXT
					return TRUE;
				}
			}
			else
			{
				/* Now continue parsing more elements */
				for (;;)
				{
					if (element->avoid)
					{
						DECL_RESULT(result);
						if (element->add_seq_function != NULL && !element->add_seq_function(prev_result, &seq_elem, element->add_seq_function_data, &result))
						{
							DEBUG_TAB; DEBUG_("add_seq_function failed\n");
							break;
						}
						if (parse_rule(parser, element->next, &result, rule, rule_result))
						{
							DISP_RESULT(result);
							DISP_RESULT(seq_elem);
							DISP_RESULT(seq_begin);
							DEBUG_EXIT("parse_rule = ");
							DEBUG_PT(rule_result); DEBUG_NL;
							EXIT_RESULT_CONTEXT
							return TRUE;
						}
						DISP_RESULT(result);
					}
					
					/* Store the current position */
					text_pos_t sp = parser->text_buffer->pos;
					
					if (element->chain_rule != NULL)
					{
						DECL_RESULT(dummy_prev_result);
						DECL_RESULT(dummy_chain_elem);
						bool parsed_chain = parse_rule(parser, element->chain_rule, &dummy_prev_result, NULL, &dummy_chain_elem);
						DISP_RESULT(dummy_chain_elem);
						DISP_RESULT(dummy_prev_result);
						if (!parsed_chain)
							break;
					}
					
					DECL_RESULT(next_seq_elem);
					if (parse_element(parser, element, &seq_elem, &next_seq_elem))
					{
						result_assign(&seq_elem, &next_seq_elem);
					}
					else
					{
						/* Failed to parse the next element of the sequence: reset the current position to the saved position. */
						text_buffer_set_pos(parser->text_buffer, &sp);
						DISP_RESULT(next_seq_elem);
						break;
					}
					DISP_RESULT(next_seq_elem);
				}
				
				DECL_RESULT(result);
				if (element->add_seq_function != NULL && !element->add_seq_function(prev_result, &seq_elem, element->add_seq_function_data, &result))
				{
					DEBUG_TAB; DEBUG_("add_seq_function failed\n");
				}
				else
				{
					if (parse_rule(parser, element->next, &result, rule, rule_result))
					{
						DISP_RESULT(result);
						DISP_RESULT(seq_elem);
						DISP_RESULT(seq_begin);
						DEBUG_EXIT("parse_rule = ");
						DEBUG_PT(rule_result); DEBUG_NL;
						EXIT_RESULT_CONTEXT
						return TRUE;
					}
				}
				DISP_RESULT(result);
			}
		}
		DISP_RESULT(seq_elem);
		DISP_RESULT(seq_begin);
	}
	else
	{
		/* The first element is not a sequence: Try to parse the first element */
		DECL_RESULT(elem);
		if (parse_element(parser, element, prev_result, &elem))
		{
			if (parse_rule(parser, element->next, &elem, rule, rule_result))
			{
				DISP_RESULT(elem);
				DEBUG_EXIT("parse_rule = ");
				DEBUG_PT(rule_result); DEBUG_NL;
				EXIT_RESULT_CONTEXT
				return TRUE;
			}
		}
		DISP_RESULT(elem);
	}
	
	/* Failed to parse the rule: reset the current position to the saved position. */
	text_buffer_set_pos(parser->text_buffer, &sp);
	
	/* If the element was optional (and should not be avoided): Skip the element
	   and try to parse the remainder of the rule */
	if (element->optional && !element->avoid)
	{
		DECL_RESULT(skip_result);
		if (element->add_skip_function != NULL)
		{
			if (!element->add_skip_function(prev_result, &skip_result))
			{
				DISP_RESULT(skip_result);
	            DEBUG_EXIT("parse_rule failed due to add skip function"); DEBUG_NL;
				EXIT_RESULT_CONTEXT
				return FALSE;
			}
		}
		else if (element->add_function != NULL)
		{
			DECL_RESULT(empty);
			if (!element->add_function(prev_result, &empty, &skip_result))
			{
				DISP_RESULT(empty);
				DISP_RESULT(skip_result);
	            DEBUG_EXIT("parse_rule failed due to add function"); DEBUG_NL;
				EXIT_RESULT_CONTEXT
				return FALSE;
			}
			DISP_RESULT(empty);
		}
		else
			result_assign(&skip_result, prev_result);
			
		if (parse_rule(parser, element->next, &skip_result, rule, rule_result))
		{
			DISP_RESULT(skip_result);
            DEBUG_EXIT("parse_rule = ");
            DEBUG_PT(rule_result); DEBUG_NL;
			EXIT_RESULT_CONTEXT
			return TRUE;
		}
		DISP_RESULT(skip_result);
	}

    DEBUG_EXIT("parse_rule: failed"); DEBUG_NL;
	EXIT_RESULT_CONTEXT
	return FALSE;
}

bool parse_seq(parser_p parser, element_p element, const result_p prev_seq, const result_p prev, rule_p rule, result_p rule_result)
{
	ENTER_RESULT_CONTEXT
	/* In case of the avoid modifier, first an attempt is made to parse the
	   remained of the rule */
	if (element->avoid)
	{
		DECL_RESULT(result);
		if (element->add_seq_function != NULL && !element->add_seq_function(prev, prev_seq, element->add_seq_function_data, &result))
		{
			DISP_RESULT(result);
			EXIT_RESULT_CONTEXT
			return FALSE;
		}
		if (parse_rule(parser, element->next, &result, rule, rule_result))
		{
			DISP_RESULT(result);
			EXIT_RESULT_CONTEXT
			return TRUE;
		}
		DISP_RESULT(result);
	}
	
	/* Store the current position */
	text_pos_t sp = parser->text_buffer->pos;

	/* If a chain rule is defined, try to parse it.*/
	bool go = TRUE;
	if (element->chain_rule != NULL)
	{
		DECL_RESULT(dummy_prev_result);
		DECL_RESULT(dummy_chain_elem);
		go = parse_rule(parser, element->chain_rule, &dummy_prev_result, NULL, &dummy_chain_elem);
		DISP_RESULT(dummy_chain_elem);
		DISP_RESULT(dummy_prev_result);
	}
	if (go)
	{
		/* Try to parse the next element of the sequence */
		DECL_RESULT(seq_elem);
		if (parse_element(parser, element, prev_seq, &seq_elem))
		{
			/* If succesful, try to parse the remainder of the sequence (and thereafter the remainder of the rule) */
			if (parse_seq(parser, element, &seq_elem, prev, rule, rule_result))
			{
				DISP_RESULT(seq_elem);
				EXIT_RESULT_CONTEXT
				return TRUE;
			}
		}
		DISP_RESULT(seq_elem);
	}
	
	/* Failed to parse the next element of the sequence: reset the current position to the saved position. */
	text_buffer_set_pos(parser->text_buffer, &sp);

	/* In case of the avoid modifier, an attempt to parse the remained of the
	   rule, was already made. So, only in case of no avoid modifier, attempt
	   to parse the remainder of the rule */
	if (!element->avoid)
	{
		DECL_RESULT(result);
		if (element->add_seq_function != NULL && !element->add_seq_function(prev, prev_seq, element->add_seq_function_data, &result))
		{
			DISP_RESULT(result);
			EXIT_RESULT_CONTEXT
			return FALSE;
		}
		
		if (parse_rule(parser, element->next, &result, rule, rule_result))
		{
			DISP_RESULT(result);
			EXIT_RESULT_CONTEXT
			return TRUE;
		}
		DISP_RESULT(result);
	}
	
	EXIT_RESULT_CONTEXT
	return FALSE;
}


/*
	Parse an element
	~~~~~~~~~~~~~~~~
	
	The following function is used to parse a part of an element, not dealing
	with if the element is optional or a sequence.
*/

void expect_element(parser_p parser, element_p element);

bool parse_element(parser_p parser, element_p element, const result_p prev_result, result_p result)
{
	DEBUG_ENTER_P2("parse_element at %d.%d: ", parser->text_buffer->pos.cur_line, parser->text_buffer->pos.cur_column);
	DEBUG_PR(element); DEBUG_NL;

	ENTER_RESULT_CONTEXT
	/* Store the current position */
	text_pos_t sp = parser->text_buffer->pos;

	switch( element->kind )
	{
		case rk_nt:
			{
				/* Parse the non-terminal */
				DECL_RESULT(nt_result)
				if (!parse_nt(parser, element->info.non_terminal, &nt_result))
				{
					DISP_RESULT(nt_result)
					EXIT_RESULT_CONTEXT
					DEBUG_EXIT("parse_element failed due to add skip function"); DEBUG_NL;
					return FALSE;
				}
				
				/* If there is a condition, evaluate the result */
				if (element->condition != 0 && !(*element->condition)(&nt_result, element->condition_argument))
				{
					DISP_RESULT(nt_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					EXIT_RESULT_CONTEXT
					DEBUG_EXIT("parse_element failed due to condition function"); DEBUG_NL;
					return FALSE;
				}
				
				/* Combine the result with the previous result */
				if (element->add_function == 0)
					result_assign(result, prev_result);
				else if (!(*element->add_function)(prev_result, &nt_result, result))
				{
					DISP_RESULT(nt_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					EXIT_RESULT_CONTEXT
					DEBUG_EXIT("parse_element failed due to add function"); DEBUG_NL;
					return FALSE;
				}
				DISP_RESULT(nt_result)
			}
			break;
		case rk_grouping:
			{
				/* Try all rules in the grouping */
				DECL_RESULT(rule_result);
				rule_p rule = element->info.rules;
				for ( ; rule != NULL; rule = rule->next )
				{
					DECL_RESULT(start);
					if (element->add_function == 0)
						result_assign(&start, prev_result);
					if (parse_rule(parser, rule->elements, &start, rule, &rule_result))
					{
						DISP_RESULT(start);
						break;
					}
					DISP_RESULT(start);
				}
				if (rule == NULL)
				{
					/* Non of the rules worked */
					DISP_RESULT(rule_result)
					EXIT_RESULT_CONTEXT
					DEBUG_EXIT("parse_element failed due to no rules parsed"); DEBUG_NL;
					return FALSE;
				}
				
				/* Combine the result of the rule with the previous result */
				if (element->add_function == 0)
					result_assign(result, &rule_result);
				else if (!(*element->add_function)(prev_result, &rule_result, result))
				{
					DISP_RESULT(rule_result)
					text_buffer_set_pos(parser->text_buffer, &sp);
					EXIT_RESULT_CONTEXT
					DEBUG_EXIT("parse_element failed due to add function"); DEBUG_NL;
					return FALSE;
				}
				DISP_RESULT(rule_result)
			}
			break;
		case rk_end:
			/* Check if the end of the buffer is reached */
			if (!text_buffer_end(parser->text_buffer))
			{
				expect_element(parser, element);
				EXIT_RESULT_CONTEXT
				DEBUG_EXIT("parse_element failed due to accept end"); DEBUG_NL;
				return FALSE;
			}
			result_assign(result, prev_result);
			break;
		case rk_char:
			/* Check if the specified character is found at the current position in the text buffer */
			if (*parser->text_buffer->info != element->info.ch)
			{
				expect_element(parser, element);
				EXIT_RESULT_CONTEXT
				DEBUG_EXIT_P1("parse_element failed due to accept char '%c'", element->info.ch); DEBUG_NL;
				return FALSE;
			}
			/* Advance the current position of the text buffer */
			text_buffer_next(parser->text_buffer);
			/* Process the character */
			if (element->add_char_function == 0)
				result_assign(result, prev_result);
			else if (!(*element->add_char_function)(prev_result, element->info.ch, result))
			{
				EXIT_RESULT_CONTEXT
				DEBUG_EXIT("parse_element failed due to add char function"); DEBUG_NL;
				return FALSE;
			}
			break;
		case rk_charset:
			/* Check if the character at the current position in the text buffer is found in the character set */
			if (!char_set_contains(element->info.char_set, *parser->text_buffer->info))
			{
				expect_element(parser, element);
				EXIT_RESULT_CONTEXT
				DEBUG_EXIT("parse_element failed due to add charset"); DEBUG_NL;
				return FALSE;
			}
			{
				/* Remember the character and advance the current position of the text buffer */
				char ch = *parser->text_buffer->info;
				text_buffer_next(parser->text_buffer);
				/* Process the character */
				if (element->add_char_function == 0)
					result_assign(result, prev_result);
				else if (!(*element->add_char_function)(prev_result, ch, result))
				{
					EXIT_RESULT_CONTEXT
					DEBUG_EXIT("parse_element failed due to add char function"); DEBUG_NL;
					return FALSE;
				}
			}
			break;
		case rk_term:
			/* Call the terminal parse function and see if it has parsed something */
			{
				const char *next_pos = element->info.terminal_function(parser->text_buffer->info, result);
				/* If the start position is returned, assume that it failed. */
				if (next_pos <= parser->text_buffer->info)
				{
					expect_element(parser, element);
					EXIT_RESULT_CONTEXT
					DEBUG_EXIT("parse_element failed due to parse terminal function"); DEBUG_NL;
					return FALSE;
				}
				/* Increment the buffer till the returned position */
				while (parser->text_buffer->info < next_pos)
					text_buffer_next(parser->text_buffer);
			}
			break;
		default:
			EXIT_RESULT_CONTEXT
			DEBUG_EXIT("parse_element failed due to unknown element"); DEBUG_NL;
			return FALSE;
			break;
	}
	
	/* Set the position on the result */
	if (element->set_pos != NULL)
		element->set_pos(result, &sp);

	EXIT_RESULT_CONTEXT
	DEBUG_EXIT("parse_element succeeded "); /*print_result(result);*/ DEBUG_NL;
	return TRUE;
}


/*
	Brute force cache
	~~~~~~~~~~~~~~~~~
	
	A simple cache implementation, is one that simply stores all results for
	all positions in the input text.

*/

typedef struct solution *solution_p;
struct solution
{
	cache_item_t cache_item;
	const char *nt;
	solution_p next;
};
typedef struct
{
	solution_p *sols;        /* Array of solutions at locations */
	size_t len;              /* Length of array (equal to length of input) */
} solutions_t, *solutions_p;


void solutions_init(solutions_p solutions, text_buffer_p text_buffer)
{
    solutions->len = text_buffer->buffer_len;
	solutions->sols = MALLOC_N(solutions->len+1, solution_p);
	size_t i;
	for (i = 0; i < solutions->len+1; i++)
		solutions->sols[i] = NULL;
}

void solutions_free(solutions_p solutions)
{
	size_t i;
	for (i = 0; i < solutions->len+1; i++)
	{	solution_p sol = solutions->sols[i];

		while (sol != NULL)
		{	if (sol->cache_item.result.dec != 0)
		    	sol->cache_item.result.dec(sol->cache_item.result.data);
			solution_p next_sol = sol->next;
		    FREE(sol);
			sol = next_sol;
		}
  	}
	FREE(solutions->sols);
}

cache_item_p solutions_find(void *cache, size_t pos, const char *nt)
{
	solutions_p solutions = (solutions_p)cache;
	solution_p sol;

	if (pos > solutions->len)
		pos = solutions->len;

	for (sol = solutions->sols[pos]; sol != NULL; sol = sol->next)
		if (sol->nt == nt)
		 	return &sol->cache_item;

	sol = MALLOC(struct solution);
	sol->next = solutions->sols[pos];
	sol->nt = nt;
	sol->cache_item.success = s_unknown;
	RESULT_INIT(&sol->cache_item.result);
	solutions->sols[pos] = sol;
	return &sol->cache_item;
}

/*
	White space tests
	~~~~~~~~~~~~~~~~~
*/

void test_parse_white_space(non_terminal_dict_p *all_nt, const char *input)
{
	ENTER_RESULT_CONTEXT

	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("white_space", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		fprintf(stderr, "OK: parsed white space\n");
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse white space from '%s'\n", input);
	}
	DISP_RESULT(result);

	solutions_free(&solutions);

	EXIT_RESULT_CONTEXT
}

void test_white_space_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_white_space(all_nt, " ");
	test_parse_white_space(all_nt, "/* */");
}

/*
	Number tests
	~~~~~~~~~~~~
*/

void test_parse_number(non_terminal_dict_p *all_nt, const char *input, int num)
{
	ENTER_RESULT_CONTEXT
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("number", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else if (CAST(number_data_p, result.data)->num != num)
			fprintf(stderr, "ERROR: parsed value %ld from '%s' instead of expected %d\n",
				CAST(number_data_p, result.data)->num, input, num);
		else
			fprintf(stderr, "OK: parsed value %ld from '%s'\n", CAST(number_data_p, result.data)->num, input);
	}
	else
		fprintf(stderr, "ERROR: failed to parse number from '%s'\n", input);
	DISP_RESULT(result);

	solutions_free(&solutions);
	EXIT_RESULT_CONTEXT
}

void test_number_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_number(all_nt, "0", 0);
	test_parse_number(all_nt, "123", 123);
}

/*
	Abstract Syntax Tree
	~~~~~~~~~~~~~~~~~~~~
	The following section of the code implements a representation for
	Abstract Syntax Trees.
*/

typedef struct
{
	ref_counted_base_t _base;
	const char *type_name;
	unsigned int line;
	unsigned int column;
} node_t, *node_p;

DEFINE_BASE_TYPE(node_p)

void init_node(node_p node, const char *type_name, void (*release_node)(void *))
{
	node->_base.cnt = 1;
	node->_base.release = release_node;
	node->type_name = type_name;
	node->line = 0;
	node->column = 0;
}

void node_set_pos(node_p node, text_pos_p ps)
{
	node->line = ps->cur_line;
	node->column = ps->cur_column;
}

typedef struct tree_param_t tree_param_t, *tree_param_p;
struct tree_param_t
{ 
	const char *name;
	const char *fmt;
};

typedef struct tree_t *tree_p;
struct tree_t
{
	node_t _node;
	tree_param_p tree_param;
	unsigned int nr_children;
	result_t *children;
};

DEFINE_SUB_BASE_TYPE(tree_p, node_p)

tree_p old_tree_nodes = NULL;
long nr_allocated_tree_nodes = 0L;

void release_tree(void *data)
{
	tree_p tree = CAST(tree_p, data);

	nr_allocated_tree_nodes--;

	if (tree->nr_children > 0)
	{
		for (int i = 0; i < tree->nr_children; i++)
			RESULT_RELEASE(&tree->children[i]);
		FREE(tree->children);
	}
	*(tree_p*)tree = old_tree_nodes;
	old_tree_nodes = tree;
}

const char *tree_node_type = "tree_node_type";

tree_p malloc_tree(tree_param_p tree_param)
{
	tree_p new_tree_node;

	if (old_tree_nodes != NULL)
	{   new_tree_node = old_tree_nodes;
		old_tree_nodes = *(tree_p*)old_tree_nodes;
	}
	else
		new_tree_node = MALLOC(struct tree_t);

	init_node(&new_tree_node->_node, tree_node_type, release_tree);
	new_tree_node->tree_param = tree_param;
	new_tree_node->nr_children = 0;
	new_tree_node->children = NULL;
	
	nr_allocated_tree_nodes++;

	return new_tree_node;
}

bool tree_is(tree_p tree, const char *name)
{
	return tree != NULL && tree->tree_param != NULL && tree->tree_param->name != NULL && strcmp(tree->tree_param->name, name) == 0;
}

bool node_is_tree(node_p node, const char *name)
{
	return node != NULL && node->type_name == tree_node_type && tree_is(CAST(tree_p, node), name);
}

result_p tree_child(tree_p tree, int nr)
{
	return tree != NULL && nr <= tree->nr_children ? &tree->children[nr-1] : NULL;
}

typedef struct prev_child_t *prev_child_p;
struct prev_child_t
{
	ref_counted_base_t _base;
	prev_child_p prev;
	result_t child;
};

DEFINE_BASE_TYPE(prev_child_p)

prev_child_p old_prev_childs = NULL;

void release_prev_child( void *data )
{
	prev_child_p prev_child = CAST(prev_child_p, data);
	RESULT_RELEASE(&prev_child->child);
	if (prev_child != NULL)
		ref_counted_base_dec(prev_child);
	prev_child->prev = old_prev_childs;
	old_prev_childs = prev_child;
}

prev_child_p malloc_prev_child()
{
	prev_child_p new_prev_child;
	if (old_prev_childs != NULL)
	{
		new_prev_child = old_prev_childs;
		old_prev_childs = old_prev_childs->prev;
	}
	else
		new_prev_child = MALLOC(struct prev_child_t);
	new_prev_child->_base.cnt = 1;
	new_prev_child->_base.release = release_prev_child;
	RESULT_INIT(&new_prev_child->child);
	new_prev_child->prev = NULL;
	return new_prev_child;
}

void prev_child_print(void *data, ostream_p ostream)
{
	ostream_puts(ostream, "prev_child[ ");
	prev_child_p prev_child = CAST(prev_child_p, data);
	for (; prev_child != NULL; prev_child = prev_child->prev)
	{
		if (prev_child->child.data == NULL || prev_child->child.print == NULL)
			ostream_puts(ostream, "NULL");
		else
			prev_child->child.print(prev_child->child.data, ostream);
		printf(" ");
	}
	ostream_puts(ostream, "]");
}

bool add_child(result_p prev, result_p elem, result_p result)
{
	prev_child_p prev_child = CAST(prev_child_p, prev->data);
	if (prev_child != NULL)
		ref_counted_base_inc(prev_child);
	prev_child_p new_prev_child = malloc_prev_child();
	new_prev_child->prev = prev_child;
	result_assign(&new_prev_child->child, elem);
	result_assign_ref_counted(result, &new_prev_child->_base, prev_child_print);
	SET_TYPE(prev_child_p, new_prev_child);
	return TRUE;
}

void prepend_child(result_p children, result_p elem)
{
	prev_child_p prev_child = CAST(prev_child_p, children->data);
	if (prev_child != NULL)
		ref_counted_base_inc(prev_child);
	prev_child_p new_prev_child = malloc_prev_child();
	new_prev_child->prev = prev_child;
	result_assign(&new_prev_child->child, elem);
	ENTER_RESULT_CONTEXT
	DECL_RESULT(new_children);
	result_assign_ref_counted(&new_children, &new_prev_child->_base, prev_child_print);
	SET_TYPE(prev_child_p, new_prev_child);
	result_assign(children, &new_children);
	DISP_RESULT(new_children);
	EXIT_RESULT_CONTEXT
}

bool rec_add_child(result_p rec_result, result_p result)
{
	prev_child_p new_prev_child = malloc_prev_child();
	new_prev_child->prev = NULL;
	result_assign(&new_prev_child->child, rec_result);
	result_assign_ref_counted(result, &new_prev_child->_base, prev_child_print);
	SET_TYPE(prev_child_p, new_prev_child);
	return TRUE;
}

bool take_child(result_p prev, result_p elem, result_p result)
{
	result_assign(result, elem);
	return TRUE;
}

tree_p make_tree_with_children(tree_param_p tree_param, prev_child_p children)
{
	tree_p tree = malloc_tree(tree_param);
	prev_child_p child;
	int i = 0;
	for (child = children; child != NULL; child = child->prev)
		i++;
	tree->nr_children = i;
	tree->children = MALLOC_N(tree->nr_children, result_t);
	for (child = children; child != NULL; child = child->prev)
	{
		i--;
		RESULT_INIT(&tree->children[i]);
		result_assign(&tree->children[i], &child->child);
	}
	return tree;
}

tree_p make_tree_with_children_of_tree(tree_param_p tree_param, tree_p list)
{
	tree_p tree = malloc_tree(tree_param);
	tree->nr_children = list->nr_children;
	tree->children = MALLOC_N(tree->nr_children, result_t);
	for (int i = 0; i < tree->nr_children; i++)
	{
		RESULT_INIT(&tree->children[i]);
		result_assign(&tree->children[i], &list->children[i]);
	}
	return tree;
}

void tree_print(void *data, ostream_p ostream)
{
	tree_p tree = CAST(tree_p, data);
	if (tree->_node.type_name != NULL)
		ostream_puts(ostream, tree->tree_param->name);
	ostream_put(ostream, '(');
	for (int i = 0; i < tree->nr_children; i++)
	{
		if (i > 0)
			ostream_put(ostream, ',');
		result_print(&tree->children[i], ostream);
	}
	ostream_put(ostream, ')');
}

bool make_tree(const result_p rule_result, void* data, result_p result)
{
	prev_child_p children = CAST(prev_child_p, rule_result->data);
	tree_param_p tree_param = (tree_param_p)data;
	tree_p tree = make_tree_with_children(tree_param, children);
	result_assign_ref_counted(result, &tree->_node._base, tree_print);
	SET_TYPE(tree_p, tree);
	return TRUE;
}

bool make_tree_from_list(const result_p rule_result, void* data, result_p result)
{
	prev_child_p children = CAST(prev_child_p, rule_result->data);
	tree_param_p tree_param = (tree_param_p)data;
	tree_p tree = (   children != 0 && children->prev == 0 
	    		   && children->child.data != 0 && node_is_tree(CAST(node_p, children->child.data), "list"))
	    		? make_tree_with_children_of_tree(tree_param, CAST(tree_p, children->child.data))
				: make_tree_with_children(tree_param, children);
	result_assign_ref_counted(result, &tree->_node._base, tree_print);
	SET_TYPE(tree_p, tree);
	return TRUE;
}

bool pass_tree(const result_p rule_result, void* data, result_p result)
{
	prev_child_p child = CAST(prev_child_p, rule_result->data);
	result_transfer(result, &child->child);
	return TRUE;
}


/*
	Keywords
	~~~~~~~~
	Many programming languages have keywords, which have the same lexical
	catagory as identifiers. This means we need some function to test
	whether an identifier is equal to one of the keywords. One way to
	do this is to use hexadecimal hash tree. This can also be used to map
	every identifier to a unique pointer, such that comparing two identifiers
	can simply be done by comparing the two pointers.

	A hexadecimal hash tree
	~~~~~~~~~~~~~~~~~~~~~~~	
	The following structure implements a mapping of strings to an integer
	value in the range [0..254]. It is a tree of hashs in combination with
	a very fast incremental hash function. In this way, it tries to combine
	the benefits of trees and hashs. The incremental hash function will first
	return the lower 4 bits of the characters in the string, and following
	this the higher 4 bits of the characters.
*/

typedef struct hexa_hash_tree_t hexa_hash_tree_t, *hexa_hash_tree_p;

struct hexa_hash_tree_t
{	byte state;
	union
	{	char *string;
		hexa_hash_tree_p *children;
	} data;
};

byte *keyword_state = NULL;

char *ident_string(const char *s)
/*  Returns a unique address representing the string. the global
    keyword_state will point to the integer value in the range [0..254].
	If the string does not occure in the store, it is added and the state
	is initialized with 0.
*/
{
	static hexa_hash_tree_p hash_tree = NULL;
	hexa_hash_tree_p *r_node = &hash_tree;
	const char *vs = s;
	int depth;
	int mode = 0;

	for (depth = 0; ; depth++)
	{   hexa_hash_tree_p node = *r_node;

		if (node == NULL)
		{   node = MALLOC(hexa_hash_tree_t);
			node->state = 0;
			STRCPY(node->data.string, s);
			*r_node = node;
			keyword_state = &node->state;
			return node->data.string;
		}

		if (node->state != 255)
		{   char *cs = node->data.string;
			hexa_hash_tree_p *children;
			unsigned short i, v = 0;

			if (*cs == *s && strcmp(cs+1, s+1) == 0)
			{   keyword_state = &node->state;
				return node->data.string;
			}

			children = MALLOC_N(16, hexa_hash_tree_t*);
			for (i = 0; i < 16; i++)
				children[i] = NULL;

			i = strlen(cs);
			if (depth <= i)
				v = ((byte)cs[depth]) & 15;
			else if (depth <= i*2)
				v = ((byte)cs[depth-i-1]) >> 4;

			children[v] = node;

			node = MALLOC(hexa_hash_tree_t);
			node->state = 255;
			node->data.children = children;
			*r_node = node;
		}
		{   unsigned short v;
			if (*vs == '\0')
			{   v = 0;
				if (mode == 0)
				{   mode = 1;
					vs = s;
				}
			}
			else if (mode == 0)
				v = ((unsigned short)*vs++) & 15;
			else
				v = ((unsigned short)*vs++) >> 4;

			r_node = &node->data.children[v];
		}
	}
}

/*  Parsing an identifier  */

/*  Data structure needed during parsing.
    Only the first 64 characters of the identifier will be significant. */

typedef struct ident_data
{
	ref_counted_base_t _base;
	char ident[65];
	int len;
	text_pos_t ps;
} *ident_data_p;

DEFINE_BASE_TYPE(ident_data_p)

bool ident_add_char(result_p prev, char ch, result_p result)
{
	if (prev->data == NULL)
	{
		ident_data_p ident_data = MALLOC(struct ident_data);
		ident_data->_base.release = NULL;
		result_assign_ref_counted(result, &ident_data->_base, NULL);
		SET_TYPE(ident_data_p, ident_data);
		ident_data->ident[0] = ch;
		ident_data->len = 1;
	}
	else
	{
		result_assign(result, prev);
		ident_data_p ident_data = CAST(ident_data_p, result->data);
		if (ident_data->len < 64)
			ident_data->ident[ident_data->len++] = ch;
	}
	return TRUE;
}

void ident_set_pos(result_p result, text_pos_p ps)
{
	if (result->data != 0)
		(CAST(ident_data_p, result->data))->ps = *ps;
}

void pass_to_sequence(result_p prev, result_p seq)
{
	result_assign(seq, prev);
}

/*  Ident tree node structure */

typedef struct ident_node_t *ident_node_p;
struct ident_node_t
{
	node_t _node;
	char *name;
	bool is_keyword;
};

DEFINE_SUB_BASE_TYPE(ident_node_p, node_p)

void ident_print(void *data, ostream_p ostream)
{
	ostream_puts(ostream, CAST(ident_node_p, data)->name);
}
const char *ident_node_type = "ident_node_type";

bool create_ident_tree(const result_p rule_result, void* data, result_p result)
{
	ident_data_p ident_data = CAST(ident_data_p, rule_result->data);
	if (ident_data == 0)
	{
		fprintf(stderr, "NULL\n");
		return TRUE;
	}
	ident_data->ident[ident_data->len] = '\0';
	ident_node_p ident = MALLOC(struct ident_node_t);
	init_node(&ident->_node, ident_node_type, NULL);
	node_set_pos(&ident->_node, &ident_data->ps);
	ident->name = ident_string(ident_data->ident);
	ident->is_keyword = *keyword_state == 1;
	result_assign_ref_counted(result, &ident->_node._base, ident_print);
	SET_TYPE(ident_node_p, ident);
	return TRUE;
}

char *ident_name(result_p result)
{
	if (result == 0)
		return "<result_p is NULL>";
	if (result->data == 0)
		return "result_p->data is NULL>";
	node_p node = CAST(node_p, result->data);
	if (node->type_name != ident_node_type)
		return "<result_p not ident>";
	ident_node_p ident = CAST(ident_node_p, node);
	return ident->name;
}
		

/*  Ident grammar  */

void ident_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("ident")
		RULE
			CHARSET(ident_add_char) ADD_RANGE('a', 'z') ADD_RANGE('A', 'Z') ADD_CHAR('_') SET_PS(ident_set_pos)
			CHARSET(ident_add_char) ADD_RANGE('a', 'z') ADD_RANGE('A', 'Z') ADD_CHAR('_') ADD_RANGE('0', '9') SEQ(pass_to_sequence, use_sequence_result, NULL) OPT(0)
			END_FUNCTION(create_ident_tree)
}

/*
	Ident tests
	~~~~~~~~~~~
*/

void test_parse_ident(non_terminal_dict_p *all_nt, const char *input)
{
	ENTER_RESULT_CONTEXT
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("ident", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else
		{
			node_p node = CAST(node_p, result.data);
			if (node->line != 1 && node->column != 1)
				fprintf(stderr, "WARNING: tree node position %d:%d is not 1:1\n", node->line, node->column);
			if (node->type_name != ident_node_type)
				fprintf(stderr, "ERROR: tree node is not of type ident_node_type\n");
			else
			{
				ident_node_p ident = CAST(ident_node_p, node);
				if (strcmp(ident->name, input) != 0)
					fprintf(stderr, "ERROR: parsed value '%s' from '%s' instead of expected '%s'\n",
					ident->name, input, input);
				else
					fprintf(stderr, "OK: parsed ident '%s' from '%s'\n", ident->name, input);
			}
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse ident from '%s'\n", input);
	}
	DISP_RESULT(result);
	
	solutions_free(&solutions);
	EXIT_RESULT_CONTEXT
}

void test_ident_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_ident(all_nt, "aBc");
	test_parse_ident(all_nt, "_123");
}

/*
	Char result
	~~~~~~~~~~~

	The struct for representing the char has but two members,
	one for the character and one for the start position.
	(besides the member for the reference counting base).
*/

typedef struct char_data
{
	ref_counted_base_t _base;
	char ch;
	text_pos_t ps;
} *char_data_p;

DEFINE_BASE_TYPE(char_data_p)

void print_single_char(char ch, char del, ostream_p ostream)
{
	if (ch == '\0')
		ostream_puts(ostream, "\\0");
	else if (ch == del)
	{
		ostream_put(ostream, '\\');
		ostream_put(ostream, del);
	}
	else if (ch == '\n')
		ostream_puts(ostream, "\\n");
	else if (ch == '\r')
		ostream_puts(ostream, "\\r");
	else if (ch == '\\')
		ostream_puts(ostream, "\\\\");
	else
		ostream_put(ostream, ch);
}

void char_data_print(void *data, ostream_p ostream)
{
	ostream_puts(ostream, "char '");
	print_single_char(CAST(char_data_p, data)->ch, '\'', ostream);
	ostream_puts(ostream, "'");
}

void char_set_pos(result_p result, text_pos_p ps)
{
	char_data_p char_data = MALLOC(struct char_data);
	char_data->ps = *ps;
	char_data->_base.release = 0;
	result_assign_ref_counted(result, &char_data->_base, char_data_print);
	SET_TYPE(char_data_p, char_data);
}

bool normal_char(result_p prev, char ch, result_p result)
{
	result_assign(result, prev);
	CAST(char_data_p, result->data)->ch = ch;
	return TRUE;
}

bool escaped_char(result_p prev, char ch, result_p result)
{
	return normal_char(prev,
		ch == '0' ? '\0' :
		ch == 'a' ? '\a' :
		ch == 'b' ? '\b' :
		ch == 'f' ? '\f' :
		ch == 'n' ? '\n' :
		ch == 'r' ? '\r' :
		ch == 't' ? '\t' :
		ch == 'v' ? '\v' : ch, result);
}

/*	Char tree node structure */

typedef struct char_node_t *char_node_p;
struct char_node_t
{
	node_t _node;
	char ch;
};

DEFINE_SUB_BASE_TYPE(char_node_p, node_p)

const char *char_node_type = "char_node_type";

void char_node_print(void *data, ostream_p ostream)
{
	ostream_puts(ostream, "'");
	print_single_char(((char_node_p)data)->ch, '\'', ostream);
	ostream_puts(ostream, "'");
}

bool create_char_tree(const result_p rule_result, void* data, result_p result)
{
	char_data_p char_data = CAST(char_data_p, rule_result->data);

	char_node_p char_node = MALLOC(struct char_node_t);
	init_node(&char_node->_node, char_node_type, NULL);
	node_set_pos(&char_node->_node, &char_data->ps);
	char_node->ch = char_data->ch;
	result_assign_ref_counted(result, &char_node->_node._base, char_node_print);
	SET_TYPE(char_data_p, char_data);
	return TRUE;
}

/*  Char grammar  */

void char_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("char")
		RULE
			CHAR('\'') SET_PS(char_set_pos)
			{ GROUPING
				RULE // Escaped character
					CHAR('\\') CHARSET(escaped_char) ADD_CHAR('0') ADD_CHAR('\"') ADD_CHAR('\'') ADD_CHAR('\\') ADD_CHAR('a') ADD_CHAR('b') ADD_CHAR('f') ADD_CHAR('n') ADD_CHAR('r') ADD_CHAR('t') ADD_CHAR('v')
				RULE // Normal character
					CHARSET(normal_char) ADD_RANGE(' ', 126) REMOVE_CHAR('\\') REMOVE_CHAR('\'')
			}
			CHAR('\'')
			END_FUNCTION(create_char_tree)
}

/*
	Char tests
	~~~~~~~~~~
*/

void test_parse_char(non_terminal_dict_p *all_nt, const char *input, char ch)
{
	ENTER_RESULT_CONTEXT
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("char", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else
		{
			node_p node = CAST(node_p, result.data);
			if (node->line != 1 && node->column != 1)
				fprintf(stderr, "WARNING: tree node position %d:%d is not 1:1\n", node->line, node->column);
			if (node->type_name != char_node_type)
				fprintf(stderr, "ERROR: tree node is not of type char_node_type\n");
			else
			{
				char_node_p char_node = (char_node_p)node;
				if (char_node->ch != ch)
					fprintf(stderr, "ERROR: parsed value '%c' from '%s' instead of expected '%c'\n",
					char_node->ch, input, ch);
				else
					fprintf(stderr, "OK: parsed char %d from '%s'\n", char_node->ch, input);
			}
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse char from '%s'\n", input);
	}
	DISP_RESULT(result);
	
	solutions_free(&solutions);

	EXIT_RESULT_CONTEXT
}

void test_char_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_char(all_nt, "'c'", 'c');
	test_parse_char(all_nt, "'\\0'", '\0');
	test_parse_char(all_nt, "'\\''", '\'');
	test_parse_char(all_nt, "'\\\\'", '\\');
	test_parse_char(all_nt, "'\\n'", '\n');
}

/*
	String result
	~~~~~~~~~~~~~
	
	The struct representing the string has several members.
	But we first need to define a string buffer type that is
	needed as a temporary storage while the string is parsed
	before memory is allocated to contain the whole string.
*/

typedef struct string_buffer *string_buffer_p;
struct string_buffer
{
	char buf[100];
	string_buffer_p next;
};
string_buffer_p global_string_buffer = NULL;

string_buffer_p new_string_buffer()
{
	string_buffer_p string_buffer = MALLOC(struct string_buffer);
	string_buffer->next = NULL;
	return string_buffer;
}

typedef struct string_data
{
	ref_counted_base_t _base;
	string_buffer_p buffer;
	size_t length;
	char octal_char;
	text_pos_t ps;
} *string_data_p;

DEFINE_BASE_TYPE(string_data_p)

void string_data_print(void *data, ostream_p ostream)
{
	string_data_p string_data = CAST(string_data_p, data);
	ostream_puts(ostream, "char \"");
	string_buffer_p string_buffer = global_string_buffer;
	int j = 0;
	for (size_t i = 0; i < string_data->length; i++)
	{
		print_single_char(string_buffer->buf[j], '"', ostream);
		if (++j == 100)
		{
			string_buffer = string_buffer->next;
			if (string_buffer == NULL)
				break;
			j = 0;
		}
	}
	ostream_puts(ostream, "\"");
}

void string_set_pos(result_p result, text_pos_p ps)
{
	if (result->data == NULL)
	{
		string_data_p string_data = MALLOC(struct string_data);
		string_data->ps = *ps;
		string_data->buffer = NULL;
		string_data->length = 0;
		string_data->_base.release = 0;
		result_assign_ref_counted(result, &string_data->_base, string_data_print);
		SET_TYPE(string_data_p, string_data);
	}
}

bool string_data_add_normal_char(result_p prev, char ch, result_p result)
{
	result_assign(result, prev);
	string_data_p string_data = CAST(string_data_p, result->data);
	int j = string_data->length % 100;
	if (j == 0)
	{
		string_buffer_p *ref_string_buffer = string_data->length == 0 ? &global_string_buffer : &string_data->buffer->next;
		if (*ref_string_buffer == NULL)
			*ref_string_buffer = new_string_buffer();
		string_data->buffer = *ref_string_buffer;
	}
	string_data->buffer->buf[j] = ch;
	string_data->length++;
	return TRUE;
}

bool string_data_add_escaped_char(result_p prev, char ch, result_p result)
{
	return string_data_add_normal_char(prev, ch == '0' ? '\0' : ch == 'n' ? '\n' : ch == 'r' ? '\r' : ch, result);
}

bool string_data_add_first_octal(result_p prev, char ch, result_p result)
{
	result_assign(result, prev);
	string_data_p string_data = CAST(string_data_p, result->data);
	string_data->octal_char = (ch - '0') << 6;
	return TRUE;
}

bool string_data_add_second_octal(result_p prev, char ch, result_p result)
{
	result_assign(result, prev);
	string_data_p string_data = CAST(string_data_p, result->data);
	string_data->octal_char |= (ch - '0') << 3;
	return TRUE;
}

bool string_data_add_third_octal(result_p prev, char ch, result_p result)
{
	string_data_p string_data = CAST(string_data_p, prev->data);
	return string_data_add_normal_char(prev, string_data->octal_char | (ch - '0'), result);
}

/*	String tree node structure */

typedef struct string_node_t *string_node_p;
struct string_node_t
{
	node_t _node;
	const char *str;
	size_t length;
};

DEFINE_SUB_BASE_TYPE(string_node_p, node_p)

const char *string_node_type = "string_node_type";

void string_node_print(void *data, ostream_p ostream)
{
	string_node_p string_node = CAST(string_node_p, data);
	ostream_puts(ostream, "\"");
	for (size_t i = 0; i < string_node->length - 1; i++)
		print_single_char(string_node->str[i], '"', ostream);
	ostream_puts(ostream, "\"");
}

bool create_string_tree(const result_p rule_result, void* data, result_p result)
{
	string_data_p string_data = CAST(string_data_p, rule_result->data);
	
	string_node_p string_node = MALLOC(struct string_node_t);
	init_node(&string_node->_node, string_node_type, NULL);
	node_set_pos(&string_node->_node, &string_data->ps);
	char *s = MALLOC_N(string_data->length + 1, char);
	string_node->str = s;
	string_node->length = string_data->length + 1;
	string_buffer_p string_buffer = global_string_buffer;
	int j = 0;
	for (size_t i = 0; i < string_data->length; i++)
	{
		*s++ = string_buffer->buf[j++];
		if (j == 100)
		{
			string_buffer = string_buffer->next;
			j = 0;
		}
	}
	*s = '\0';
	result_assign_ref_counted(result, &string_node->_node._base, string_node_print);
	SET_TYPE(string_node_p, string_node);
	return TRUE;
}
		
/*	String grammar */

void string_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("string")
		RULE
			{ GROUPING
				RULE
					CHAR('"') SET_PS(string_set_pos)
					{ GROUPING
						RULE // Octal character
							CHAR('\\')
							CHARSET(string_data_add_first_octal) ADD_CHAR('0') ADD_CHAR('1')
							CHARSET(string_data_add_second_octal) ADD_RANGE('0','7')
							CHARSET(string_data_add_third_octal) ADD_RANGE('0','7')
						RULE // Escaped character
							CHAR('\\') CHARSET(string_data_add_escaped_char) ADD_CHAR('0') ADD_CHAR('\'') ADD_CHAR('"') ADD_CHAR('\\') ADD_CHAR('n') ADD_CHAR('r')
						RULE // Normal character
							CHARSET(string_data_add_normal_char) ADD_RANGE(' ', 126) REMOVE_CHAR('\\') REMOVE_CHAR('"')
					} SEQ(pass_to_sequence, use_sequence_result, NULL) OPT(0)
					CHAR('"')
			} SEQ(pass_to_sequence, use_sequence_result, NULL) { CHAIN NTF("white_space", 0) }
			END_FUNCTION(create_string_tree)
}

void test_parse_string(non_terminal_dict_p *all_nt, const char *input, const char *str)
{
	ENTER_RESULT_CONTEXT

	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("string", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else
		{
			node_p node = CAST(node_p, result.data);
			if (node->line != 1 && node->column != 1)
				fprintf(stderr, "WARNING: tree node position %d:%d is not 1:1\n", node->line, node->column);
			if (node->type_name != string_node_type)
				fprintf(stderr, "ERROR: tree node is not of type string_node_type\n");
			else
			{
				string_node_p string_node = CAST(string_node_p, node);
				if (strcmp(string_node->str, str) != 0)
					fprintf(stderr, "ERROR: parsed value '%s' from '%s' instead of expected '%s'\n",
					string_node->str, input, str);
				else
					fprintf(stderr, "OK: parsed string \"%s\" from \"%s\"\n", string_node->str, input);
			}
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse string from '%s'\n", input);
	}
	DISP_RESULT(result);
	
	solutions_free(&solutions);

	EXIT_RESULT_CONTEXT
}

void test_string_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_string(all_nt, "\"abc\"", "abc");
	test_parse_string(all_nt, "\"\\0\"", "");
	test_parse_string(all_nt, "\"\\'\"", "\'");
	test_parse_string(all_nt, "\"abc\" /* */ \"def\"", "abcdef");
	test_parse_string(all_nt, "\"\\n\"", "\n");
}

/*
	Int result
	~~~~~~~~~~
	
	For parsing an integer value a single function will used,
	which will implement a co-routine for processing the
	characters.
*/

typedef struct int_data
{
	ref_counted_base_t _base;
	long long int value;
	int state;
	int sign;
	text_pos_t ps;
} *int_data_p;

DEFINE_BASE_TYPE(int_data_p)

void int_data_print(void *data, ostream_p ostream)
{
	int_data_p int_data = CAST(int_data_p, data);
	char buffer[51];
	snprintf(buffer, 50, "%lld", int_data->sign * int_data->value);
	buffer[50] = '\0';
	ostream_puts(ostream, buffer);
}

void int_set_pos(result_p result, text_pos_p ps)
{
	if (result->data != NULL && CAST(int_data_p, result->data)->ps.cur_line == -1)
		CAST(int_data_p, result->data)->ps = *ps;
}

#define INT_DATA_WAIT_NEXT_CHAR(X)  int_data->state = X; return TRUE; L##X:

bool int_data_add_char(result_p prev, char ch, result_p result)
{
	if (prev->data == NULL)
	{
		int_data_p int_data = MALLOC(struct int_data);
		int_data->value = 0;
		int_data->state = 0;
		int_data->sign = 1;
		int_data->_base.release = 0;
		int_data->ps.cur_line = -1;
		result_assign_ref_counted(result, &int_data->_base, int_data_print);
		SET_TYPE(int_data_p, int_data);
	}
	else
		result_assign(result, prev);
	int_data_p int_data = CAST(int_data_p, result->data);
	
	switch (int_data->state)
	{
		case 0: goto L0;
		case 1: goto L1;
		case 2: goto L2;
		case 3: goto L3;
		case 4: goto L4;
		case 5: goto L5;
		case 6: goto L6;
	}
	L0:
	
	if (ch == '-')
	{
		int_data->sign = -1;
		INT_DATA_WAIT_NEXT_CHAR(1)
	}
	if (ch == '0')
	{
		INT_DATA_WAIT_NEXT_CHAR(2)
		if (ch == 'x')
		{
			// Process hexa decimal number
			INT_DATA_WAIT_NEXT_CHAR(3)
			for (;;)
			{
				if ('0' <= ch && ch <= '9')
					int_data->value = 16 * int_data->value + ch - '0';
				else if ('A' <= ch && ch <= 'F')
					int_data->value = 16 * int_data->value + ch + (10 - 'A');
				else if ('a' <= ch && ch <= 'f')
					int_data->value = 16 * int_data->value + ch + (10 - 'a');
				else
					break;
				INT_DATA_WAIT_NEXT_CHAR(4)
			}
		}
		else
		{
			// Process octal number
			while ('0' <= ch && ch <= '7')
			{
				int_data->value = 8 * int_data->value +  ch - '0';
				INT_DATA_WAIT_NEXT_CHAR(5)
			}
		}
	}
	else
	{
		// Process decimal number
		while ('0' <= ch && ch <= '9')
		{
			int_data->value = 10 * int_data->value + ch - '0';
			INT_DATA_WAIT_NEXT_CHAR(6)
		}
	}
	return FALSE;
}

/*	Int tree node structure */

typedef struct int_node_t *int_node_p;
struct int_node_t
{
	node_t _node;
	long long int value;
};

DEFINE_SUB_BASE_TYPE(int_node_p, node_p)

const char *int_node_type = "int_node_type";

void int_node_print(void *data, ostream_p ostream)
{
	int_node_p int_node = (int_node_p)data;
	char buffer[51];
	snprintf(buffer, 50, "ii %lld", int_node->value);
	buffer[50] = '\0';
	ostream_puts(ostream, buffer);
}

bool create_int_tree(const result_p rule_result, void* data, result_p result)
{
	int_data_p int_data = CAST(int_data_p, rule_result->data);
	
	int_node_p int_node = MALLOC(struct int_node_t);
	init_node(&int_node->_node, int_node_type, NULL);
	node_set_pos(&int_node->_node, &int_data->ps);
	int_node->value = int_data->sign * int_data->value;
	result_assign_ref_counted(result, &int_node->_node._base, int_node_print);
	SET_TYPE(int_node_p, int_node);
	return TRUE;
}
		
/*	Int grammar */

void int_grammar(non_terminal_dict_p *all_nt)
{
	HEADER(all_nt)
	
	NT_DEF("int")
		RULE
			CHARF('-', int_data_add_char) OPT(0) SET_PS(int_set_pos)
			{ GROUPING
				RULE // hexadecimal representaion
					CHARF('0', int_data_add_char) SET_PS(int_set_pos) 
					CHARF('x', int_data_add_char)
					CHARSET(int_data_add_char) ADD_RANGE('0','9') ADD_RANGE('A','F') ADD_RANGE('a','f') SEQ(pass_to_sequence, use_sequence_result, NULL)
				RULE // octal representation
					CHARF('0', int_data_add_char) SET_PS(int_set_pos) 
					CHARSET(int_data_add_char) ADD_RANGE('0','7') SEQ(pass_to_sequence, use_sequence_result, NULL) OPT(0)
				RULE // decimal representation
					CHARSET(int_data_add_char) ADD_RANGE('1','9') SET_PS(int_set_pos) 
					CHARSET(int_data_add_char) ADD_RANGE('0','9') SEQ(pass_to_sequence, use_sequence_result, NULL) OPT(0)
 			}
			CHAR('U') OPT(0)
			CHAR('L') OPT(0)
			CHAR('L') OPT(0)
			END_FUNCTION(create_int_tree)
}

void test_parse_int(non_terminal_dict_p *all_nt, const char *input, long long int value)
{
	ENTER_RESULT_CONTEXT

	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("int", all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else
		{
			node_p node = CAST(node_p, result.data);
			if (node->line != 1 && node->column != 1)
				fprintf(stderr, "WARNING: tree node position %d:%d is not 1:1\n", node->line, node->column);
			if (node->type_name != int_node_type)
				fprintf(stderr, "ERROR: tree node is not of type int_node_type\n");
			else
			{
				int_node_p int_node = (int_node_p)node;
				if (int_node->value != value)
					fprintf(stderr, "ERROR: parsed value %lld from '%s' instead of expected %lld\n",
					int_node->value, input, value);
				else
					fprintf(stderr, "OK: parsed integer %lld from \"%s\"\n", int_node->value, input);
			}
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse int from '%s'\n", input);
	}
	DISP_RESULT(result);
	
	solutions_free(&solutions);

	EXIT_RESULT_CONTEXT
}

void test_int_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_int(all_nt, "0", 0);
	test_parse_int(all_nt, "1", 1);
	test_parse_int(all_nt, "-1", -1);
	test_parse_int(all_nt, "077", 077);
	test_parse_int(all_nt, "0xAbc", 0xAbc);
	test_parse_int(all_nt, "1234L", 1234);
	test_parse_int(all_nt, "-23", -23);
	test_parse_int(all_nt, "46464664", 46464664);
}


bool equal_string(result_p result, const void *argument)
{
	const char *keyword_name = (const char*)argument;
	return    result->data != NULL
		   && CAST(node_p, result->data)->type_name == ident_node_type
		   && strcmp(CAST(ident_node_p, result->data)->name, keyword_name) == 0;
}

bool not_a_keyword(result_p result, const void *argument)
{
	ident_node_p ident = CAST(ident_node_p, result->data);
	return !ident->is_keyword;
}


const char * const list_type = "list";

bool add_seq_as_list(result_p prev, result_p seq, void *data, result_p result)
{
	prev_child_p prev_child = CAST(prev_child_p, prev->data);
	if (prev_child != NULL)
		ref_counted_base_inc(prev_child);
	prev_child_p new_prev_child = malloc_prev_child();
	new_prev_child->prev = prev_child;
	tree_param_p tree_param = (tree_param_p)data;
	tree_p list = make_tree_with_children(tree_param, CAST(prev_child_p, seq->data));
	result_assign_ref_counted(&new_prev_child->child, &list->_node._base, tree_print);
	SET_TYPE(tree_p, list);
	result_assign_ref_counted(result, &new_prev_child->_base, NULL);
	SET_TYPE(prev_child_p, new_prev_child);
	return TRUE;
}

#define ADD_CHILD element->add_function = add_child;
#define NT(S) NTF(S, add_child)
#define NTP(S) NTF(S, take_child)
#define WS NTF("white_space", 0)
#define PASS rules->end_function = pass_tree;
#define TREE(N,F) rules->end_function = make_tree; { static tree_param_t tp = { N, F }; rules->end_function_data = &tp; }
#define TREE_TP(N) rules->end_function = make_tree; rules->end_function_data = &N##_tp;
#define TREE_FROM_LIST(N,F) rules->end_function = make_tree_from_list; { static tree_param_t tp = { N, F }; rules->end_function_data = &tp; }
#define TREE_FROM_LIST_TP(N) rules->end_function = make_tree_from_list; rules->end_function_data = &N##_tp;
#define KEYWORD(K) NTF("ident", 0) element->condition = equal_string; element->condition_argument = ident_string(K); *keyword_state = 1; WS
#define OPTN OPT(0)
#define IDENT NTF("ident", add_child) element->condition = not_a_keyword; WS
#define IDENT_OPT NTF("ident", add_child) element->condition = not_a_keyword; OPTN WS
#define SEQL(F) { static tree_param_t tp = { list_type, F }; SEQ(0, add_seq_as_list, &tp) }
#define REC_RULEC REC_RULE(rec_add_child);
#define CHAR_WS(C) CHAR(C) WS

#define TREE_PARAM(N,F) tree_param_t N##_tp = { #N, F };

TREE_PARAM(declaration, "%*%*")
TREE_PARAM(list, "")
TREE_PARAM(decl, "%*;\n")
TREE_PARAM(decl_init, "%*%*")
TREE_PARAM(semi, "%*;")
TREE_PARAM(assignment, "%* %* %*")
TREE_PARAM(ass, "=")
TREE_PARAM(call, "%*(%*)")

void c_grammar(non_terminal_dict_p *all_nt)
{
	white_space_grammar(all_nt);
	ident_grammar(all_nt);
	char_grammar(all_nt);
	string_grammar(all_nt);
	int_grammar(all_nt);
	
	HEADER(all_nt)
	
	NT_DEF("primary_expr")
		RULE IDENT PASS
		RULE NTP("int") WS
		RULE NTP("double") WS
		RULE NTP("char") WS
		RULE NTP("string") WS
		RULE CHAR_WS('(') NT("expr") CHAR_WS(')') TREE("brackets","(%*)")

	NT_DEF("postfix_expr")
		RULE NTP("primary_expr")
		REC_RULEC CHAR_WS('[') NT("expr") CHAR_WS(']') TREE("arrayexp", "%*[%*]")
		REC_RULEC CHAR_WS('(') NT("assignment_expr") SEQL(", ") { CHAIN CHAR_WS(',') } OPTN CHAR_WS(')') TREE_TP(call)
		REC_RULEC CHAR_WS('.') IDENT TREE("field", "%*.%*")
		REC_RULEC CHAR('-') CHAR_WS('>') IDENT TREE("fieldderef", "%*->%*")
		REC_RULEC CHAR('+') CHAR_WS('+') TREE("post_inc", "%*++")
		REC_RULEC CHAR('-') CHAR_WS('-') TREE("post_dec", "%*--")

	NT_DEF("unary_expr")
		RULE CHAR('+') CHAR_WS('+') NT("unary_expr") TREE("pre_inc", "++%*")
		RULE CHAR('-') CHAR_WS('-') NT("unary_expr") TREE("pre_dec", "--%*")
		RULE CHAR_WS('&') NT("cast_expr") TREE("address_of", "&%*")
		RULE CHAR_WS('*') NT("cast_expr") TREE("deref", "*%*")
		RULE CHAR_WS('+') NT("cast_expr") TREE("plus", "+%*")
		RULE CHAR_WS('-') NT("cast_expr") TREE("min", "-%*")
		RULE CHAR_WS('~') NT("cast_expr") TREE("invert", "~%*")
		RULE CHAR_WS('!') NT("cast_expr") TREE("not", "!%*")
		RULE KEYWORD("sizeof") CHAR_WS('(') NT("sizeof_type") CHAR_WS(')') TREE("sizeof", "sizeiof(%*)")
		RULE KEYWORD("sizeof") NT("unary_expr") TREE("sizeof_expr", "sizeof(%*)")
		RULE NTP("postfix_expr")

	NT_DEF("sizeof_type")
		RULE KEYWORD("char") TREE("char", "char")
		RULE KEYWORD("short") TREE("short", "short")
		RULE KEYWORD("int") TREE("int", "int")
		RULE KEYWORD("long") TREE("long", "long")
		RULE KEYWORD("signed") NT("sizeof_type") TREE("signed", "signed")
		RULE KEYWORD("unsigned") NT("sizeof_type") TREE("unsigned", "unsigned")
		RULE KEYWORD("float") TREE("float", "float")
		RULE KEYWORD("double") NT("sizeof_type") OPTN TREE("double", "double")
		RULE KEYWORD("const") NT("sizeof_type") TREE("const","const")
		RULE KEYWORD("volatile") NT("sizeof_type") TREE("volatile","volatile")
		RULE KEYWORD("void") TREE("void","void")
		RULE KEYWORD("struct") IDENT TREE("structdecl","struct %*")
		RULE IDENT PASS
		REC_RULEC WS CHAR_WS('*') TREE("pointdecl", "%**")

	NT_DEF("cast_expr")
		RULE CHAR_WS('(') NT("abstract_declaration") CHAR_WS(')') NT("cast_expr") TREE("cast","(%*)")
		RULE NTP("unary_expr")

	NT_DEF("l_expr1")
		RULE NTP("cast_expr")
		REC_RULEC WS CHAR_WS('*') NT("cast_expr") TREE("times","%* * %*")
		REC_RULEC WS CHAR_WS('/') NT("cast_expr") TREE("div","%* / %*")
		REC_RULEC WS CHAR_WS('%') NT("cast_expr") TREE("mod","%* %% %*")

	NT_DEF("l_expr2")
		RULE NTP("l_expr1")
		REC_RULEC WS CHAR_WS('+') NT("l_expr1") TREE("add","%* + %*")
		REC_RULEC WS CHAR_WS('-') NT("l_expr1") TREE("sub","%* - %*")

	NT_DEF("l_expr3")
		RULE NTP("l_expr2")
		REC_RULEC WS CHAR('<') CHAR_WS('<') NT("l_expr2") TREE("ls","%* << %*")
		REC_RULEC WS CHAR('>') CHAR_WS('>') NT("l_expr2") TREE("rs","%* >> %*")

	NT_DEF("l_expr4")
		RULE NTP("l_expr3")
		REC_RULEC WS CHAR('<') CHAR_WS('=') NT("l_expr3") TREE("le","%* <= %*")
		REC_RULEC WS CHAR('>') CHAR_WS('=') NT("l_expr3") TREE("ge","%* >= %*")
		REC_RULEC WS CHAR_WS('<') NT("l_expr3") TREE("lt","%* < %*")
		REC_RULEC WS CHAR_WS('>') NT("l_expr3") TREE("gt","%* > %*")
		REC_RULEC WS CHAR('=') CHAR_WS('=') NT("l_expr3") TREE("eq","%* == %*")
		REC_RULEC WS CHAR('!') CHAR_WS('=') NT("l_expr3") TREE("ne","%* != %*")

	NT_DEF("l_expr5")
		RULE NTP("l_expr4")
		REC_RULEC WS CHAR_WS('^') NT("l_expr4") TREE("bexor","%* ^ %*")

	NT_DEF("l_expr6")
		RULE NTP("l_expr5")
		REC_RULEC WS CHAR_WS('&') NT("l_expr5") TREE("land","%* & %*")

	NT_DEF("l_expr7")
		RULE NTP("l_expr6")
		REC_RULEC WS CHAR_WS('|') NT("l_expr6") TREE("lor","%* | %*")

	NT_DEF("l_expr8")
		RULE NTP("l_expr7")
		REC_RULEC WS CHAR('&') CHAR_WS('&') NT("l_expr7") TREE("and","%* && %*")

	NT_DEF("l_expr9")
		RULE NTP("l_expr8")
		REC_RULEC WS CHAR('|') CHAR_WS('|') NT("l_expr8") TREE("or","%* || %*")

	NT_DEF("conditional_expr")
		RULE NT("l_expr9") WS CHAR_WS('?') NT("l_expr9") WS CHAR_WS(':') NT("conditional_expr") TREE("if_expr","%* ? %* : %*")
		RULE NTP("l_expr9")

	NT_DEF("assignment_expr")
		RULE NT("unary_expr") WS NT("assignment_operator") WS NT("assignment_expr") TREE_TP(assignment)
		RULE NTP("conditional_expr")

	NT_DEF("assignment_operator")
		RULE CHAR_WS('=') TREE_TP(ass)
		RULE CHAR('*') CHAR_WS('=') TREE("times_ass", "*=")
		RULE CHAR('/') CHAR_WS('=') TREE("div_ass", "/=")
		RULE CHAR('%') CHAR_WS('=') TREE("mod_ass", "%%=")
		RULE CHAR('+') CHAR_WS('=') TREE("add_ass", "+=")
		RULE CHAR('-') CHAR_WS('=') TREE("sub_ass", "-=")
		RULE CHAR('<') CHAR('<') CHAR_WS('=') TREE("sl_ass", "<<=")
		RULE CHAR('>') CHAR('>') CHAR_WS('=') TREE("sr_ass", ">>=")
		RULE CHAR('&') CHAR_WS('=') TREE("and_ass", "&=")
		RULE CHAR('|') CHAR_WS('=') TREE("or_ass", "!=")
		RULE CHAR('^') CHAR_WS('=') TREE("exor_ass", "^=")

	NT_DEF("expr")
		RULE NT("assignment_expr") /*SEQL(", ") { CHAIN CHAR_WS(',') }*/ PASS

	NT_DEF("constant_expr")
		RULE NTP("conditional_expr")

	NT_DEF("declaration")
		RULE
		{ GROUPING
			RULE NT("storage_class_specifier") PASS
			RULE NT("simple_type_specifier") PASS
		} SEQL("") OPTN ADD_CHILD AVOID
		{ GROUPING
			RULE
			{ GROUPING
				RULE NT("declarator")
				{ GROUPING
					RULE WS CHAR_WS('=') NT("initializer") TREE("init", " = %*")
				} OPTN ADD_CHILD TREE_TP(decl_init)
			} /*SEQL(", ")*/ ADD_CHILD /*{ CHAIN CHAR_WS(',') }*/ CHAR_WS(';') TREE_FROM_LIST_TP(decl)
		} ADD_CHILD TREE_TP(declaration)
		RULE
		{ GROUPING
			RULE NT("storage_class_specifier") PASS
			RULE NT("type_specifier") PASS
		} SEQL("") OPTN ADD_CHILD AVOID
		{ GROUPING
			RULE NT("func_declarator") CHAR_WS('(')
			{ GROUPING
				RULE NTP("parameter_declaration_list") OPTN
				RULE KEYWORD("void") TREE("void","void")
			} ADD_CHILD
			CHAR_WS(')')
			{ GROUPING
				RULE CHAR_WS(';') TREE("forward", ";\n")
				RULE CHAR_WS('{') NT("decl_or_stat") CHAR_WS('}') TREE("body","{\n%>%*%<\n}\n\n")
			} ADD_CHILD TREE("new_style", "%*(%*)\n%*") WS
			RULE NT("func_declarator") CHAR_WS('(') NT("ident_list") OPTN CHAR_WS(')') NT("declaration") SEQL("") OPTN CHAR_WS('{') NT("decl_or_stat") CHAR_WS('}') TREE("old_style","%*%*{\n%*\n}\n")
			RULE
			{ GROUPING
				RULE NT("declarator")
				{ GROUPING
					RULE WS CHAR_WS('=') NT("initializer") TREE("init", " = %*")
				} OPTN ADD_CHILD TREE_TP(decl_init)
			} /*SEQL(", ")*/ OPTN ADD_CHILD /*{ CHAIN CHAR_WS(',') }*/ CHAR_WS(';') TREE_FROM_LIST_TP(decl)
		} ADD_CHILD TREE_TP(declaration)

	NT_DEF("var_declaration")
		RULE
		{ GROUPING
			RULE NT("storage_class_specifier") PASS
			RULE NT("type_specifier") PASS
		} SEQL("") OPTN ADD_CHILD AVOID
		{ GROUPING
			RULE
			{ GROUPING
				RULE NT("declarator")
				{ GROUPING
					RULE WS CHAR_WS('=') NT("initializer") TREE("init", " = %*")
				} OPTN ADD_CHILD TREE_TP(decl_init)
			} /*SEQL(", ")*/ OPTN ADD_CHILD /*{ CHAIN CHAR_WS(',') }*/ CHAR_WS(';') TREE_TP/*_FROM_LIST_TP*/(decl)
		} ADD_CHILD TREE_TP(declaration)

	NT_DEF("storage_class_specifier")
		RULE KEYWORD("typedef") TREE("typedef","typedef")
		RULE KEYWORD("extern") TREE("extern","extern")
		RULE KEYWORD("inline") TREE("inline","inline")
		RULE KEYWORD("static") TREE("static","static")
		RULE KEYWORD("auto") TREE("auto","auto")
		RULE KEYWORD("task") TREE("task","task")
		RULE KEYWORD("register") TREE("register","register")

	NT_DEF("simple_type_specifier")
		RULE KEYWORD("char") TREE("char","char")
		RULE KEYWORD("short") TREE("short","short")
		RULE KEYWORD("int") TREE("int","int")
		RULE KEYWORD("long") TREE("long","long")
		RULE KEYWORD("signed") TREE("signed","signed")
		RULE KEYWORD("unsigned") TREE("unsigned","unsigned")
		RULE KEYWORD("float") TREE("float","float")
		RULE KEYWORD("double") TREE("double","double")
		RULE KEYWORD("const") TREE("const","const")
		RULE KEYWORD("volatile") TREE("volatile","volatile")
		RULE KEYWORD("void") TREE("void","void")
		RULE IDENT PASS

	NT_DEF("type_specifier")
		RULE KEYWORD("char") TREE("char","char")
		RULE KEYWORD("short") TREE("short","short")
		RULE KEYWORD("int") TREE("int","int")
		RULE KEYWORD("long") TREE("long","long")
		RULE KEYWORD("signed") TREE("signed","signed")
		RULE KEYWORD("unsigned") TREE("unsigned","unsigned")
		RULE KEYWORD("float") TREE("float","float")
		RULE KEYWORD("double") TREE("double","double")
		RULE KEYWORD("const") TREE("const","const")
		RULE KEYWORD("volatile") TREE("volatile","volatile")
		RULE KEYWORD("void") TREE("void","void")
		RULE NT("struct_or_union_specifier")
		RULE NT("enum_specifier")
		RULE IDENT PASS

	NT_DEF("struct_or_union_specifier")
		RULE KEYWORD("struct") IDENT_OPT
		{ GROUPING
			RULE CHAR_WS('{')
			{ GROUPING
				RULE NTP("struct_declaration_or_anon")
			} SEQL("") ADD_CHILD
			CHAR_WS('}') PASS
		} OPTN ADD_CHILD TREE("struct","struct %*{\n%*\n}")
		RULE KEYWORD("union") IDENT_OPT
		{ GROUPING
			RULE CHAR_WS('{')
			{ GROUPING
				RULE NTP("struct_declaration_or_anon")
			} SEQL("") ADD_CHILD
			CHAR_WS('}') PASS
		} OPTN ADD_CHILD TREE("union","union %*{\n%*\n}")

	NT_DEF("struct_declaration_or_anon")
		RULE NT("struct_or_union_specifier") CHAR_WS(';') TREE_FROM_LIST_TP(semi)
		RULE NTP("struct_declaration")

	NT_DEF("struct_declaration")
		RULE NT("type_specifier") NT("struct_declaration") TREE("type","%*%*")
		RULE NT("struct_declarator") SEQL(", ") { CHAIN CHAR_WS(',') } CHAR_WS(';') TREE("strdec","%*%*;")

	NT_DEF("struct_declarator")
		RULE NT("declarator")
		{ GROUPING
			RULE CHAR_WS(':') NT("constant_expr") TREE("fieldsize"," : &*")
		} OPTN ADD_CHILD TREE("record_field","%*%*")

	NT_DEF("enum_specifier")
		RULE KEYWORD("enum") IDENT_OPT CHAR_WS('{') NT("enumerator") SEQL(", ") { CHAIN CHAR_WS(',') } CHAR_WS('}') TREE("enum","enum %*{\n%*\n}")

	NT_DEF("enumerator")
		RULE IDENT
		{ GROUPING
			RULE CHAR_WS('=') NTP("constant_expr") TREE("value"," = &*")
		} OPTN ADD_CHILD TREE("enumerator","%s%s")

	NT_DEF("func_declarator")
		RULE CHAR_WS('*')
		{ GROUPING
			RULE KEYWORD("const") TREE("const","const")
		} OPTN ADD_CHILD NT("func_declarator") TREE("pointdecl","*%*")
		RULE CHAR_WS('(') NT("func_declarator") CHAR_WS(')')
		RULE IDENT PASS

	NT_DEF("declarator")
		RULE CHAR_WS('*')
		{ GROUPING
			RULE KEYWORD("const") TREE("const","const")
		} OPTN ADD_CHILD NT("declarator") TREE("pointdecl","*")
		RULE CHAR_WS('(') NT("declarator") CHAR_WS(')') TREE("brackets","(%*)")
		RULE WS IDENT PASS
		REC_RULEC CHAR_WS('[') NT("constant_expr") OPTN CHAR_WS(']') TREE("array","%*[%*]")
		REC_RULEC CHAR_WS('(') NT("abstract_declaration_list") OPTN CHAR_WS(')') TREE("function","%*(%*)")

	NT_DEF("abstract_declaration_list")
		RULE
			NT("abstract_declaration") SEQL(", ") BACK_TRACKING { CHAIN CHAR_WS(',') }
			{ GROUPING
				RULE CHAR_WS(',') CHAR('.') CHAR('.') CHAR_WS('.') TREE("varargs",", ..")
			} OPTN ADD_CHILD TREE("abstract_declaration_list","%*%*")

	NT_DEF("parameter_declaration_list")
		RULE
			NT("parameter_declaration") SEQL(", ") BACK_TRACKING { CHAIN CHAR_WS(',') }
			{ GROUPING
				RULE CHAR_WS(',') CHAR('.') CHAR('.') CHAR_WS('.') TREE("varargs",", ..")
			} OPTN ADD_CHILD TREE("parameter_declaration_list","%*%*")

	NT_DEF("ident_list")
		RULE IDENT
		{ GROUPING
			RULE CHAR_WS(',')
			{ GROUPING
				RULE CHAR('.') CHAR('.') CHAR_WS('.') TREE("varargs",", ..")
				RULE NT("ident_list") TREE("ident_list","%*%*")
			}
		} OPTN ADD_CHILD TREE("ident_list","%*%*")

	NT_DEF("parameter_declaration")
		RULE NT("type_specifier") NT("parameter_declaration") TREE("type","%*%*")
		RULE NTP("declarator")
		RULE NTP("abstract_declarator")

	NT_DEF("abstract_declaration")
		RULE NT("type_specifier") NT("parameter_declaration") TREE("type","%*%*")
		RULE NTP("abstract_declarator")

	NT_DEF("abstract_declarator")
		RULE CHAR_WS('*')
		{ GROUPING
			RULE KEYWORD("const") TREE("const","cont")
		} OPTN ADD_CHILD NT("abstract_declarator") TREE("abs_pointdecl","*%*%*")
		RULE CHAR_WS('(') NT("abstract_declarator") CHAR_WS(')') TREE("abs_brackets","(%*)")
		RULE
		REC_RULEC CHAR_WS('[') NT("constant_expr") OPTN CHAR_WS(']') TREE("abs_array","[%*]")
		REC_RULEC CHAR_WS('(') NT("parameter_declaration_list") CHAR_WS(')') TREE("abs_func","(%*)")

	NT_DEF("initializer")
		RULE NTP("assignment_expr")
		RULE CHAR_WS('{') NT("initializer") SEQL(", ") { CHAIN CHAR_WS(',') } CHAR(',') OPTN WS CHAR_WS('}') TREE("initializer","%*{%*}")

	NT_DEF("decl_or_stat")
		RULE 
		{ GROUPING
			RULE NT("statement") PASS
			RULE NT("var_declaration") PASS
		} SEQL("") OPTN ADD_CHILD PASS

	NT_DEF("statement")
		RULE
		{ GROUPING
			RULE IDENT
			RULE KEYWORD("case") NT("constant_expr") TREE("case","case %*")
			RULE KEYWORD("default") TREE("default", "default")
		} ADD_CHILD CHAR_WS(':') NT("statement") TREE("label","%*:")
		RULE CHAR_WS('{') NT("decl_or_stat") CHAR_WS('}') TREE_FROM_LIST("statements","%<{\n%>%*\n%<}%>")
		RULE NT("expr") OPTN CHAR_WS(';') TREE_FROM_LIST_TP(semi)
		RULE KEYWORD("if") WS CHAR_WS('(') NT("expr") CHAR_WS(')') NT("statement")
		{ GROUPING
			RULE KEYWORD("else") NT("statement") TREE("else","\nelse\n%>%*%<")
		} OPTN ADD_CHILD TREE("if","if (%*)\n%>%*%<%*")
		RULE KEYWORD("switch") WS CHAR_WS('(') NT("expr") CHAR_WS(')') NT("statement") TREE("switch", "switch (%*)%*")
		RULE KEYWORD("while") WS CHAR_WS('(') NT("expr") CHAR_WS(')') NT("statement") TREE("while", "while (%*)%*")
		RULE KEYWORD("do") NT("statement") KEYWORD("while") WS CHAR_WS('(') NT("expr") CHAR_WS(')') CHAR_WS(';') TREE("do", "do%>%*%<\nwhile (%*);")
		RULE KEYWORD("for") WS CHAR_WS('(') NT("expr") OPTN CHAR_WS(';')
		{ GROUPING
			RULE WS NTP("expr")
		} OPTN ADD_CHILD CHAR_WS(';')
		{ GROUPING
			RULE WS NTP("expr")
		} OPTN ADD_CHILD CHAR_WS(')') NT("statement") TREE("for", "for (%*; %*; %*)\n%>%*%<")
		RULE KEYWORD("goto") IDENT CHAR_WS(';') TREE("goto", "goto %*;")
		RULE KEYWORD("continue") CHAR_WS(';') TREE("cont", "continue;")
		RULE KEYWORD("break") CHAR_WS(';') TREE("break", "break;")
		RULE KEYWORD("return") NT("expr") OPTN CHAR_WS(';') TREE("ret", "return%*;")
		RULE KEYWORD("queue") WS KEYWORD("for") WS NT("ident") WS NT("statement") TREE("queuefor","queue for %*\n%>%*%<")
		RULE KEYWORD("poll") WS NT("statement")
		{ GROUPING
			RULE KEYWORD("at") WS KEYWORD("most") WS CHAR_WS('(') NT("expr") CHAR_WS(')') NT("statement") TREE("atmost","\nat most (%*)\n%>%*%<\n")
		} OPTN ADD_CHILD TREE("poll","poll\n%>%*%<%*")
		RULE KEYWORD("timer") WS NT("ident") WS CHAR_WS(';') TREE("timer","timer %*;")
		RULE KEYWORD("every") WS CHAR_WS('(') NT("expr") CHAR_WS(')') KEYWORD("start") WS NT("ident") WS CHAR_WS(';') TREE("every", "every (%*) start %*;")

	NT_DEF("root")
		RULE
		WS
		{ GROUPING
			RULE NT("declaration")
		} SEQL("") OPTN END PASS
}


/*
	Fixed string output stream
	~~~~~~~~~~~~~~~~~~~~~~~~~~
*/

typedef struct fixed_string_ostream fixed_string_ostream_t, *fixed_string_ostream_p;
struct fixed_string_ostream
{
	ostream_t ostream;
	char *buffer;
	unsigned int i;
	unsigned int len;
};

void fixed_string_ostream_put(ostream_p ostream, char ch)
{
	if (((fixed_string_ostream_p)ostream)->i < ((fixed_string_ostream_p)ostream)->len)
		((fixed_string_ostream_p)ostream)->buffer[((fixed_string_ostream_p)ostream)->i++] = ch;
}

void fixed_string_ostream_init(fixed_string_ostream_p ostream, char *buffer, unsigned int len)
{
	ostream->ostream.put = fixed_string_ostream_put;
	ostream->buffer = buffer;
	ostream->i = 0;
	ostream->len = len - 1;
}

void fixed_string_ostream_finish(fixed_string_ostream_p ostream)
{
	ostream->buffer[ostream->i] = '\0';
}




void test_parse_grammar(non_terminal_dict_p *all_nt, const char *nt, const char *input, const char *exp_output)
{
	ENTER_RESULT_CONTEXT

	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt(nt, all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
			fprintf(stderr, "ERROR: parsing '%s' did not return result\n", input);
		else
		{
			char output[200];
			fixed_string_ostream_t fixed_string_ostream;
			fixed_string_ostream_init(&fixed_string_ostream, output, 200);
			result_print(&result, &fixed_string_ostream.ostream);
			fixed_string_ostream_finish(&fixed_string_ostream);
			if (strcmp(output, exp_output) != 0)
			{
				fprintf(stderr, "ERROR: parsed value '%s' from '%s' instead of expected '%s'\n",
						output, input, exp_output);
			}
			else
				fprintf(stderr, "OK: parsed '%s' to '%s'\n", input, output);
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse ident from '%s'\n", input);
	}
	DISP_RESULT(result);
	
	solutions_free(&solutions);

	EXIT_RESULT_CONTEXT
}

void test_c_grammar(non_terminal_dict_p *all_nt)
{
	test_parse_grammar(all_nt, "expr", "a", "list(a)");
	test_parse_grammar(all_nt, "expr", "a*b", "list(times(a,b))");
}

/*
	File output stream
	~~~~~~~~~~~~~~~~~~
*/

typedef struct file_ostream file_ostream_t, *file_ostream_p;
struct file_ostream
{
	ostream_t ostream;
	FILE *f;
};

void file_ostream_put(ostream_p ostream, char ch)
{
	if (((file_ostream_p)ostream)->f != NULL)
		fputc(ch, ((file_ostream_p)ostream)->f);
}

void file_ostream_init(file_ostream_p ostream, FILE *f)
{
	ostream->ostream.put = file_ostream_put;
	ostream->f = f;
}

/*
	Expect
*/

#define MAX_EXP_SYM 200

struct nt_stack
{
	const char *name;
	unsigned long int ref_count;
	text_pos_t pos;
	nt_stack_p parent;
};
nt_stack_p nt_stack_allocated = NULL;

nt_stack_p nt_stack_push(const char *name, parser_p parser)
{
	nt_stack_p child;
	if (nt_stack_allocated != NULL)
	{
		child = nt_stack_allocated;
		nt_stack_allocated = child->parent;
	}
	else
		child = MALLOC(struct nt_stack);
	child->name = name;
	child->ref_count = 1;
	child->pos = parser->text_buffer->pos;
	child->parent = parser->nt_stack;
	if (parser->nt_stack != 0)
		parser->nt_stack->ref_count++;
	//DEBUG_TAB; DEBUG_P1("push %s\n", child->name);
	return child;
}

void nt_stack_dispose(nt_stack_p nt_stack)
{
	while (nt_stack != NULL && --nt_stack->ref_count == 0)
	{
		nt_stack_p parent = nt_stack->parent;
		nt_stack->parent = nt_stack_allocated;
		nt_stack_allocated = nt_stack;
		nt_stack = parent;
	}
}

nt_stack_p nt_stack_pop(nt_stack_p cur)
{
	//DEBUG_TAB; DEBUG_P1("pop %s\n", cur == NULL ? "<NULL>" : cur->name);
	nt_stack_p parent = cur->parent;
	nt_stack_dispose(cur);
	return parent;
}

text_pos_t highest_pos;
typedef struct
{
	nt_stack_p nt_stack;
	element_p element;
} expect_t;
expect_t expected[MAX_EXP_SYM];
int nr_expected;

void init_expected()
{
	highest_pos.pos = 0;
	nr_expected = 0;
}

void expect_element(parser_p parser, element_p element)
{
	if (parser->text_buffer->pos.pos < highest_pos.pos) return;
	
	if (parser->text_buffer->pos.pos > highest_pos.pos)
	{
		highest_pos = parser->text_buffer->pos;
		//for (int i = 0; i < nr_expected; i++)
		//	nt_stack_dispose(expected[i].nt_stack);
		nr_expected = 0;
	}
	for (int i = 0; i < nr_expected; i++)
		if (expected[i].nt_stack == parser->nt_stack && expected[i].element == element)
			return;
	if (nr_expected < MAX_EXP_SYM)
	{
		parser->nt_stack->ref_count++;
		expected[nr_expected].nt_stack = parser->nt_stack;
		expected[nr_expected].element = element;
		nr_expected++;
	}
}



void print_expected(FILE *fout)
{
	fprintf(fout, "Expect at %d.%d:\n", highest_pos.cur_line, highest_pos.cur_column);
	for (int i = 0; i < nr_expected; i++)
	{
		element_p element = expected[i].element;
		fprintf(fout, "- expect ");
		element_print(fout, element);
		for (nt_stack_p nt_stack = expected[i].nt_stack; nt_stack != NULL; nt_stack = nt_stack->parent)
			fprintf(fout, " in %s at %d.%d", nt_stack->name, nt_stack->pos.cur_line, nt_stack->pos.cur_column);
		fprintf(fout, "\n");
	}
}

int indent = 0;
bool start_line = FALSE;
bool need_sp = FALSE;

void unparse_nl(ostream_p ostream)
{
	if (start_line)
	{
		ostream_put(ostream, '\n');
		for (int i = 0; i < indent; i++)
			ostream_puts(ostream, "    ");
		start_line = FALSE;
		need_sp = FALSE;
	}
}

void unparse(result_p result, ostream_p ostream)
{
	if (result->data == NULL)
		; //ostream_puts(ostream, "[NULL]");
	else
	{
		node_p node = CAST(node_p, result->data);
		if (node->type_name == tree_node_type)
		{
			tree_p tree = (tree_p)node;
			if (tree->tree_param == NULL)
				ostream_puts(ostream, "[tree_param NULL]");
			else if (tree->tree_param->name == list_type)
			{
				for (int i = 0; i < tree->nr_children; i++)
				{
					if (i > 0 && tree->tree_param->fmt[0] != '\0')
					{
						ostream_puts(ostream, tree->tree_param->fmt);
						need_sp = FALSE;
					}
					unparse(&tree->children[i], ostream);
				}
			}
			else
			{
				int i = 0;
				bool is_alphanum = FALSE;
				for (const char *s = tree->tree_param->fmt; *s != '\0'; s++)
					if (*s == '%')
					{
						if (s[1] == '*')
						{
							if (is_alphanum)
							{
								need_sp = TRUE;
								is_alphanum = FALSE;
							}
							if (i < tree->nr_children)
								unparse(&tree->children[i++], ostream);
							else
							{
								ostream_puts(ostream, "(ERR1:");
								ostream_puts(ostream, tree->tree_param->name);
								ostream_puts(ostream, " ");
								ostream_puts(ostream, tree->tree_param->fmt);
								ostream_puts(ostream, ")");
							}
							s++;
						}
						else if (s[1] == '%')
						{
							ostream_put(ostream, '%');
							s++;
						}
						else if (s[1] == '<')
						{
							//ostream_puts(ostream, "[-]");
							indent--;
							s++;
						}
						else if (s[1] == '>')
						{
							//ostream_puts(ostream, "[+]");
							indent++;
							s++;
						}
						else
						{
							ostream_puts(ostream, "[ERR3:");
							ostream_put(ostream, s[1]);
							ostream_put(ostream, ']');
						}
					}
					else if (*s == '\n')
					{
						if (start_line)
							ostream_put(ostream, '\n');
						start_line = TRUE;
						need_sp = FALSE;
						is_alphanum = FALSE;
					}
					else
					{
						unparse_nl(ostream);
						is_alphanum = ('a' <= *s && *s <= 'z') || ('A' <= *s && *s <= 'Z') || ('0' <= *s && *s <= '9') || *s == '_';
						if (need_sp && is_alphanum)
							ostream_put(ostream, ' ');
						ostream_put(ostream, *s);
						need_sp = FALSE;
					}
				if (is_alphanum)
					need_sp = TRUE;
				if (i < tree->nr_children)
				{
					ostream_puts(ostream, "(ERR2:");
					ostream_puts(ostream, tree->tree_param->name);
					ostream_puts(ostream, " ");
					ostream_puts(ostream, tree->tree_param->fmt);
					ostream_puts(ostream, ")");
				}
			}
		}
		else if (result->print != NULL)
		{
			unparse_nl(ostream);
			if (need_sp)
				ostream_put(ostream, ' ');
			result->print(result->data, ostream);
			need_sp = TRUE;
		}
		else
		{
			ostream_puts(ostream, "(type_name:");
			ostream_puts(ostream, node->type_name);
			ostream_puts(ostream, ")");
		}
			
	//if (result->print == 0 || result->data == NULL)
	//	ostream_puts(ostream, "<>");
	}
}



typedef struct result_list *result_list_p;
struct result_list
{
	ref_counted_base_t _base;
	result_t value;
	result_t next;
};

DEFINE_BASE_TYPE(result_list_p)

result_list_p old_result_lists = NULL;

void result_list_release(void *data)
{
	result_list_p result_list = CAST(result_list_p, data);
	RESULT_RELEASE(&result_list->value);
	RESULT_RELEASE(&result_list->next);
	*(result_list_p*)result_list = old_result_lists;
	old_result_lists = result_list;
}

void result_list_init(result_list_p result_list)
{
	result_list->_base.cnt = 1;
	result_list->_base.release = result_list_release;
	RESULT_INIT(&result_list->value);
	RESULT_INIT(&result_list->next);
}

result_list_p malloc_result_list(void)
{
	result_list_p result_list;
	if (old_result_lists)
	{   result_list = old_result_lists;
		old_result_lists = *(result_list_p*)old_result_lists;
	}
	else
		result_list = MALLOC(struct result_list);
	result_list_init(result_list);
	return result_list;
}

void result_list_print(void *data, ostream_p ostream)
{
	result_list_p result_list = CAST(result_list_p, data);
	if (result_list != NULL)
	{
		if (result_list->value.data == NULL || result_list->value.print == NULL)
			ostream_puts(ostream, "NULL");
		else
			result_list->value.print(result_list->value.data, ostream);
		if (result_list->next.data == NULL || result_list->next.print == NULL)
			ostream_puts(ostream, "NULL");
		else
		{
			ostream_puts(ostream, ",\n");
			result_list->next.print(result_list->next.data, ostream);
		}
	}
}

void make_result_list(const result_p result, result_p value, result_p next)
{
	result_list_p result_list = malloc_result_list();
	result_assign(&result_list->value, value);
	result_assign(&result_list->next, next);
	result_assign_ref_counted(result, &result_list->_base, result_list_print);
	SET_TYPE(result_list_p, result_list);
}	


// ----------------------------------------------------------------------------------------



char *strprintf(const char *fmt, ...)
{
	va_list arg_ptr;
	va_start(arg_ptr, fmt);
	static char buffer[1000];
	vsnprintf(buffer, 999, fmt, arg_ptr);
	va_end(arg_ptr);
	buffer[999] = '\0';
	char *str = (char *)malloc(strlen(buffer) + 1);
	strcpy(str, buffer);
	return str;
}

typedef struct tree_list *tree_list_p;
struct tree_list
{
	tree_p tree;
	tree_list_p next;
};

tree_list_p new_result_list(tree_p tree)
{
	tree_list_p tree_list = MALLOC(struct tree_list);
	tree_list->tree = tree;
	tree_list->next = NULL;
	return tree_list;
}

typedef struct tree_iterator tree_iterator_t, *tree_iterator_p;
struct tree_iterator
{
	const char *name;
	int nr_children;
	result_t *children;
};

node_p tree_child_node(tree_p tree, int nr)
{
	result_p result = tree_child(tree, nr);
	return result != NULL ? CAST(node_p, result->data) : NULL;
}

tree_p tree_of_result(result_p result)
{
	if (result != NULL && result->data != NULL)
	{
		node_p node = CAST(node_p, result->data);
		if (node->type_name == tree_node_type)
			return CAST(tree_p, node);
	}
	return NULL;
}

tree_p tree_child_tree(tree_p tree, int nr) { return tree_of_result(tree_child(tree, nr)); }

tree_p list_of_result(result_p result)
{
	if (result != NULL && result->data != NULL)
	{
		node_p node = CAST(node_p, result->data);
		if (node->type_name == tree_node_type)
		{
			tree_p tree = CAST(tree_p, node);
			return tree->tree_param->name == list_type ? tree : NULL;
		}
	}
	return NULL;
}

tree_p tree_child_list(tree_p tree, int nr) { return list_of_result(tree_child(tree, nr)); }

void tree_iterator_init(tree_iterator_p tree_iterator, result_p result)
{
	tree_iterator->name = "";
	tree_iterator->nr_children = 0;
	tree_p tree = tree_of_result(result);
	if (tree != NULL)
	{
		tree_iterator->name = tree->tree_param->name;
		tree_iterator->nr_children = tree->nr_children;
		tree_iterator->children = tree->children;
	}
}
#define TREE_ITERATOR(N,R) tree_iterator_t N; tree_iterator_init(&N, R)
#define ITERATOR_TREE(C,I,N) tree_p C = tree_of_result(&I.children[N]);

node_p node_of_result(result_p result)
{
	return result != 0 && result->data != 0 ? CAST(node_p, result->data) : NULL;
}

const char *tree_name(result_p result)
{
	if (result == 0)
		return "<result_p is NULL>";
	if (result->data == 0)
		return "<result_p->data is NULL>";
	node_p node = CAST(node_p, result->data);
	if (node->type_name == ident_node_type)
		return CAST(ident_node_p, node)->name;
	if (node->type_name == tree_node_type)
	{
		tree_p tree = CAST(tree_p, node);
		if (tree->tree_param == NULL || tree->tree_param->name == NULL)
			return "<result->data->tree_param == NULL>";
		return tree->tree_param->name;
	}
	return "<result_p->data has no name>";
}

void node_print(void *data, ostream_p ostream)
{
	node_p tree = CAST(node_p, data);
	if (tree->type_name == ident_node_type)
		ident_print(data, ostream);
	else if (tree->type_name == char_node_type)
		char_node_print(data, ostream);
	else if (tree->type_name == string_node_type)
		string_node_print(data, ostream);
	else if (tree->type_name == int_node_type)
		int_node_print(data, ostream);
}

node_p make_tree_for(tree_param_p tree_param, int nr, ...)
{
	tree_p tree = malloc_tree(tree_param);
	tree->nr_children = nr;
	va_list args;
	va_start(args, nr);
	tree->children = MALLOC_N(nr, result_t);
	for (int i = 0; i < nr; i++)
	{
		RESULT_INIT(&tree->children[i]);
		node_p node = va_arg(args, node_p);
		//fprintf(stderr, "arg %d: %p\n", i, node);
		
		if (node != NULL)
			result_assign_ref_counted(&tree->children[i], &node->_base, node_print);
	}
	va_end(args);
	//fprintf(stderr, "make_tree_for returns %p\n", &tree->_node);
	return &tree->_node;
}

node_p make_ident_node(const char *name)
{
	ident_node_p ident = MALLOC(struct ident_node_t);
	init_node(&ident->_node, ident_node_type, NULL);
	ident->name = ident_string(name);
	return &ident->_node;
}

node_p make_int_node(int value)
{
	int_node_p int_node = MALLOC(struct int_node_t);
	init_node(&int_node->_node, int_node_type, NULL);
	int_node->value = value;
	return &int_node->_node;
}

typedef struct task_func *task_func_p;
struct task_func
{
	const char *name;
	result_t statement_trace;
	task_func_p next;
};

typedef struct task *task_p;
struct task
{
	char *name;
	int nr;
	char *result_var_name;
	int nr_local_vars;
	int nr_funcs;
	task_func_p task_funcs;
	task_func_p *ref_next_task_func;
	task_p next;
};
task_p tasks = NULL;
task_p *ref_next_task = &tasks;
int nr_tasks = 0;

task_p cur_task = NULL;

void add_task_func(result_p statement_trace)
{
	task_func_p task_func = MALLOC(struct task_func);
	task_func->name = strprintf("%s_step%d", cur_task->name, ++cur_task->nr_funcs);
	RESULT_INIT(&task_func->statement_trace);
	task_func->next = NULL;
	result_assign(&task_func->statement_trace, statement_trace);
	*cur_task->ref_next_task_func = task_func;
	cur_task->ref_next_task_func = &task_func->next;
}

task_func_p find_task_func(result_p statement)
{
	for (task_func_p task_func = cur_task->task_funcs; task_func != NULL; task_func = task_func->next)
		if (CAST(result_list_p, task_func->statement_trace.data)->value.data == statement->data)
			return task_func;
	return NULL;
}

task_p find_task(const char *name)
{
	for (task_p task = tasks; task != NULL; task = task->next)
		if (strcmp(task->name, name) == 0)
			return task;
	return NULL;
}

typedef struct var_context *var_context_p;
struct var_context
{
	char *name;
	char *global_name;
	var_context_p prev;
};

var_context_p new_var_context(char *name, char *global_name, var_context_p prev)
{
	var_context_p var_context = MALLOC(struct var_context);
	var_context->name = name;
	var_context->global_name = global_name;
	var_context->prev = prev;
	return var_context;
}

char *var_context_global_name(var_context_p var_context, char *name)
{
	for (; var_context != 0; var_context = var_context->prev)
		if (strcmp(var_context->name, name) == 0)	
			return var_context->global_name;
	return name;
}

tree_list_p new_global_vars = NULL;
tree_list_p *ref_new_global_var = &new_global_vars;

void pass1_expr(node_p node, var_context_p var_context, ostream_p ostream)
{
	if (node == NULL)
		return;
		
	if (node->type_name == ident_node_type)
	{
		ident_node_p ident = CAST(ident_node_p, node);
		printf("Replacing %s ", ident->name);
		ident->name = var_context_global_name(var_context, ident->name);
		printf("with %s\n", ident->name);
	}
	else if (node->type_name == tree_node_type)
	{
		tree_p tree = CAST(tree_p, node);
		for (int i = 0; i < tree->nr_children; i++)
			pass1_expr(CAST(node_p, tree->children[i].data), var_context, ostream);
	}
}

bool is_call_to_task(node_p node)
{
	if (node_is_tree(node, "call"))
	{
		node_p func_name = tree_child_node(CAST(tree_p, node), 1);
		if (func_name->type_name == ident_node_type)
		{
			ident_node_p ident = CAST(ident_node_p, func_name);
			return find_task(ident->name) != NULL;
		}
	}
	return FALSE;
}

task_p task_with_call(node_p node)
{
	node_p func_name = tree_child_node(CAST(tree_p, node), 1);
	if (func_name->type_name == ident_node_type)
	{
		ident_node_p ident = CAST(ident_node_p, func_name);
		return find_task(ident->name);
	}
	return NULL;
}

void pass1_statement(result_p result, result_p parent_statement_trace, var_context_p var_context, ostream_p ostream)
{
	ENTER_RESULT_CONTEXT
	
	tree_p statement = tree_of_result(result);
	for (int i = 0; i < indent; i++)
		printf("  ");
	if (statement == NULL)
	{
		printf("pass1_statement: NULL\n");
		return;
	}
	indent++;
	DECL_RESULT(statement_trace);
	make_result_list(&statement_trace, result, parent_statement_trace);
	if (tree_is(statement, "list") || tree_is(statement, "statements"))
	{
		printf("statements / list\n");
		for (int i = 1; i <= statement->nr_children; i++)
		{
			tree_p child = tree_child_tree(statement, i);
			if (child == NULL)
			{}
			else if (tree_is(child, "declaration"))
			{
				//result_print(tree_child(statement, i), ostream);
				tree_p type = tree_child_tree(child, 1);
				tree_p decl = tree_child_tree(child, 2);
				//printf("%d", j);
				tree_p decl_init = tree_child_tree(decl, 1);
				node_p init = tree_child_node(decl_init, 2);
				pass1_expr(init, var_context, ostream);
				node_p var_node = tree_child_node(decl_init, 1);
				if (var_node->type_name == ident_node_type)
				{
					ident_node_p ident = CAST(ident_node_p, var_node);
					char *loc_var_name = strprintf("%s_var%d_%s", cur_task->name, ++cur_task->nr_local_vars, ident->name);
					// Add global var
					var_context = new_var_context(ident->name, loc_var_name, var_context);
					ident->name = loc_var_name;
					//printf("var_local %s => %s\n", ident->name, loc_var_name);
					node_p declaration
						= make_tree_for(&declaration_tp, 2,
							type,
							make_tree_for(&decl_tp, 1,
								make_tree_for(&decl_init_tp, 2,
									make_ident_node(loc_var_name),
									tree_child_tree(decl_init, 2))));
					*ref_new_global_var = new_result_list((tree_p)declaration);
					ref_new_global_var = &(*ref_new_global_var)->next;
				}
				else
				{
					printf("ERROR var decl: ");
					result_print(tree_child(decl_init, 1), ostream);
					printf("\n");
				}
				if (is_call_to_task(init))
				{
					DECL_RESULT(child_trace);
					make_result_list(&child_trace, tree_child(statement, i), &statement_trace);
					add_task_func(&child_trace);
					DISP_RESULT(child_trace);
				}
				printf("\n");
			}
			else
				pass1_statement(tree_child(statement, i), &statement_trace, var_context, ostream);
		}
	}
	else if (tree_is(statement, "if"))
	{
		pass1_expr(tree_child_node(statement, 1), var_context, ostream);
		pass1_statement(tree_child(statement, 2), &statement_trace, var_context, ostream);
		pass1_statement(tree_child(tree_child_tree(statement, 3), 1),  &statement_trace, var_context, ostream);
	}
	else if (tree_is(statement, "queuefor"))
	{
		add_task_func(&statement_trace);
		pass1_statement(tree_child(statement, 2), &statement_trace, var_context, ostream);
	}
	else if (tree_is(statement, "poll"))
	{
		add_task_func(&statement_trace);
		pass1_statement(tree_child(statement, 1), &statement_trace, var_context, ostream);
		tree_p atmost_opt = tree_child_tree(statement, 2);
		if (atmost_opt != NULL)
		{
			DECL_RESULT(atmost_statement_trace);
			make_result_list(&atmost_statement_trace, tree_child(statement, 2), &statement_trace);
			add_task_func(&atmost_statement_trace);
			pass1_expr(tree_child_node(atmost_opt, 1), var_context, ostream);
			pass1_statement(tree_child(atmost_opt, 2), &atmost_statement_trace, var_context, ostream);
			DISP_RESULT(atmost_statement_trace);
		}
	}		
	else if (tree_is(statement, "semi"))
	{
		pass1_expr(tree_child_node(statement, 1), var_context, ostream);
		node_p node = tree_child_node(statement, 1);
		if (   is_call_to_task(node)
			|| (   node_is_tree(node, "assignment")
				&& is_call_to_task(tree_child_node(CAST(tree_p, node), 3))))
			add_task_func(&statement_trace);
	}
	else if (tree_is(statement, "ret"))
	{
		pass1_expr(tree_child_node(statement, 1), var_context, ostream);
	}
	else
	{
		printf("pass1_statement: ");
		tree_print(statement, ostream);
		printf("\n");
	}
	DISP_RESULT(statement_trace);
	indent--;
}

void prepend_child_node(result_p children, node_p node)
{
	ENTER_RESULT_CONTEXT
	DECL_RESULT(child_result)
	result_assign_ref_counted(&child_result, &node->_base, tree_print);
	prepend_child(children, &child_result);
	DISP_RESULT(child_result)
	EXIT_RESULT_CONTEXT
}


void pass2_statement(result_p result, result_p children, ostream_p ostream)
{
	//ENTER_RESULT_CONTEXT
	
	tree_p statement = tree_of_result(result);
	for (int i = 0; i < indent; i++)
		printf("  ");
	if (statement == NULL)
	{
		printf("pass2_statement: NULL\n");
		return;
	}
	indent++;
	if (tree_is(statement, "list") || tree_is(statement, "statements"))
	{
		printf("statements / list\n");
		for (int i = 1; i <= statement->nr_children; i++)
		{
			tree_p child = tree_child_tree(statement, i);
			if (child == NULL)
			{}
			else if (tree_is(child, "declaration"))
			{
				//result_print(tree_child(statement, i), ostream);
				//tree_p type = tree_child_tree(child, 1);
				tree_p decl = tree_child_tree(child, 2);

				tree_p decl_init = tree_child_tree(decl, 1);
				node_p init = tree_child_node(decl_init, 2);
				//pass1_expr(init, var_context, ostream);
				//node_p var_node = tree_child_node(decl_init, 1);
				if (init != NULL)
				{
					if (is_call_to_task(init))
					{
						task_p task_called = task_with_call(init);
						task_func_p task_func = find_task_func(&statement->children[i]);
						// Create call to task
						prepend_child_node(children,
							make_tree_for(&semi_tp, 1,
								make_tree_for(&call_tp, 2,
									make_ident_node("os_call_task"),
									make_tree_for(&list_tp, 3,
										make_int_node(task_called->nr),
										make_int_node(cur_task->nr),
										make_ident_node(task_func->name)
										))));
					}
					else
					{
						
						// Create assignment from declaration
						prepend_child_node(children,
							make_tree_for(&semi_tp, 1,
								make_tree_for(&assignment_tp, 3,
									tree_child_node(decl_init, 1),
									make_tree_for(&ass_tp, 0),
									init)));
					}
				}
				printf("\n");
			}
			else
			{
				pass2_statement(tree_child(statement, i), children, ostream);
			}
		}
	}
	else if (tree_is(statement, "if"))
	{
		//pass2_expr(tree_child_node(statement, 1), var_context, ostream);
		//pass2_statement(tree_child(statement, 2), &statement_trace, var_context, ostream);
		//pass2_statement(tree_child(tree_child_tree(statement, 3), 1),  &statement_trace, var_context, ostream);
	}
	else if (tree_is(statement, "queuefor"))
	{
		// Create call to os_query_for;
		//add_task_func(&statement_trace, NULL);
		//pass2_statement(tree_child(statement, 2), &statement_trace, var_context, ostream);
	}
	else if (tree_is(statement, "poll"))
	{
		// Create
		//add_task_func(&statement_trace, NULL);
		//pass2_statement(tree_child(statement, 1), &statement_trace, var_context, ostream);
		//tree_p atmost_opt = tree_child_tree(statement, 2);
		//if (atmost_opt != NULL)
		//{
		//	DECL_RESULT(atmost_statement_trace);
		//	make_result_list(&atmost_statement_trace, tree_child(statement, 2), &statement_trace);
		//	add_task_func(&atmost_statement_trace, NULL);
		//	//pass2_expr(tree_child_node(atmost_opt, 1), var_context, ostream);
		//	pass2_statement(tree_child(atmost_opt, 2), &atmost_statement_trace, var_context, ostream);
		//	DISP_RESULT(atmost_statement_trace);
		//}
	}
	else if (tree_is(statement, "semi"))
	{
		//for (int i = 1; i <= statement->nr_children; i++)
		//	pass2_expr(tree_child_node(statement, i), var_context, ostream);
		if (statement->nr_children == 1)
		{
			node_p node = tree_child_node(statement, 1);
			if (   is_call_to_task(node)
				|| (   node_is_tree(node, "assignment")
					&& is_call_to_task(tree_child_node(CAST(tree_p, node), 3))))
			{
				// Create call to task
				//add_task_func(&statement_trace, NULL);
			}
		}
	}
	else if (tree_is(statement, "ret"))
	{
		// Create statement to retun
		//pass1_expr(tree_child_node(statement, 1), var_context, ostream);
	}
	else
	{
		//printf("pass1_statement: ");
		//tree_print(statement, ostream);
		//printf("\n");
	}
	indent--;
}


void compile(result_p result, ostream_p ostream)
{
	//result_list_p tasks = NULL;
	//result_list_p *ref_next_task = &tasks;
	ENTER_RESULT_CONTEXT
	
	TREE_ITERATOR(decls0, result);
	for (int i = 0; i < decls0.nr_children; i++)
	{
		ITERATOR_TREE(decl, decls0, i);
		if (tree_is(decl, "declaration"))
		{
			tree_p types = tree_child_list(decl, 1);
			bool is_task = types != 0 && tree_is(tree_child_tree(types, 1), "task");
			if (is_task)
			{
				char *task_name = ident_name(tree_child(tree_child_tree(decl, 2), 1));
				result_p result_type = tree_child(types, 2);
				const char *result_type_name = tree_name(result_type);
				char *result_var_name = strprintf("%s_result", task_name);
				cur_task = MALLOC(struct task);
				cur_task->name = task_name;
				cur_task->nr = nr_tasks++;
				cur_task->result_var_name = result_var_name;
				cur_task->nr_local_vars = 0;
				cur_task->nr_funcs = 0;
				cur_task->task_funcs = NULL;
				cur_task->ref_next_task_func = &cur_task->task_funcs;
				cur_task->next = NULL;
				*ref_next_task = cur_task;
				ref_next_task = &cur_task->next;
				printf("task %s %s\n", task_name, result_type_name);
				if (strcmp(result_type_name, "void") != 0)
				{
					// Add global var
					node_p declaration 
						= make_tree_for(&declaration_tp, 2,
							make_tree_for(&list_tp, 1, node_of_result(result_type)),
							make_tree_for(&decl_tp, 1,
								make_tree_for(&list_tp, 1,
									make_tree_for(&decl_init_tp, 2,
										make_ident_node(result_var_name),
										NULL))));
					*ref_new_global_var = new_result_list((tree_p)declaration);
					ref_new_global_var = &(*ref_new_global_var)->next;
				}
			}
		}
	}

	cur_task = tasks;
	TREE_ITERATOR(decls, result);
	for (int i = 0; i < decls.nr_children; i++)
	{
		ITERATOR_TREE(decl, decls, i);
		if (tree_is(decl, "declaration"))
		{
			printf("\n");
			tree_p types = tree_child_list(decl, 1);
			bool is_task = types != 0 && tree_is(tree_child_tree(types, 1), "task");
			if (is_task)
			{
				DECL_RESULT(statement_trace);
				pass1_statement(tree_child(tree_child_tree(tree_child_tree(decl, 2), 3), 1), &statement_trace, NULL, ostream);
				DISP_RESULT(statement_trace);
				
				for (task_func_p task_func = cur_task->task_funcs; task_func != 0; task_func = task_func->next)
				{
					printf("\nTask func %s : ", task_func->name);
					result_print(&task_func->statement_trace, ostream);
					printf("\n");
				}
				cur_task = cur_task->next;
			}
			else
			{
				if (tree_is(tree_child_tree(decl, 2), "decl"))
					printf("global variable ");
				result_print(&decls.children[i], ostream);
			}
			printf("\n");
		}
		else
			printf("other\n");
	}
	EXIT_RESULT_CONTEXT
}

int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		printf("Usuage: %s <filename>\n", argv[0]);
		return 0;
	}
	FILE *f = fopen(argv[1], "r");
	if (f == 0)
	{
		printf("Cannot open %s\n", argv[1]);
		return 0;
	}
	text_buffer_t text_buffer;
	text_buffer_from_file(&text_buffer, f);
	fclose(f);
	
	file_ostream_t debug_ostream;
	file_ostream_init(&debug_ostream, stdout);
	stdout_stream = &debug_ostream.ostream;
	
	non_terminal_dict_p all_nt = NULL;
	c_grammar(&all_nt);
    //test_c_grammar(&all_nt_c_grammar);

	ENTER_RESULT_CONTEXT

	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);
	
	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache_hit_function = solutions_find;
	parser.cache = &solutions;
	
	DECL_RESULT(result);
	if (parse_nt(&parser, find_nt("root", &all_nt), &result) && text_buffer_end(&text_buffer))
	{
		if (result.data == NULL)
		{
			fprintf(stderr, "ERROR: parsing did not return result\n");
			print_expected(stdout);
		}
		else
		{
			file_ostream_t out_ostream;
			file_ostream_init(&out_ostream, stdout);
			//result_print(&result, &out_ostream.ostream);
			//printf("\n");
			compile(&result, &out_ostream.ostream);
		}
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse \n");
		print_expected(stdout);
	}
	DISP_RESULT(result);

	EXIT_RESULT_CONTEXT
	solutions_free(&solutions);

	return 0;
}

