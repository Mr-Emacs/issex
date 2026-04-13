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
    TEOF,
    TCOUNT,
} token_kind_t;

typedef struct {
    token_kind_t as;
    String_View name;
} token_t;

typedef struct {
    String_View source;
    size_t position;
} lexer_t;

static const char *token_kind_name(token_kind_t as);
static void print_token(token_t tk);
static char lexer_peek(lexer_t *l);
static char lexer_advance(lexer_t *l);
static token_t token_next(lexer_t *l);

static const char *token_kind_name(token_kind_t as)
{
    switch(as) {
        case TLPARENT: return "LPARENT";
        case TRPARENT: return "RPARENT";
        case TEQUAL:   return "EQUAL";
        case TIDENT:   return "IDENT";
        case TNUMBER:  return "NUMBER";
        case TDOT:     return "DOT";
        case TEOF:     return "EOF";
        case TCOUNT: default: return NULL;
    }
    static_assert(TCOUNT == 7 && "The token count Error");
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
    while(l->position - l->source.count &&
          isspace((unsigned char)lexer_peek(l))) {
              lexer_advance(l);
    }
}

static token_t token_next(lexer_t *l)
{
    lexer_skip_whitespace(l);

    if (l->position >= l->source.count) {
        return (token_t) { .as = TEOF };
    }
    char c = lexer_peek(l);
    if (c == '=') { lexer_advance(l); return (token_t) { .as = TEQUAL   }; }
    if (c == '.') { lexer_advance(l); return (token_t) { .as = TDOT     }; }
    if (c == '(') { lexer_advance(l); return (token_t) { .as = TLPARENT }; }
    if (c == ')') { lexer_advance(l); return (token_t) { .as = TRPARENT }; }

    if (isdigit((unsigned char)c)) {
        size_t start = l->position;
        while (isdigit((unsigned char)lexer_peek(l))) lexer_advance(l);
        return (token_t) { .as = TNUMBER, .name = sv_from_parts(l->source.data + start, l->position - start)};
    }
    if (isalpha((unsigned char)c)) {
        size_t start = l->position;
        while (isalpha((unsigned char)lexer_peek(l)) || isdigit((unsigned char)lexer_peek(l)) || lexer_peek(l) == '_') lexer_advance(l);
        return (token_t) { .as = TIDENT, .name = sv_from_parts(l->source.data + start, l->position - start)};
    }
    nob_log(ERROR, "Unexpected char '%c'\n", c);
    lexer_advance(l);
    return (token_t) { .as = TEOF };
}

int main(void)
{
    lexer_t lexer = {
        .source   = sv_from_cstr(".p =50"),
        .position = 0,
    };

    printf(SV_Fmt"\n", SV_Arg(lexer.source));
    while (1) {
        token_t token = token_next(&lexer);
        print_token(token);
        if (token.as == TEOF) break;
    }
    return 0;
}
