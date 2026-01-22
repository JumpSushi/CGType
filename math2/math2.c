/*
 * math2.c - attempted tree-based math vpam implementation
 */

#include "math2.h"
#include <gint/display.h>
#include <gint/keyboard.h>
#include <string.h>
#include <stdio.h>

/* ===== Constants ===== */

#define CHAR_W      9       
#define CHAR_H      14      
#define FRAC_PAD    4       
#define FRAC_BAR_H  2       
#define EXP_SCALE   70      
#define ROOT_PAD    4       

/* Colors */
#define COL_TEXT        C_BLACK
#define COL_CURSOR      C_RGB(0, 0, 31)
#define COL_PLACEHOLDER C_RGB(20, 20, 20)
#define COL_FRAC_BAR    C_BLACK

/* Parenthesis colors for nesting depth */
static const color_t PAREN_COLORS[] = {
    C_RGB(0, 0, 31),    /* Blue */
    C_RGB(31, 0, 0),    /* Red */
    C_RGB(0, 20, 0),    /* Green */
    C_RGB(20, 0, 20),   /* Purple */
    C_RGB(20, 15, 0),   /* Orange */
};
#define NUM_PAREN_COLORS 5

static int g_paren_depth = 0;  /* Track depth during drawing */

/* External setting for bracket coloring */
bool g_color_brackets = true;

/* Cursor flash state (controlled externally) */
bool g_cursor_visible = true;

/* ===== Node Pool Management ===== */

void math2_init(math_expr2_t *expr)
{
    memset(expr, 0, sizeof(*expr));
    
    /* Mark all nodes as empty */
    for(int i = 0; i < MAX_NODES; i++) {
        expr->nodes[i].type = NODE_EMPTY;
    }
    expr->next_free = 0;
    
    /* Create root sequence */
    expr->root = math2_new_sequence(expr);
    
    /* Initialize cursor at start of root */
    expr->cursor.sequence = expr->root;
    expr->cursor.after = NULL;
    
    expr->shift_mode = false;
    expr->alpha_mode = false;
}

expr_node_t *math2_alloc_node(math_expr2_t *expr, node_type_t type)
{
    /* Find next free slot */
    for(int i = 0; i < MAX_NODES; i++) {
        int idx = (expr->next_free + i) % MAX_NODES;
        if(expr->nodes[idx].type == NODE_EMPTY) {
            expr_node_t *node = &expr->nodes[idx];
            memset(node, 0, sizeof(*node));
            node->type = type;
            expr->next_free = (idx + 1) % MAX_NODES;
            return node;
        }
    }
    return NULL; /* Pool exhausted */
}

void math2_free_node(math_expr2_t *expr, expr_node_t *node)
{
    if(!node || node->type == NODE_EMPTY) return;
    
    /* Recursively free children based on type */
    switch(node->type) {
        case NODE_SEQUENCE:
            {
                expr_node_t *child = node->data.seq.first;
                while(child) {
                    expr_node_t *next = child->next;
                    math2_free_node(expr, child);
                    child = next;
                }
            }
            break;
        case NODE_FRACTION:
            math2_free_node(expr, node->data.frac.numer);
            math2_free_node(expr, node->data.frac.denom);
            break;
        case NODE_EXPONENT:
            math2_free_node(expr, node->data.exp.base);
            math2_free_node(expr, node->data.exp.power);
            break;
        case NODE_SUBSCRIPT:
            math2_free_node(expr, node->data.subscript.base);
            math2_free_node(expr, node->data.subscript.sub);
            break;
        case NODE_ROOT:
            math2_free_node(expr, node->data.root.content);
            break;
        case NODE_NTHROOT:
            math2_free_node(expr, node->data.nthroot.index);
            math2_free_node(expr, node->data.nthroot.content);
            break;
        case NODE_ABS:
            math2_free_node(expr, node->data.abs.content);
            break;
        case NODE_PAREN:
            math2_free_node(expr, node->data.paren.content);
            break;
        case NODE_FUNCTION:
            math2_free_node(expr, node->data.func.arg);
            break;
        case NODE_MIXED_FRAC:
            math2_free_node(expr, node->data.mixed.whole);
            math2_free_node(expr, node->data.mixed.numer);
            math2_free_node(expr, node->data.mixed.denom);
            break;
        default:
            break;
    }
    
    node->type = NODE_EMPTY;
}

expr_node_t *math2_new_sequence(math_expr2_t *expr)
{
    expr_node_t *seq = math2_alloc_node(expr, NODE_SEQUENCE);
    if(seq) {
        seq->data.seq.first = NULL;
        seq->data.seq.last = NULL;
    }
    return seq;
}

/* ===== Sequence Operations ===== */

void seq_insert_after(expr_node_t *seq, expr_node_t *after, expr_node_t *node)
{
    if(!seq || !node || seq->type != NODE_SEQUENCE) return;
    
    node->parent = seq;
    
    if(after == NULL) {
        /* Insert at beginning */
        node->prev = NULL;
        node->next = seq->data.seq.first;
        if(seq->data.seq.first) {
            seq->data.seq.first->prev = node;
        }
        seq->data.seq.first = node;
        if(!seq->data.seq.last) {
            seq->data.seq.last = node;
        }
    } else {
        /* Insert after given node */
        node->prev = after;
        node->next = after->next;
        if(after->next) {
            after->next->prev = node;
        }
        after->next = node;
        if(seq->data.seq.last == after) {
            seq->data.seq.last = node;
        }
    }
}

void seq_remove(expr_node_t *node)
{
    if(!node || !node->parent) return;
    
    expr_node_t *seq = node->parent;
    if(seq->type != NODE_SEQUENCE) return;
    
    if(node->prev) {
        node->prev->next = node->next;
    } else {
        seq->data.seq.first = node->next;
    }
    
    if(node->next) {
        node->next->prev = node->prev;
    } else {
        seq->data.seq.last = node->prev;
    }
    
    node->parent = NULL;
    node->prev = NULL;
    node->next = NULL;
}

bool seq_is_empty(expr_node_t *seq)
{
    return seq && seq->type == NODE_SEQUENCE && seq->data.seq.first == NULL;
}

/* ===== Cursor Operations ===== */

bool cursor_left(math_expr2_t *expr)
{
    if(expr->cursor.after) {
        /* Move to previous node */
        expr->cursor.after = expr->cursor.after->prev;
        return true;
    }
    /* At start of sequence - can't move left within this sequence */
    return false;
}

bool cursor_right(math_expr2_t *expr)
{
    expr_node_t *next;
    if(expr->cursor.after) {
        next = expr->cursor.after->next;
    } else {
        next = expr->cursor.sequence->data.seq.first;
    }
    
    if(next) {
        expr->cursor.after = next;
        return true;
    }
    /* At end of sequence - can't move right within this sequence */
    return false;
}

