/*
 * math2.h - Clean tree-based math expression system
 * 
 * Architecture:
 *   - Tree structure where any node can contain other nodes
 *   - Sequences are horizontal lists of nodes
 *   - Cursor is a pointer into the tree
 *   - Two-pass rendering: measure then draw
 */

#ifndef MATH2_H
#define MATH2_H

#include <stdbool.h>

/* Maximum nodes in pool */
#define MAX_NODES 256

/* Maximum LaTeX output length */
#define MAX_LATEX 1024

/* Node types */
typedef enum {
    NODE_EMPTY,         /* Unused slot in pool */
    NODE_SEQUENCE,      /* Horizontal list of children */
    NODE_TEXT,          /* Leaf: number, variable, operator */
    NODE_FRACTION,      /* num/denom */
    NODE_EXPONENT,      /* base^power */
    NODE_SUBSCRIPT,     /* base_index */
    NODE_ROOT,          /* sqrt (fixed index) */
    NODE_NTHROOT,       /* nth root with editable index */
    NODE_ABS,           /* |content| */
    NODE_PAREN,         /* (content) */
    NODE_FUNCTION,      /* sin, cos, etc. with argument */
    NODE_MIXED_FRAC,    /* whole + num/denom */
} node_type_t;

/* Text subtypes for NODE_TEXT */
typedef enum {
    TEXT_NUMBER,        /* 0-9, . */
    TEXT_VARIABLE,      /* x, y, z, α, β, etc. */
    TEXT_OPERATOR,      /* +, -, ×, ÷ */
    TEXT_PI,            /* π */
    TEXT_PAREN_OPEN,    /* ( */
    TEXT_PAREN_CLOSE,   /* ) */
} text_type_t;

/* Forward declaration */
typedef struct expr_node expr_node_t;

/* Expression node - can be leaf or container */
struct expr_node {
    node_type_t type;
    expr_node_t *parent;    /* Parent node for navigation */
    expr_node_t *next;      /* Next sibling in sequence */
    expr_node_t *prev;      /* Previous sibling in sequence */
    
    union {
        /* NODE_SEQUENCE: horizontal list */
        struct {
            expr_node_t *first;     /* First child */
            expr_node_t *last;      /* Last child (for fast append) */
        } seq;
        
        /* NODE_TEXT: leaf with text content */
        struct {
            text_type_t subtype;
            char text[16];          /* The actual text */
        } text;
        
        /* NODE_FRACTION */
        struct {
            expr_node_t *numer;     /* Numerator sequence */
            expr_node_t *denom;     /* Denominator sequence */
        } frac;
        
        /* NODE_EXPONENT */
        struct {
            expr_node_t *base;      /* Base sequence */
            expr_node_t *power;     /* Power sequence */
        } exp;
        
        /* NODE_SUBSCRIPT */
        struct {
            expr_node_t *base;      /* Base sequence */
            expr_node_t *sub;       /* Subscript sequence */
        } subscript;
        
        /* NODE_ROOT */
        struct {
            int index;              /* 2 for sqrt, 3 for cube root, etc. */
            expr_node_t *content;   /* Content sequence */
        } root;
        
        /* NODE_NTHROOT - nth root with editable index */
        struct {
            expr_node_t *index;     /* Index sequence (editable) */
            expr_node_t *content;   /* Content sequence */
        } nthroot;
        
        /* NODE_MIXED_FRAC */
        struct {
            expr_node_t *whole;     /* Whole number part */
            expr_node_t *numer;     /* Numerator */
            expr_node_t *denom;     /* Denominator */
        } mixed;
        
        /* NODE_ABS */
        struct {
            expr_node_t *content;   /* Content sequence */
        } abs;
        
        /* NODE_PAREN */
        struct {
            expr_node_t *content;   /* Content sequence */
        } paren;
        
        /* NODE_FUNCTION */
        struct {
            char name[8];           /* "sin", "cos", etc. */
            expr_node_t *arg;       /* Argument sequence */
        } func;
    } data;
};

/* Metrics returned by measure pass */
typedef struct {
    int width;
    int height;
    int baseline;   /* Distance from top to middle line */
} metrics_t;

/* Cursor position in tree */
typedef struct {
    expr_node_t *sequence;  /* Which sequence we're in */
    expr_node_t *after;     /* Node cursor is after (NULL = at start) */
} cursor_t;

