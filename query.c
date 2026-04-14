#define NOB_NO_ECHO
#define NOB_IMPLEMENTATION
#include "nob.h"

#include <ctype.h>

typedef enum {
    TLPARENT,
    TRPARENT,
    TEQUAL,
    TNUMBER,
    TIDENT,
    TDOT,
    TNOT,
    TAMPRESAND,
    TBAR,
    TGREAT,
    TLESS,
    TEOF,
    TCOUNT,
} token_kind_t;

typedef struct {
    token_kind_t as;
    String_View name;
    size_t pos;
} token_t;

typedef struct {
    String_View source;
    size_t position;
} lexer_t;

typedef enum {
    NODE_TAG,
    NODE_PRIORITY,
    NODE_STATUS,
    NODE_AND,
    NODE_OR,
    NODE_NOT,
    NODE_GREATER,
    NODE_LESSER,
    NODE_COUNT,
} node_kind_t;

typedef struct node_t node_t;
typedef struct {
    node_t *lhs;
    node_t *rhs;
} binop_t;

typedef struct {
    bool status;
    node_t *child;
} status_node_t;

typedef struct {
    size_t value;
    node_t *child;
} priority_node_t;

typedef struct {
    node_t *child;
} unary_node_t;

struct node_t {
    node_kind_t as;
    union {
        binop_t binary;
        String_View  tag;
        priority_node_t prio;
        status_node_t stat;
        unary_node_t unary;
    };
};

typedef struct {
    int pp;
    FILE *f;
    const char *stdout_path;
} node_opt_t;

typedef struct {
    lexer_t *lex;
    token_t cur;
} parser_t;

static const char *token_kind_name(token_kind_t as);
static void print_token(token_t tk);
static char lexer_peek(lexer_t *l);
static char lexer_advance(lexer_t *l);
static token_t token_next(lexer_t *l);
static void parse_error_at(parser_t *p, token_t tok, const char *msg);

static node_t *make_node(node_kind_t type);
static void parser_advance(parser_t *p);
static bool parse_expect(parser_t *p, token_kind_t kind);

node_t *parse_expr(parser_t *p);
node_t *parse_atom(parser_t *p);
node_t *parse_binary_operation(parser_t *p);
node_t *parse_and(parser_t *p);

void node_print_opt(node_t *node, node_opt_t opt);

static const char *token_kind_name(token_kind_t as)
{
    static_assert(TCOUNT == 12 && "The token count Error");
    switch(as) {
        case TLPARENT:    return "LPARENT";
        case TRPARENT:    return "RPARENT";
        case TEQUAL:      return "EQUAL";
        case TIDENT:      return "IDENT";
        case TNUMBER:     return "NUMBER";
        case TDOT:        return "DOT";
        case TNOT:        return "NOT";
        case TAMPRESAND:  return "AMPRESAND";
        case TBAR:        return "BAR";
        case TEOF:        return "EOF";
        case TGREAT:      return "GREATER";
        case TLESS:       return "LESSER";
        case TCOUNT: default: return NULL;
    }
}

static void print_token(token_t tk)
{
    if (tk.name.count > 0) {
        printf("%-8s \""SV_Fmt"\"\n", token_kind_name(tk.as), SV_Arg(tk.name));
    } else {
        printf("%s\n", token_kind_name(tk.as));
    }
}

static char lexer_peek(lexer_t *l)
{
    return (l->position < l->source.count) ? l->source.data[l->position] : '\0';
}


static char lexer_peek_n(lexer_t *l, size_t n)
{
    return (l->position + n < l->source.count) ? l->source.data[l->position + n] : '\0';
}

static char lexer_advance(lexer_t *l)
{
    return l->source.data[l->position++];
}

static void lexer_skip_whitespace(lexer_t *l)
{
    while(l->position < l->source.count &&
          isspace((unsigned char)lexer_peek(l))) {
              lexer_advance(l);
    }
}