/* Get the last editable child slot of a node (for entering from the right) */
static expr_node_t *get_last_slot(expr_node_t *node)
{
    switch(node->type) {
        case NODE_FRACTION:
            return node->data.frac.denom;
        case NODE_EXPONENT:
            return node->data.exp.power;  /* Enter at power when coming from right */
        case NODE_SUBSCRIPT:
            return node->data.subscript.sub;
        case NODE_ROOT:
            return node->data.root.content;
        case NODE_NTHROOT:
            return node->data.nthroot.content;
        case NODE_ABS:
            return node->data.abs.content;
        case NODE_PAREN:
            return node->data.paren.content;
        case NODE_FUNCTION:
            return node->data.func.arg;
        case NODE_MIXED_FRAC:
            return node->data.mixed.denom;
        default:
            return NULL;
    }
}

/* Get the first editable child slot of a node */
static expr_node_t *get_first_slot(expr_node_t *node)
{
    switch(node->type) {
        case NODE_FRACTION:
            return node->data.frac.numer;
        case NODE_EXPONENT:
            return node->data.exp.base;  /* Start at base, use DOWN to go to power */
        case NODE_SUBSCRIPT:
            return node->data.subscript.base;
        case NODE_ROOT:
            return node->data.root.content;
        case NODE_NTHROOT:
            return node->data.nthroot.index;  /* Start at index */
        case NODE_ABS:
            return node->data.abs.content;
        case NODE_PAREN:
            return node->data.paren.content;
        case NODE_FUNCTION:
            return node->data.func.arg;
        case NODE_MIXED_FRAC:
            return node->data.mixed.whole;
        default:
            return NULL;
    }
}

/* Enter a node from the left (via RIGHT key) - go to first slot, start */
bool cursor_enter_right(math_expr2_t *expr)
{
    /* Enter the node that cursor is positioned before */
    expr_node_t *target;
    if(expr->cursor.after) {
        target = expr->cursor.after->next;
    } else {
        target = expr->cursor.sequence->data.seq.first;
    }
    
    if(!target) return false;
    
    expr_node_t *slot = get_first_slot(target);
    if(slot && slot->type == NODE_SEQUENCE) {
        expr->cursor.sequence = slot;
        expr->cursor.after = NULL; /* Go to start of slot */
        return true;
    }
    return false;
}

/* Enter a node from the right (via LEFT key) - go to last slot, end */
/* This is called AFTER cursor_left moved the cursor, so we need to enter
 * the node that is AFTER cursor.after (the one we just skipped over) */
bool cursor_enter_left(math_expr2_t *expr)
{
    /* Get the node we just moved past (it's after our current position) */
    expr_node_t *target;
    if(expr->cursor.after) {
        target = expr->cursor.after->next;
    } else {
        target = expr->cursor.sequence->data.seq.first;
    }
    
    if(!target) return false;
    
    expr_node_t *slot = get_last_slot(target);
    if(slot && slot->type == NODE_SEQUENCE) {
        expr->cursor.sequence = slot;
        expr->cursor.after = slot->data.seq.last; /* Go to end of slot */
        return true;
    }
    return false;
}

bool cursor_enter(math_expr2_t *expr)
{
    /* Enter the node after cursor (or first node if at start) */
    expr_node_t *target;
    if(expr->cursor.after) {
        target = expr->cursor.after;
    } else {
        target = expr->cursor.sequence->data.seq.first;
    }
    
    if(!target) return false;
    
    expr_node_t *slot = get_first_slot(target);
    if(slot && slot->type == NODE_SEQUENCE) {
        expr->cursor.sequence = slot;
        expr->cursor.after = slot->data.seq.last; /* Go to end of slot */
        return true;
    }
    return false;
}

/* Exit to the right of the container */
bool cursor_exit_right(math_expr2_t *expr)
{
    expr_node_t *seq = expr->cursor.sequence;
    if(!seq->parent) return false; /* Already at root */
    
    expr_node_t *container = seq->parent;
    
    if(container->parent && container->parent->type == NODE_SEQUENCE) {
        expr->cursor.sequence = container->parent;
        expr->cursor.after = container; /* Position AFTER the container */
        return true;
    }
    return false;
}

/* Exit to the left of the container */
bool cursor_exit_left(math_expr2_t *expr)
{
    expr_node_t *seq = expr->cursor.sequence;
    if(!seq->parent) return false; /* Already at root */
    
    expr_node_t *container = seq->parent;
    
    if(container->parent && container->parent->type == NODE_SEQUENCE) {
        expr->cursor.sequence = container->parent;
        expr->cursor.after = container->prev; /* Position BEFORE the container */
        return true;
    }
    return false;
}

bool cursor_exit(math_expr2_t *expr)
{
    expr_node_t *seq = expr->cursor.sequence;
    if(!seq->parent) return false; /* Already at root */
    
    /* Find the container node that owns this sequence */
    expr_node_t *container = seq->parent;
    
    /* Move cursor to after the container in its parent sequence */
    if(container->parent && container->parent->type == NODE_SEQUENCE) {
        expr->cursor.sequence = container->parent;
        expr->cursor.after = container;
        return true;
    }
    return false;
}

/* Get the next slot in a container (e.g., numerator -> denominator) */
static expr_node_t *get_next_slot(expr_node_t *container, expr_node_t *current_slot)
{
    switch(container->type) {
        case NODE_FRACTION:
            if(current_slot == container->data.frac.numer)
                return container->data.frac.denom;
            break;
        case NODE_EXPONENT:
            if(current_slot == container->data.exp.base)
                return container->data.exp.power;
            break;
        case NODE_SUBSCRIPT:
            if(current_slot == container->data.subscript.base)
                return container->data.subscript.sub;
            break;
        case NODE_NTHROOT:
            if(current_slot == container->data.nthroot.index)
                return container->data.nthroot.content;
            break;
        case NODE_MIXED_FRAC:
            if(current_slot == container->data.mixed.whole)
                return container->data.mixed.numer;
            if(current_slot == container->data.mixed.numer)
                return container->data.mixed.denom;
            break;
        default:
            break;
    }
    return NULL;
}

static expr_node_t *get_prev_slot(expr_node_t *container, expr_node_t *current_slot)
{
    switch(container->type) {
        case NODE_FRACTION:
            if(current_slot == container->data.frac.denom)
                return container->data.frac.numer;
            break;
        case NODE_EXPONENT:
            if(current_slot == container->data.exp.power)
                return container->data.exp.base;
            break;
        case NODE_SUBSCRIPT:
            if(current_slot == container->data.subscript.sub)
                return container->data.subscript.base;
            break;
        case NODE_NTHROOT:
            if(current_slot == container->data.nthroot.content)
                return container->data.nthroot.index;
            break;
        case NODE_MIXED_FRAC:
            if(current_slot == container->data.mixed.denom)
                return container->data.mixed.numer;
            if(current_slot == container->data.mixed.numer)
                return container->data.mixed.whole;
            break;
        default:
            break;
    }
    return NULL;
}