/* Main expression editor state */
typedef struct {
    expr_node_t nodes[MAX_NODES];   /* Node pool */
    int next_free;                   /* Next free slot */
    
    expr_node_t *root;              /* Root sequence */
    cursor_t cursor;                /* Current cursor position */
    
    char latex[MAX_LATEX];          /* Generated LaTeX */
    
    bool shift_mode;                /* SHIFT pressed */
    bool alpha_mode;                /* ALPHA pressed */
} math_expr2_t;

/* ===== Node Pool Management ===== */

/* Initialize expression */
void math2_init(math_expr2_t *expr);

/* Allocate a new node from pool */
expr_node_t *math2_alloc_node(math_expr2_t *expr, node_type_t type);

/* Free a node and all children back to pool */
void math2_free_node(math_expr2_t *expr, expr_node_t *node);

/* Create a new empty sequence */
expr_node_t *math2_new_sequence(math_expr2_t *expr);

/* ===== Sequence Operations ===== */

/* Insert node into sequence after given position (NULL = at start) */
void seq_insert_after(expr_node_t *seq, expr_node_t *after, expr_node_t *node);

/* Remove node from its sequence */
void seq_remove(expr_node_t *node);

/* Check if sequence is empty */
bool seq_is_empty(expr_node_t *seq);

/* ===== Cursor Operations ===== */

/* Move cursor left within sequence */
bool cursor_left(math_expr2_t *expr);

/* Move cursor right within sequence */
bool cursor_right(math_expr2_t *expr);

/* Move cursor into child slot (down into fraction numerator, etc.) */
bool cursor_enter(math_expr2_t *expr);

/* Enter node from the right (pressing LEFT), goes to last slot end */
bool cursor_enter_left(math_expr2_t *expr);

/* Enter node from the left (pressing RIGHT), goes to first slot start */
bool cursor_enter_right(math_expr2_t *expr);

/* Move cursor out to parent (to the right of container) */
bool cursor_exit(math_expr2_t *expr);

/* Exit to the right of container */
bool cursor_exit_right(math_expr2_t *expr);

/* Exit to the left of container */
bool cursor_exit_left(math_expr2_t *expr);

/* Move between slots (numerator <-> denominator) */
bool cursor_next_slot(math_expr2_t *expr);
bool cursor_prev_slot(math_expr2_t *expr);

/* ===== Editing Operations ===== */

/* Insert text node at cursor */
void math2_insert_text(math_expr2_t *expr, text_type_t subtype, const char *text);

/* Insert fraction at cursor, optionally collecting previous nodes as numerator */
void math2_insert_fraction(math_expr2_t *expr);

/* Insert exponent at cursor, optionally collecting previous node as base */
void math2_insert_exponent(math_expr2_t *expr);

/* Insert subscript at cursor */
void math2_insert_subscript(math_expr2_t *expr);

/* Insert square root at cursor */
void math2_insert_sqrt(math_expr2_t *expr);

/* Insert nth root at cursor (fixed index) */
void math2_insert_nthroot(math_expr2_t *expr, int n);

/* Insert xth root at cursor (editable index) */
void math2_insert_xthroot(math_expr2_t *expr);

/* Insert mixed fraction at cursor */
void math2_insert_mixed_frac(math_expr2_t *expr);

/* Insert absolute value at cursor */
void math2_insert_abs(math_expr2_t *expr);

/* Insert parentheses at cursor */
void math2_insert_paren(math_expr2_t *expr);

/* Insert function (sin, cos, etc.) at cursor */
void math2_insert_function(math_expr2_t *expr, const char *name);

/* Delete node before cursor */
void math2_delete(math_expr2_t *expr);

/* Clear entire expression */
void math2_clear(math_expr2_t *expr);

/* ===== Rendering ===== */

/* Measure a node and return its metrics */
metrics_t math2_measure(expr_node_t *node, int font_scale);

/* Draw expression at position */
void math2_draw(math_expr2_t *expr, int x, int y);

/* Get expression dimensions for centering */
int math2_get_width(math_expr2_t *expr);
int math2_get_height(math_expr2_t *expr);

/* Draw a single node (recursive) */
void math2_draw_node(expr_node_t *node, int x, int y_baseline, int font_scale,
                     math_expr2_t *expr);

/* ===== LaTeX Generation ===== */

/* Generate LaTeX string from expression */
void math2_to_latex(math_expr2_t *expr);

/* ===== Mode Management ===== */

/* Toggle shift mode */
void math2_toggle_shift(math_expr2_t *expr);

/* Toggle alpha mode */
void math2_toggle_alpha(math_expr2_t *expr);

/* Clear all modes */
void math2_clear_modes(math_expr2_t *expr);

#endif /* MATH2_H */