static token_t token_next(lexer_t *l)
{
    lexer_skip_whitespace(l);

    if (l->position >= l->source.count) {
        size_t p = l->position;
        return (token_t) { .as = TEOF , .pos = p };
    }
    char c = lexer_peek(l);
    if (c == '=') { size_t p = l->position; lexer_advance(l); return (token_t) { .as = TEQUAL    , .pos = p }; }
    if (c == '.') { size_t p = l->position; lexer_advance(l); return (token_t) { .as = TDOT      , .pos = p }; }
    if (c == '(') { size_t p = l->position; lexer_advance(l); return (token_t) { .as = TLPARENT  , .pos = p }; }
    if (c == ')') { size_t p = l->position; lexer_advance(l); return (token_t) { .as = TRPARENT  , .pos = p }; }
    if (c == '!') { size_t p = l->position; lexer_advance(l); return (token_t) { .as = TNOT      , .pos = p }; }
    if (c == '&') { size_t p = l->position; lexer_advance(l); return (token_t) { .as = TAMPRESAND, .pos = p }; }
    if (c == '|') { size_t p = l->position; lexer_advance(l); return (token_t) { .as = TBAR      , .pos = p }; }
    if (c == '>') { size_t p = l->position; lexer_advance(l); return (token_t) { .as = TGREAT    , .pos = p }; }
    if (c == '<') { size_t p = l->position; lexer_advance(l); return (token_t) { .as = TLESS     , .pos = p }; }

    if (isdigit((unsigned char)c)) {
        size_t start = l->position;
        while (isdigit((unsigned char)lexer_peek(l))) lexer_advance(l);
        return (token_t) { .as = TNUMBER, .name = sv_from_parts(l->source.data + start, l->position - start), .pos = start };
    }

    if (isalpha((unsigned char)c)) {
        size_t start = l->position;
        while (isalpha((unsigned char)lexer_peek(l)) || isdigit((unsigned char)lexer_peek(l)) || lexer_peek(l) == '_') lexer_advance(l);
        return (token_t) { .as = TIDENT, .name = sv_from_parts(l->source.data + start, l->position - start), .pos = start };
    }

    nob_log(ERROR, "Unexpected char '%c'", c);
    size_t pos = l->position;
    lexer_advance(l);
    return (token_t) { .as = TEOF , .pos = pos };
}

static node_t *make_node(node_kind_t kind)
{
    node_t *node = calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->as = kind;
    return node;
}

static void parser_advance(parser_t *p)
{
    p->cur = token_next(p->lex);
}

static void parse_error_at(parser_t *p, token_t tok, const char *msg)
{
    String_View src = p->lex->source;
    nob_log(NOB_ERROR, "%s", msg);
    fprintf(stderr, "    "SV_Fmt"\n", SV_Arg(src));
    fprintf(stderr, "    %*s^\n", (int)tok.pos, "");
}

// TODO: @Mr-Emacs:
// 01:52:25: Make error message not use token name but rather more description

static bool parse_expect(parser_t *p, token_kind_t kind)
{
    if (p->cur.as != kind) {
        const char *msg = nob_temp_sprintf("Expected %s but got %s\n",
                          token_kind_name(kind), token_kind_name(p->cur.as));
        parse_error_at(p, p->cur, msg);
        return false;
    }
    return true;
}

node_t *parse_and(parser_t *p)
{
    node_t *lhs = parse_atom(p);
    if (!lhs) return NULL;

    while (p->cur.as == TAMPRESAND) {
        parser_advance(p);
        node_t *rhs = parse_atom(p);

        if (!rhs) return NULL;
        node_t *node = make_node(NODE_AND);
        node->binary.lhs = lhs;
        node->binary.rhs = rhs;
        lhs = node;
    }
    return lhs;
}

node_t *parse_binary_operation(parser_t *p)
{
    node_t *lhs = parse_and(p);
    if (!lhs) return NULL;

    while (p->cur.as == TBAR) {
        parser_advance(p);
        node_t *rhs = parse_and(p);

        if (!rhs) return NULL;
        node_t *node = make_node(NODE_OR);
        node->binary.lhs = lhs;
        node->binary.rhs = rhs;
        lhs = node;
    }
    return lhs;
}

node_t *parse_atom(parser_t *p)
{

    node_t *node = NULL;
    node_kind_t kind;
    if(p->cur.as == TIDENT) {
        node = make_node(NODE_TAG);
        node->tag = p->cur.name;
        if (sv_eq(node->tag, sv_from_cstr("p"))) kind = NODE_PRIORITY;
        else if (sv_eq(node->tag, sv_from_cstr("s"))) kind = NODE_STATUS;
        else kind = NODE_TAG;
        parser_advance(p);
    }
    else if(p->cur.as == TLPARENT) {
        parser_advance(p);
        node = parse_expr(p);
        if (!parse_expect(p, TRPARENT)) return NULL;
        parser_advance(p);
    } else {
        const char *msg = nob_temp_sprintf("Unexpected %s in the atom branch\n",
                          token_kind_name(p->cur.as));
        parse_error_at(p, p->cur, msg);
        return NULL;
    }
    if (p->cur.as == TEQUAL || p->cur.as == TGREAT || p->cur.as == TLESS) {
        token_kind_t op = p->cur.as;
        parser_advance(p);
        if (!parse_expect(p, TNUMBER)) return NULL;

        if (kind == NODE_PRIORITY) {
            node_kind_t kind =   (op == TGREAT) ? NODE_GREATER
                               : (op == TLESS)  ? NODE_LESSER
                               : NODE_PRIORITY;

            node_t *node2 = make_node(kind);
            size_t num = 0;
            for (size_t i = 0; i < p->cur.name.count; i++) {
                num = num * 10 + (size_t)(p->cur.name.data[i] - '0');
            }
            node2->prio.value = num;
            node2->prio.child = node;
            parser_advance(p);
            node = node2;
        }
        else if (kind == NODE_STATUS) {
            node_kind_t kind =   (op == TGREAT) ? NODE_GREATER
                               : (op == TLESS)  ? NODE_LESSER
                               : NODE_STATUS;

            node_t *node2 = make_node(kind);

            size_t num = 0;
            for (size_t i = 0; i < p->cur.name.count; i++) {
                num = num * 10 + (size_t)(p->cur.name.data[i] - '0');
            }

            node2->stat.status = (num != 0);
            node2->stat.child = node;
            parser_advance(p);
            node = node2;
        }
    }
    return node;
}