bool cursor_next_slot(math_expr2_t *expr)
{
    expr_node_t *seq = expr->cursor.sequence;
    if(!seq->parent) return false;
    
    expr_node_t *container = seq->parent;
    expr_node_t *next = get_next_slot(container, seq);
    
    if(next && next->type == NODE_SEQUENCE) {
        expr->cursor.sequence = next;
        expr->cursor.after = NULL; /* Start of new slot */
        return true;
    }
    return false;
}

bool cursor_prev_slot(math_expr2_t *expr)
{
    expr_node_t *seq = expr->cursor.sequence;
    if(!seq->parent) return false;
    
    expr_node_t *container = seq->parent;
    expr_node_t *prev = get_prev_slot(container, seq);
    
    if(prev && prev->type == NODE_SEQUENCE) {
        expr->cursor.sequence = prev;
        expr->cursor.after = prev->data.seq.last;
        return true;
    }
    return false;
}

/* ===== Editing Operations ===== */

void math2_insert_text(math_expr2_t *expr, text_type_t subtype, const char *text)
{
    expr_node_t *node = math2_alloc_node(expr, NODE_TEXT);
    if(!node) return;
    
    node->data.text.subtype = subtype;
    strncpy(node->data.text.text, text, sizeof(node->data.text.text) - 1);
    
    seq_insert_after(expr->cursor.sequence, expr->cursor.after, node);
    expr->cursor.after = node;
    
    math2_clear_modes(expr);
}

void math2_insert_fraction(math_expr2_t *expr)
{
    expr_node_t *frac = math2_alloc_node(expr, NODE_FRACTION);
    if(!frac) return;
    
    frac->data.frac.numer = math2_new_sequence(expr);
    frac->data.frac.denom = math2_new_sequence(expr);
    
    if(!frac->data.frac.numer || !frac->data.frac.denom) return;
    
    frac->data.frac.numer->parent = frac;
    frac->data.frac.denom->parent = frac;
    
    /* Collect nodes as numerator, stopping at operators */
    expr_node_t *collect_end = expr->cursor.after;
    expr_node_t *collect_start = collect_end;
    
    /* Helper to check if node is an operator */
    #define IS_OPERATOR(n) ((n)->type == NODE_TEXT && (n)->data.text.subtype == TEXT_OPERATOR)
    
    /* Walk backwards to find start of collectable sequence */
    /* Stop at operators, but include closing parens and their contents */
    while(collect_start) {
        /* Stop if current node is an operator */
        if(IS_OPERATOR(collect_start)) {
            /* Don't include the operator - start after it */
            collect_start = collect_start->next;
            break;
        }
        
        /* Check if we can include the previous node */
        expr_node_t *prev = collect_start->prev;
        if(!prev) break;  /* At start of sequence */
        
        /* Stop if previous is an operator */
        if(IS_OPERATOR(prev)) break;
        
        /* Can collect: text, numbers, parens, exponents, subscripts, fractions */
        if(prev->type == NODE_TEXT ||
           prev->type == NODE_EXPONENT ||
           prev->type == NODE_SUBSCRIPT ||
           prev->type == NODE_PAREN ||
           prev->type == NODE_FRACTION ||
           prev->type == NODE_ROOT ||
           prev->type == NODE_ABS) {
            collect_start = prev;
        } else {
            break;
        }
    }
    
    #undef IS_OPERATOR
    
    /* Move all collected nodes to numerator */
    if(collect_start && collect_end && collect_start != NULL) {
        /* Update cursor to before the collected sequence */
        expr->cursor.after = collect_start->prev;
        
        /* Move nodes one by one to numerator */
        expr_node_t *node = collect_start;
        expr_node_t *last_inserted = NULL;
        while(node) {
            expr_node_t *next = node->next;
            bool is_last = (node == collect_end);
            
            seq_remove(node);
            seq_insert_after(frac->data.frac.numer, last_inserted, node);
            last_inserted = node;
            
            if(is_last) break;
            node = next;
        }
    }
    
    seq_insert_after(expr->cursor.sequence, expr->cursor.after, frac);
    
    /* Move cursor into denominator (or numerator if empty) */
    if(seq_is_empty(frac->data.frac.numer)) {
        expr->cursor.sequence = frac->data.frac.numer;
    } else {
        expr->cursor.sequence = frac->data.frac.denom;
    }
    expr->cursor.after = NULL;
    
    math2_clear_modes(expr);
}

void math2_insert_exponent(math_expr2_t *expr)
{
    expr_node_t *exp = math2_alloc_node(expr, NODE_EXPONENT);
    if(!exp) return;
    
    exp->data.exp.base = math2_new_sequence(expr);
    exp->data.exp.power = math2_new_sequence(expr);
    
    if(!exp->data.exp.base || !exp->data.exp.power) return;
    
    exp->data.exp.base->parent = exp;
    exp->data.exp.power->parent = exp;
    
    /* Collect previous node as base */
    if(expr->cursor.after && 
       (expr->cursor.after->type == NODE_TEXT ||
        expr->cursor.after->type == NODE_FRACTION ||
        expr->cursor.after->type == NODE_PAREN ||
        expr->cursor.after->type == NODE_EXPONENT ||
        expr->cursor.after->type == NODE_ROOT)) {
        expr_node_t *prev = expr->cursor.after;
        expr->cursor.after = prev->prev;
        seq_remove(prev);
        seq_insert_after(exp->data.exp.base, NULL, prev);
    }
    
    seq_insert_after(expr->cursor.sequence, expr->cursor.after, exp);
    
    /* Move cursor into power */
    expr->cursor.sequence = exp->data.exp.power;
    expr->cursor.after = NULL;
    
    math2_clear_modes(expr);
}

void math2_insert_subscript(math_expr2_t *expr)
{
    expr_node_t *sub = math2_alloc_node(expr, NODE_SUBSCRIPT);
    if(!sub) return;
    
    sub->data.subscript.base = math2_new_sequence(expr);
    sub->data.subscript.sub = math2_new_sequence(expr);
    
    if(!sub->data.subscript.base || !sub->data.subscript.sub) return;
    
    sub->data.subscript.base->parent = sub;
    sub->data.subscript.sub->parent = sub;
    
    /* Collect previous node as base */
    if(expr->cursor.after && expr->cursor.after->type == NODE_TEXT) {
        expr_node_t *prev = expr->cursor.after;
        expr->cursor.after = prev->prev;
        seq_remove(prev);
        seq_insert_after(sub->data.subscript.base, NULL, prev);
    }
    
    seq_insert_after(expr->cursor.sequence, expr->cursor.after, sub);
    
    /* Move cursor into subscript */
    expr->cursor.sequence = sub->data.subscript.sub;
    expr->cursor.after = NULL;
    
    math2_clear_modes(expr);
}

void math2_insert_sqrt(math_expr2_t *expr)
{
    math2_insert_nthroot(expr, 2);
}

void math2_insert_nthroot(math_expr2_t *expr, int n)
{
    expr_node_t *root = math2_alloc_node(expr, NODE_ROOT);
    if(!root) return;
    
    root->data.root.index = n;
    root->data.root.content = math2_new_sequence(expr);
    
    if(!root->data.root.content) return;
    
    root->data.root.content->parent = root;
    
    seq_insert_after(expr->cursor.sequence, expr->cursor.after, root);
    
    /* Move cursor into content */
    expr->cursor.sequence = root->data.root.content;
    expr->cursor.after = NULL;
    
    math2_clear_modes(expr);
}

void math2_insert_xthroot(math_expr2_t *expr)
{
    expr_node_t *root = math2_alloc_node(expr, NODE_NTHROOT);
    if(!root) return;
    
    root->data.nthroot.index = math2_new_sequence(expr);
    root->data.nthroot.content = math2_new_sequence(expr);
    
    if(!root->data.nthroot.index || !root->data.nthroot.content) return;
    
    root->data.nthroot.index->parent = root;
    root->data.nthroot.content->parent = root;
    
    seq_insert_after(expr->cursor.sequence, expr->cursor.after, root);
    
    /* Move cursor into index first */
    expr->cursor.sequence = root->data.nthroot.index;
    expr->cursor.after = NULL;
    
    math2_clear_modes(expr);
}

void math2_insert_mixed_frac(math_expr2_t *expr)
{
    expr_node_t *mf = math2_alloc_node(expr, NODE_MIXED_FRAC);
    if(!mf) return;
    
    mf->data.mixed.whole = math2_new_sequence(expr);
    mf->data.mixed.numer = math2_new_sequence(expr);
    mf->data.mixed.denom = math2_new_sequence(expr);
    
    if(!mf->data.mixed.whole || !mf->data.mixed.numer || !mf->data.mixed.denom) return;
    
    mf->data.mixed.whole->parent = mf;
    mf->data.mixed.numer->parent = mf;
    mf->data.mixed.denom->parent = mf;
    
    seq_insert_after(expr->cursor.sequence, expr->cursor.after, mf);
    
    /* Move cursor into whole part first */
    expr->cursor.sequence = mf->data.mixed.whole;
    expr->cursor.after = NULL;
    
    math2_clear_modes(expr);
}

void math2_insert_abs(math_expr2_t *expr)
{
    expr_node_t *abs = math2_alloc_node(expr, NODE_ABS);
    if(!abs) return;
    
    abs->data.abs.content = math2_new_sequence(expr);
    
    if(!abs->data.abs.content) return;
    
    abs->data.abs.content->parent = abs;
    
    seq_insert_after(expr->cursor.sequence, expr->cursor.after, abs);
    
    /* Move cursor into content */
    expr->cursor.sequence = abs->data.abs.content;
    expr->cursor.after = NULL;
    
    math2_clear_modes(expr);
}

void math2_insert_paren(math_expr2_t *expr)
{
    expr_node_t *paren = math2_alloc_node(expr, NODE_PAREN);
    if(!paren) return;
    
    paren->data.paren.content = math2_new_sequence(expr);
    
    if(!paren->data.paren.content) return;
    
    paren->data.paren.content->parent = paren;
    
    seq_insert_after(expr->cursor.sequence, expr->cursor.after, paren);
    
    /* Move cursor into content */
    expr->cursor.sequence = paren->data.paren.content;
    expr->cursor.after = NULL;
    
    math2_clear_modes(expr);
}

void math2_insert_function(math_expr2_t *expr, const char *name)
{
    expr_node_t *func = math2_alloc_node(expr, NODE_FUNCTION);
    if(!func) return;
    
    strncpy(func->data.func.name, name, sizeof(func->data.func.name) - 1);
    func->data.func.arg = math2_new_sequence(expr);
    
    if(!func->data.func.arg) return;
    
    func->data.func.arg->parent = func;
    
    seq_insert_after(expr->cursor.sequence, expr->cursor.after, func);
    
    /* Move cursor into argument */
    expr->cursor.sequence = func->data.func.arg;
    expr->cursor.after = NULL;
    
    math2_clear_modes(expr);
}

void math2_delete(math_expr2_t *expr)
{
    if(!expr->cursor.after) {
        /* At start of sequence - try to exit if in nested */
        cursor_exit(expr);
        return;
    }
    
    expr_node_t *to_delete = expr->cursor.after;
    expr->cursor.after = to_delete->prev;
    
    seq_remove(to_delete);
    math2_free_node(expr, to_delete);
}

void math2_clear(math_expr2_t *expr)
{
    /* Free all children of root */
    expr_node_t *child = expr->root->data.seq.first;
    while(child) {
        expr_node_t *next = child->next;
        math2_free_node(expr, child);
        child = next;
    }
    expr->root->data.seq.first = NULL;
    expr->root->data.seq.last = NULL;
    
    /* Reset cursor to root */
    expr->cursor.sequence = expr->root;
    expr->cursor.after = NULL;
    
    math2_clear_modes(expr);
}

/* ===== Rendering ===== */

/* Scale dimensions by percentage */
static int scale(int val, int percent)
{
    return (val * percent) / 100;
}

/* Get text width */
static int text_width(const char *text, int font_scale)
{
    int len = strlen(text);
    return scale(CHAR_W * len, font_scale);
}

/* Get text height */
static int text_height(int font_scale)
{
    return scale(CHAR_H, font_scale);
}

/* Measure sequence width */
static metrics_t measure_sequence(expr_node_t *seq, int font_scale);