node_t *parse_expr(parser_t *p)
{
    bool negate = false;
    if (p->cur.as == TNOT) {
        negate = true;
        parser_advance(p);
    }

    node_t *lhs = parse_binary_operation(p);
    if (!lhs) return NULL;

    if (negate) {
        node_t *node = make_node(NODE_NOT);
        node->unary.child = lhs;
        lhs = node;
    }
    return lhs;
}

static void node_print_binop_labeled(node_t *node, node_opt_t opt, FILE *f, const char *label)
{
    fprintf(f, "%*s%s(\n", opt.pp, "", label);
    opt.pp += 4;
    node_print_opt(node->binary.lhs, opt);
    node_print_opt(node->binary.rhs, opt);
    opt.pp -= 4;
    fprintf(f, "%*s)\n", opt.pp, "");
}

static void node_print_unop_labeled(node_t *node, node_opt_t opt, FILE *f, const char *label)
{
    fprintf(f, "%*s%s(\n", opt.pp, "", label);
    opt.pp += 4;
    node_print_opt(node->unary.child, opt);
    opt.pp -= 4;
    fprintf(f, "%*s)\n", opt.pp, "");

}

void node_print_opt(node_t *node, node_opt_t opt)
{
    static_assert(NODE_COUNT == 8 && "There is an error in the node count");
    if (!node) return;

    FILE *f = opt.f ? opt.f : stdout;
    if (opt.stdout_path){
        f = fopen(opt.stdout_path, "wb");
        opt.f = f;
        opt.stdout_path = NULL;
    }
    switch(node->as) {
        case NODE_TAG: {
            fprintf(f, "%*sTAG: "SV_Fmt"\n", opt.pp, "", SV_Arg(node->tag));
        } break;
        case NODE_PRIORITY: {
            fprintf(f, "%*sPRIORITY: %zu tag: "SV_Fmt"\n", opt.pp, "",node->prio.value, SV_Arg(node->prio.child->tag));
        } break;
        case NODE_STATUS: {
            char *state = node->stat.status ? "OPEN" : "CLOSED";
            fprintf(f, "%*sSTATUS: %s tag: "SV_Fmt"\n", opt.pp, "", state, SV_Arg(node->stat.child->tag));
        } break;
        case NODE_NOT: {
            node_print_unop_labeled(node, opt, f, "NOT");
        } break;
        case NODE_GREATER: {
            fprintf(f, "%*sGREATER: %zu tag: "SV_Fmt"\n", opt.pp, "",node->prio.value, SV_Arg(node->prio.child->tag));
        } break;
        case NODE_LESSER: {
            fprintf(f, "%*sLESSER: %zu tag: "SV_Fmt"\n", opt.pp, "",node->prio.value, SV_Arg(node->prio.child->tag));
        } break;
        case NODE_AND: {
            node_print_binop_labeled(node, opt, f, "AND");
        } break;
        case NODE_OR: {
            node_print_binop_labeled(node, opt, f, "OR");
        } break;
        case NODE_COUNT: default: {
            nob_log(NOB_ERROR, "Unexpected node type %d", node->as);
        } break;
    }
}

#define node_print(node, ...) node_print_opt((node), (node_opt_t) { __VA_ARGS__ } )

int main(void)
{
    lexer_t lexer = {
        .source   = sv_from_cstr("!(p<30 * (!s=0) | p>20)"),
        .position = 0,
    };

    parser_t parser = { .lex = &lexer };
    parser_advance(&parser);
    node_t *tree = parse_expr(&parser);

    node_print(tree);
    return 0;
}