metrics_t math2_measure(expr_node_t *node, int font_scale)
{
    metrics_t m = {0, 0, 0};
    
    if(!node) return m;
    
    switch(node->type) {
        case NODE_SEQUENCE:
            return measure_sequence(node, font_scale);
            
        case NODE_TEXT:
            /* Special handling for pi symbol which isn't in font */
            if(node->data.text.subtype == TEXT_PI) {
                m.width = 10;  /* Fixed width for custom pi drawing */
                m.height = text_height(font_scale);
                m.baseline = m.height / 2;
            } else {
                m.width = text_width(node->data.text.text, font_scale);
                m.height = text_height(font_scale);
                m.baseline = m.height / 2;
            }
            break;
            
        case NODE_FRACTION:
            {
                metrics_t num = math2_measure(node->data.frac.numer, font_scale);
                metrics_t den = math2_measure(node->data.frac.denom, font_scale);
                
                /* Minimum width for empty slots */
                if(num.width < 12) num.width = 12;
                if(den.width < 12) den.width = 12;
                if(num.height < 10) num.height = 10;
                if(den.height < 10) den.height = 10;
                
                m.width = (num.width > den.width ? num.width : den.width) + 4;
                m.height = num.height + FRAC_PAD + FRAC_BAR_H + FRAC_PAD + den.height;
                m.baseline = num.height + FRAC_PAD;
            }
            break;
            
        case NODE_EXPONENT:
            {
                metrics_t base = math2_measure(node->data.exp.base, font_scale);
                /* Minimum exponent scale to prevent over-shrinking */
                int exp_scale = scale(font_scale, EXP_SCALE);
                if(exp_scale < 60) exp_scale = 60;  /* Min ~60% of normal */
                metrics_t power = math2_measure(node->data.exp.power, exp_scale);
                
                if(power.width < 8) power.width = 8;
                if(power.height < 10) power.height = 10;
                
                m.width = base.width + power.width + 4;  /* +4 spacing for nested exponents */
                m.height = base.height + power.height - 4;
                m.baseline = base.baseline + power.height - 4;
            }
            break;
            
        case NODE_SUBSCRIPT:
            {
                metrics_t base = math2_measure(node->data.subscript.base, font_scale);
                int sub_scale = scale(font_scale, EXP_SCALE);
                metrics_t sub = math2_measure(node->data.subscript.sub, sub_scale);
                
                if(sub.width < 8) sub.width = 8;
                if(sub.height < 8) sub.height = 8;
                
                m.width = base.width + sub.width;
                m.height = base.height + sub.height / 2;
                m.baseline = base.baseline;
            }
            break;
            
        case NODE_ROOT:
            {
                metrics_t content = math2_measure(node->data.root.content, font_scale);
                if(content.width < 12) content.width = 12;
                if(content.height < 10) content.height = 10;
                
                int root_w = scale(10, font_scale); /* √ symbol width */
                int index_w = 0;
                int index_h = 0;
                
                /* Add space for root index if not 2 */
                if(node->data.root.index != 2) {
                    index_w = 8;
                    index_h = 8;
                }
                
                m.width = index_w + root_w + content.width + 2;
                m.height = content.height + 4 + index_h / 2;
                m.baseline = content.baseline + 2 + index_h / 2;
            }
            break;
            
        case NODE_NTHROOT:
            {
                int idx_scale = scale(font_scale, 60);  /* Smaller index */
                metrics_t idx = math2_measure(node->data.nthroot.index, idx_scale);
                metrics_t content = math2_measure(node->data.nthroot.content, font_scale);
                
                if(idx.width < 8) idx.width = 8;
                if(idx.height < 8) idx.height = 8;
                if(content.width < 12) content.width = 12;
                if(content.height < 10) content.height = 10;
                
                int root_w = scale(10, font_scale);
                m.width = idx.width + root_w + content.width + 2;
                m.height = content.height + 4 + idx.height / 2;
                m.baseline = content.baseline + 2 + idx.height / 2;
            }
            break;
            
        case NODE_MIXED_FRAC:
            {
                metrics_t whole = math2_measure(node->data.mixed.whole, font_scale);
                metrics_t num = math2_measure(node->data.mixed.numer, font_scale);
                metrics_t den = math2_measure(node->data.mixed.denom, font_scale);
                
                if(whole.width < 8) whole.width = 8;
                if(whole.height < 10) whole.height = 10;
                if(num.width < 10) num.width = 10;
                if(den.width < 10) den.width = 10;
                if(num.height < 8) num.height = 8;
                if(den.height < 8) den.height = 8;
                
                int frac_w = (num.width > den.width ? num.width : den.width) + 4;
                int frac_h = num.height + FRAC_PAD + FRAC_BAR_H + FRAC_PAD + den.height;
                
                m.width = whole.width + 4 + frac_w;
                m.height = frac_h > whole.height ? frac_h : whole.height;
                m.baseline = num.height + FRAC_PAD;
            }
            break;
            
        case NODE_ABS:
            {
                metrics_t content = math2_measure(node->data.abs.content, font_scale);
                if(content.width < 8) content.width = 8;
                if(content.height < 10) content.height = 10;
                
                m.width = content.width + 8; /* | on each side */
                m.height = content.height + 4;
                m.baseline = content.baseline + 2;
            }
            break;
            
        case NODE_PAREN:
            {
                metrics_t content = math2_measure(node->data.paren.content, font_scale);
                if(content.width < 8) content.width = 8;
                if(content.height < 10) content.height = 10;
                
                /* Paren width scales slightly with height for taller content */
                int paren_w = 6 + (content.height > 20 ? 2 : 0);
                m.width = content.width + paren_w * 2 + 4;
                m.height = content.height + 4;  /* Parens cover full height */
                m.baseline = content.baseline + 2;
            }
            break;
            
        case NODE_FUNCTION:
            {
                int name_w = text_width(node->data.func.name, font_scale);
                metrics_t arg = math2_measure(node->data.func.arg, font_scale);
                if(arg.width < 8) arg.width = 8;
                if(arg.height < 10) arg.height = 10;
                
                m.width = name_w + 6 + arg.width + 6; /* name( arg ) */
                m.height = arg.height > text_height(font_scale) ? arg.height : text_height(font_scale);
                m.baseline = m.height / 2;
            }
            break;
            
        default:
            m.width = 8;
            m.height = 10;
            m.baseline = 5;
            break;
    }
    
    return m;
}

static metrics_t measure_sequence(expr_node_t *seq, int font_scale)
{
    metrics_t m = {0, 0, 0};
    
    if(!seq || seq->type != NODE_SEQUENCE) return m;
    
    int max_above = 0;  /* Max height above baseline */
    int max_below = 0;  /* Max height below baseline */
    
    expr_node_t *child = seq->data.seq.first;
    while(child) {
        metrics_t cm = math2_measure(child, font_scale);
        m.width += cm.width;
        
        int above = cm.baseline;
        int below = cm.height - cm.baseline;
        if(above > max_above) max_above = above;
        if(below > max_below) max_below = below;
        
        child = child->next;
    }
    
    m.height = max_above + max_below;
    m.baseline = max_above;
    
    /* Minimum size for empty sequence */
    if(m.width == 0) m.width = 8;
    if(m.height == 0) {
        m.height = text_height(font_scale);
        m.baseline = m.height / 2;
    }
    
    return m;
}

/* Draw placeholder box for empty slot */
static void draw_placeholder(int x, int y, int w, int h, bool is_cursor_here)
{
    if(is_cursor_here && g_cursor_visible) {
        drect(x, y, x + w - 1, y + h - 1, COL_CURSOR);
    } else {
        drect_border(x, y, x + w - 1, y + h - 1, C_WHITE, 1, COL_PLACEHOLDER);
    }
}

/* Check if cursor is in this sequence */
static bool cursor_in_seq(math_expr2_t *expr, expr_node_t *seq)
{
    return expr->cursor.sequence == seq;
}

/* Draw cursor line */
static void draw_cursor_line(int x, int y, int h)
{
    if(!g_cursor_visible) return;
    dline(x, y, x, y + h - 1, COL_CURSOR);
    dline(x + 1, y, x + 1, y + h - 1, COL_CURSOR);
}

/* Forward declaration */
static void draw_sequence(expr_node_t *seq, int x, int y_baseline, int font_scale,
                          math_expr2_t *expr);

void math2_draw_node(expr_node_t *node, int x, int y_baseline, int font_scale,
                     math_expr2_t *expr)
{
    if(!node) return;
    
    metrics_t m = math2_measure(node, font_scale);
    int y_top = y_baseline - m.baseline;
    
    switch(node->type) {
        case NODE_SEQUENCE:
            draw_sequence(node, x, y_baseline, font_scale, expr);
            break;
            
        case NODE_TEXT:
            /* Draw text - convert display symbols */
            if(node->data.text.subtype == TEXT_PI) {
                /* Draw pi symbol manually (font doesn't have it) */
                /* Size approximately 8x10 pixels */
                drect(x, y_top, x + 7, y_top + 1, COL_TEXT);       /* Top bar */
                drect(x + 1, y_top + 1, x + 2, y_top + 9, COL_TEXT); /* Left leg */
                drect(x + 5, y_top + 1, x + 6, y_top + 9, COL_TEXT); /* Right leg */
            } else if(node->data.text.subtype == TEXT_OPERATOR) {
                if(strcmp(node->data.text.text, "×") == 0) {
                    dtext(x, y_top, COL_TEXT, "*");
                } else if(strcmp(node->data.text.text, "÷") == 0) {
                    dtext(x, y_top, COL_TEXT, "/");
                } else {
                    dtext(x, y_top, COL_TEXT, node->data.text.text);
                }
            } else {
                dtext(x, y_top, COL_TEXT, node->data.text.text);
            }
            break;
            
        case NODE_FRACTION:
            {
                metrics_t num = math2_measure(node->data.frac.numer, font_scale);
                metrics_t den = math2_measure(node->data.frac.denom, font_scale);
                
                int bar_y = y_baseline;
                int num_y = bar_y - FRAC_PAD - num.height + num.baseline;
                int den_y = bar_y + FRAC_PAD + FRAC_BAR_H + den.baseline;
                
                /* Center numerator and denominator */
                int num_x = x + (m.width - num.width) / 2;
                int den_x = x + (m.width - den.width) / 2;
                
                /* Draw numerator */
                if(seq_is_empty(node->data.frac.numer)) {
                    draw_placeholder(num_x, bar_y - FRAC_PAD - 10, 
                                   num.width, 10,
                                   cursor_in_seq(expr, node->data.frac.numer));
                } else {
                    draw_sequence(node->data.frac.numer, num_x, num_y, font_scale, expr);
                }
                
                /* Draw fraction bar */
                drect(x + 1, bar_y, x + m.width - 2, bar_y + FRAC_BAR_H - 1, COL_FRAC_BAR);
                
                /* Draw denominator */
                if(seq_is_empty(node->data.frac.denom)) {
                    draw_placeholder(den_x, bar_y + FRAC_PAD + FRAC_BAR_H,
                                   den.width, 10,
                                   cursor_in_seq(expr, node->data.frac.denom));
                } else {
                    draw_sequence(node->data.frac.denom, den_x, den_y, font_scale, expr);
                }
            }
            break;
            
        case NODE_EXPONENT:
            {
                metrics_t base = math2_measure(node->data.exp.base, font_scale);
                int exp_scale = scale(font_scale, EXP_SCALE);
                if(exp_scale < 60) exp_scale = 60;  /* Min ~60% of normal */
                metrics_t power = math2_measure(node->data.exp.power, exp_scale);
                
                /* Draw base */
                if(seq_is_empty(node->data.exp.base)) {
                    draw_placeholder(x, y_top, 8, m.height,
                                   cursor_in_seq(expr, node->data.exp.base));
                } else {
                    draw_sequence(node->data.exp.base, x, y_baseline, font_scale, expr);
                }
                
                /* Draw power (raised) - position relative to base top */
                int power_x = x + base.width + 3;  /* +3 spacing for nested exponents */
                int power_y = y_top + power.baseline;
                
                if(seq_is_empty(node->data.exp.power)) {
                    draw_placeholder(power_x, y_top, power.width, power.height,
                                   cursor_in_seq(expr, node->data.exp.power));
                } else {
                    draw_sequence(node->data.exp.power, power_x, power_y, exp_scale, expr);
                }
            }
            break;
            
        case NODE_SUBSCRIPT:
            {
                metrics_t base = math2_measure(node->data.subscript.base, font_scale);
                int sub_scale = scale(font_scale, EXP_SCALE);
                metrics_t sub = math2_measure(node->data.subscript.sub, sub_scale);
                
                /* Draw base */
                if(seq_is_empty(node->data.subscript.base)) {
                    draw_placeholder(x, y_top, 8, base.height,
                                   cursor_in_seq(expr, node->data.subscript.base));
                } else {
                    draw_sequence(node->data.subscript.base, x, y_baseline, font_scale, expr);
                }
                
                /* Draw subscript (lowered) */
                int sub_x = x + base.width;
                int sub_y = y_baseline + sub.baseline;
                
                if(seq_is_empty(node->data.subscript.sub)) {
                    draw_placeholder(sub_x, y_baseline, sub.width, sub.height,
                                   cursor_in_seq(expr, node->data.subscript.sub));
                } else {
                    draw_sequence(node->data.subscript.sub, sub_x, sub_y, sub_scale, expr);
                }
            }
            break;
            
        case NODE_ROOT:
            {
                metrics_t content = math2_measure(node->data.root.content, font_scale);
                int root_w = scale(10, font_scale);
                int index_w = 0;
                int index_h = 0;
                
                /* Add space for root index if not 2 */
                if(node->data.root.index != 2) {
                    index_w = 8;
                    index_h = 8;
                    /* Draw index number */
                    char idx_str[4];
                    snprintf(idx_str, sizeof(idx_str), "%d", node->data.root.index);
                    dtext(x, y_top, COL_TEXT, idx_str);
                }
                
                int rx = x + index_w;
                
                /* Draw root symbol */
                int ry = y_top + index_h / 2;
                int rh = m.height - index_h / 2;
                dline(rx, y_baseline, rx + 3, ry + rh - 1, COL_TEXT);
                dline(rx + 3, ry + rh - 1, rx + root_w - 2, ry, COL_TEXT);
                dline(rx + root_w - 2, ry, x + m.width - 1, ry, COL_TEXT);
                
                /* Draw content */
                int cx = rx + root_w;
                if(seq_is_empty(node->data.root.content)) {
                    draw_placeholder(cx, ry + 2, content.width, content.height,
                                   cursor_in_seq(expr, node->data.root.content));
                } else {
                    draw_sequence(node->data.root.content, cx, y_baseline, font_scale, expr);
                }
            }
            break;
            
        case NODE_NTHROOT:
            {
                int idx_scale = scale(font_scale, 60);
                metrics_t idx = math2_measure(node->data.nthroot.index, idx_scale);
                metrics_t content = math2_measure(node->data.nthroot.content, font_scale);
                int root_w = scale(10, font_scale);
                
                if(idx.width < 8) idx.width = 8;
                if(idx.height < 8) idx.height = 8;
                
                /* Draw index */
                if(seq_is_empty(node->data.nthroot.index)) {
                    draw_placeholder(x, y_top, idx.width, idx.height,
                                   cursor_in_seq(expr, node->data.nthroot.index));
                } else {
                    draw_sequence(node->data.nthroot.index, x, y_top + idx.baseline, idx_scale, expr);
                }
                
                int rx = x + idx.width;
                
                /* Draw root symbol */
                int ry = y_top + idx.height / 2;
                int rh = m.height - idx.height / 2;
                dline(rx, y_baseline, rx + 3, ry + rh - 1, COL_TEXT);
                dline(rx + 3, ry + rh - 1, rx + root_w - 2, ry, COL_TEXT);
                dline(rx + root_w - 2, ry, x + m.width - 1, ry, COL_TEXT);
                
                /* Draw content */
                int cx = rx + root_w;
                if(seq_is_empty(node->data.nthroot.content)) {
                    draw_placeholder(cx, ry + 2, content.width, content.height,
                                   cursor_in_seq(expr, node->data.nthroot.content));
                } else {
                    draw_sequence(node->data.nthroot.content, cx, y_baseline, font_scale, expr);
                }
            }
            break;
            
        case NODE_MIXED_FRAC:
            {
                metrics_t whole = math2_measure(node->data.mixed.whole, font_scale);
                metrics_t num = math2_measure(node->data.mixed.numer, font_scale);
                metrics_t den = math2_measure(node->data.mixed.denom, font_scale);
                
                if(whole.width < 8) whole.width = 8;
                if(num.width < 10) num.width = 10;
                if(den.width < 10) den.width = 10;
                
                int frac_w = (num.width > den.width ? num.width : den.width) + 4;
                int frac_h = num.height + FRAC_PAD + FRAC_BAR_H + FRAC_PAD + den.height;
                
                /* Draw whole part */
                if(seq_is_empty(node->data.mixed.whole)) {
                    draw_placeholder(x, y_top + (m.height - whole.height) / 2, whole.width, whole.height,
                                   cursor_in_seq(expr, node->data.mixed.whole));
                } else {
                    draw_sequence(node->data.mixed.whole, x, y_baseline, font_scale, expr);
                }
                
                int fx = x + whole.width + 4;  /* Fraction x position */
                int bar_y = y_baseline - FRAC_BAR_H / 2;
                
                /* Draw numerator */
                int num_x = fx + (frac_w - num.width) / 2;
                int num_y = bar_y - FRAC_PAD - num.height + num.baseline;
                if(seq_is_empty(node->data.mixed.numer)) {
                    draw_placeholder(num_x, bar_y - FRAC_PAD - num.height, num.width, num.height,
                                   cursor_in_seq(expr, node->data.mixed.numer));
                } else {
                    draw_sequence(node->data.mixed.numer, num_x, num_y, font_scale, expr);
                }
                
                /* Draw fraction bar */
                drect(fx, bar_y, fx + frac_w - 1, bar_y + FRAC_BAR_H - 1, COL_FRAC_BAR);
                
                /* Draw denominator */
                int den_x = fx + (frac_w - den.width) / 2;
                int den_y = bar_y + FRAC_BAR_H + FRAC_PAD + den.baseline;
                if(seq_is_empty(node->data.mixed.denom)) {
                    draw_placeholder(den_x, bar_y + FRAC_BAR_H + FRAC_PAD, den.width, den.height,
                                   cursor_in_seq(expr, node->data.mixed.denom));
                } else {
                    draw_sequence(node->data.mixed.denom, den_x, den_y, font_scale, expr);
                }
            }
            break;
            
        case NODE_ABS:
            {
                metrics_t content = math2_measure(node->data.abs.content, font_scale);
                
                /* Draw | bars */
                dline(x + 2, y_top, x + 2, y_top + m.height - 1, COL_TEXT);
                dline(x + m.width - 3, y_top, x + m.width - 3, y_top + m.height - 1, COL_TEXT);
                
                /* Draw content */
                int cx = x + 4;
                if(seq_is_empty(node->data.abs.content)) {
                    draw_placeholder(cx, y_top + 2, content.width, content.height,
                                   cursor_in_seq(expr, node->data.abs.content));
                } else {
                    draw_sequence(node->data.abs.content, cx, y_baseline, font_scale, expr);
                }
            }
            break;
            
        case NODE_PAREN:
            {
                metrics_t content = math2_measure(node->data.paren.content, font_scale);
                
                /* Get color based on nesting depth (or black if coloring disabled) */
                color_t paren_color;
                if(g_color_brackets) {
                    paren_color = PAREN_COLORS[g_paren_depth % NUM_PAREN_COLORS];
                } else {
                    paren_color = COL_TEXT;
                }
                g_paren_depth++;
                
                int paren_w = 6 + (m.height > 24 ? 2 : 0);
                
                /* Draw scaled parentheses */
                if(m.height <= 20) {
                    /* Small: just draw text parens */
                    dtext(x + 2, y_top + (m.height - 14) / 2, paren_color, "(");
                    dtext(x + m.width - paren_w - 2, y_top + (m.height - 14) / 2, paren_color, ")");
                } else {
                    /* Large: draw curved parens */
                    int h = m.height - 4;
                    int left_x = x + 5;
                    int right_x = x + m.width - 6;
                    int top_y = y_top + 2;
                    int bot_y = y_top + m.height - 3;
                    (void)bot_y;  /* Unused */
                    
                    /* Curve depth based on height */
                    int curve = (h > 40) ? 4 : 3;
                    
                    /* Left paren ( - single smooth curve */
                    for(int i = 0; i <= h; i++) {
                        /* Parabolic curve: deepest at middle */
                        int dist_from_mid = (i - h/2);
                        int offset = curve - (curve * dist_from_mid * dist_from_mid * 4) / (h * h);
                        dpixel(left_x - offset, top_y + i, paren_color);
                        /* Thicken for visibility */
                        if(h > 30) dpixel(left_x - offset - 1, top_y + i, paren_color);
                    }
                    
                    /* Right paren ) - mirror of left */
                    for(int i = 0; i <= h; i++) {
                        int dist_from_mid = (i - h/2);
                        int offset = curve - (curve * dist_from_mid * dist_from_mid * 4) / (h * h);
                        dpixel(right_x + offset, top_y + i, paren_color);
                        if(h > 30) dpixel(right_x + offset + 1, top_y + i, paren_color);
                    }
                }
                
                /* Draw content */
                int cx = x + paren_w + 2;
                if(seq_is_empty(node->data.paren.content)) {
                    draw_placeholder(cx, y_top + 2, content.width, content.height,
                                   cursor_in_seq(expr, node->data.paren.content));
                } else {
                    draw_sequence(node->data.paren.content, cx, y_baseline, font_scale, expr);
                }
                
                g_paren_depth--;
            }
            break;
            
        case NODE_FUNCTION:
            {
                int name_w = text_width(node->data.func.name, font_scale);
                
                /* Draw function name */
                dtext(x, y_top, COL_TEXT, node->data.func.name);
                dtext(x + name_w, y_top, COL_TEXT, "(");
                
                /* Draw argument */
                int ax = x + name_w + 6;
                if(seq_is_empty(node->data.func.arg)) {
                    draw_placeholder(ax, y_top, 8, m.height,
                                   cursor_in_seq(expr, node->data.func.arg));
                } else {
                    draw_sequence(node->data.func.arg, ax, y_baseline, font_scale, expr);
                }
                
                dtext(x + m.width - 6, y_top, COL_TEXT, ")");
            }
            break;
            
        default:
            break;
    }
}

static void draw_sequence(expr_node_t *seq, int x, int y_baseline, int font_scale,
                          math_expr2_t *expr)
{
    if(!seq || seq->type != NODE_SEQUENCE) return;
    
    int cx = x;
    bool cursor_here = cursor_in_seq(expr, seq);
    
    /* Cursor height based on font scale */
    int cursor_h = (CHAR_H * font_scale) / 100 + 4;
    int cursor_offset = cursor_h / 2;
    
    /* Draw cursor at start if needed */
    if(cursor_here && expr->cursor.after == NULL) {
        draw_cursor_line(cx, y_baseline - cursor_offset, cursor_h);
        cx += 3;
    }
    
    expr_node_t *child = seq->data.seq.first;
    while(child) {
        metrics_t cm = math2_measure(child, font_scale);
        math2_draw_node(child, cx, y_baseline, font_scale, expr);
        cx += cm.width;
        
        /* Draw cursor after this node if needed */
        if(cursor_here && expr->cursor.after == child) {
            draw_cursor_line(cx, y_baseline - cursor_offset, cursor_h);
            cx += 3;
        }
        
        child = child->next;
    }
}

void math2_draw(math_expr2_t *expr, int x, int y)
{
    metrics_t m = math2_measure(expr->root, 100);
    /* Center vertically around y */
    int y_baseline = y + m.baseline;
    draw_sequence(expr->root, x, y_baseline, 100, expr);
}

/* Get the total width of the expression for centering */
int math2_get_width(math_expr2_t *expr)
{
    metrics_t m = math2_measure(expr->root, 100);
    return m.width;
}

/* Get the total height of the expression */
int math2_get_height(math_expr2_t *expr)
{
    metrics_t m = math2_measure(expr->root, 100);
    return m.height;
}

/* ===== LaTeX Generation ===== */

static void latex_sequence(expr_node_t *seq, char *buf);
static void latex_node(expr_node_t *node, char *buf);

static void latex_node(expr_node_t *node, char *buf)
{
    if(!node) return;
    
    switch(node->type) {
        case NODE_SEQUENCE:
            latex_sequence(node, buf);
            break;
            
        case NODE_TEXT:
            switch(node->data.text.subtype) {
                case TEXT_PI:
                    strcat(buf, "\\pi ");
                    break;
                case TEXT_OPERATOR:
                    if(strcmp(node->data.text.text, "×") == 0) {
                        strcat(buf, "*");
                    } else if(strcmp(node->data.text.text, "÷") == 0) {
                        strcat(buf, "/");
                    } else {
                        strcat(buf, node->data.text.text);
                    }
                    break;
                default:
                    strcat(buf, node->data.text.text);
                    break;
            }
            break;
            
        case NODE_FRACTION:
            strcat(buf, "\\frac{");
            latex_sequence(node->data.frac.numer, buf);
            strcat(buf, "}{");
            latex_sequence(node->data.frac.denom, buf);
            strcat(buf, "}");
            break;
            
        case NODE_EXPONENT:
            strcat(buf, "{");
            latex_sequence(node->data.exp.base, buf);
            strcat(buf, "}^{");
            latex_sequence(node->data.exp.power, buf);
            strcat(buf, "}");
            break;
            
        case NODE_SUBSCRIPT:
            strcat(buf, "{");
            latex_sequence(node->data.subscript.base, buf);
            strcat(buf, "}_{");
            latex_sequence(node->data.subscript.sub, buf);
            strcat(buf, "}");
            break;
            
        case NODE_ROOT:
            if(node->data.root.index == 2) {
                strcat(buf, "\\sqrt{");
            } else {
                char tmp[16];
                snprintf(tmp, sizeof(tmp), "\\sqrt[%d]{", node->data.root.index);
                strcat(buf, tmp);
            }
            latex_sequence(node->data.root.content, buf);
            strcat(buf, "}");
            break;
            
        case NODE_ABS:
            strcat(buf, "\\left|");
            latex_sequence(node->data.abs.content, buf);
            strcat(buf, "\\right|");
            break;
            
        case NODE_PAREN:
            strcat(buf, "\\left(");
            latex_sequence(node->data.paren.content, buf);
            strcat(buf, "\\right)");
            break;
            
        case NODE_FUNCTION:
            strcat(buf, "\\");
            strcat(buf, node->data.func.name);
            strcat(buf, "\\left(");
            latex_sequence(node->data.func.arg, buf);
            strcat(buf, "\\right)");
            break;
            
        case NODE_NTHROOT:
            strcat(buf, "\\sqrt[");
            latex_sequence(node->data.nthroot.index, buf);
            strcat(buf, "]{");
            latex_sequence(node->data.nthroot.content, buf);
            strcat(buf, "}");
            break;
            
        case NODE_MIXED_FRAC:
            /* Mixed fraction as whole + frac */
            latex_sequence(node->data.mixed.whole, buf);
            strcat(buf, "\\frac{");
            latex_sequence(node->data.mixed.numer, buf);
            strcat(buf, "}{");
            latex_sequence(node->data.mixed.denom, buf);
            strcat(buf, "}");
            break;
            
        default:
            break;
    }
}

static void latex_sequence(expr_node_t *seq, char *buf)
{
    if(!seq || seq->type != NODE_SEQUENCE) return;
    
    expr_node_t *child = seq->data.seq.first;
    while(child) {
        latex_node(child, buf);
        child = child->next;
    }
}

void math2_to_latex(math_expr2_t *expr)
{
    expr->latex[0] = '\0';
    latex_sequence(expr->root, expr->latex);
}

/* ===== Mode Management ===== */

void math2_toggle_shift(math_expr2_t *expr)
{
    expr->shift_mode = !expr->shift_mode;
    expr->alpha_mode = false;
}

void math2_toggle_alpha(math_expr2_t *expr)
{
    expr->alpha_mode = !expr->alpha_mode;
    expr->shift_mode = false;
}

void math2_clear_modes(math_expr2_t *expr)
{
    expr->shift_mode = false;
    expr->alpha_mode = false;
}
