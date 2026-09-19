/* Analisador sintatico (parser) da linguagem MINIC.
 *
 * Uso:
 *   ./parser arquivo.minic
 *   ./parser arquivo.c
 *
 * Le o arquivo de entrada, executa a analise lexica e a analise sintatica
 * (descida recursiva) e imprime a arvore sintatica (AST) resultante como
 * uma S-expression compacta na saida padrao (ex.: "Program(VarDecl(int
 * x))"). Se houver um erro sintatico, imprime uma linha "ERRO SINTATICO:
 * ..." e termina com codigo diferente de zero.
 *
 * Este arquivo inclui seu proprio analisador lexico (a mesma
 * implementacao de scanner.c), para que o parser funcione sozinho, sem
 * depender de nenhum outro arquivo do projeto.
 */

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Analisador lexico (identico, em espirito, ao scanner.c do projeto) */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *src;
    long n;
    long i;
    int line;
    int column;
} Scanner;

static int is_ident_start(int c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident_continue(int c) { return isalnum((unsigned char)c) || c == '_'; }
static int is_delimiter_stop(int c) { return strchr("(){}[],;+-*/%<>=!&|.", c) != NULL; }

static int sc_peek(Scanner *s, long offset) {
    long j = s->i + offset;
    if (j < 0 || j >= s->n) return -1;
    return (unsigned char)s->src[j];
}

static int sc_advance(Scanner *s) {
    int c = (unsigned char)s->src[s->i];
    s->i += 1;
    if (c == '\n') { s->line += 1; s->column = 1; }
    else { s->column += 1; }
    return c;
}

typedef struct { const char *word; const char *token; } Keyword;
static const Keyword KEYWORDS[] = {
    {"int", "INT"}, {"float", "FLOAT"}, {"bool", "BOOL"}, {"char", "CHAR"},
    {"void", "VOID"}, {"if", "IF"}, {"else", "ELSE"}, {"while", "WHILE"},
    {"for", "FOR"}, {"return", "RETURN"}, {"break", "BREAK"},
    {"continue", "CONTINUE"}, {"true", "TRUE"}, {"false", "FALSE"},
    {"print", "PRINT"}, {"read", "READ"},
};
#define N_KEYWORDS (sizeof(KEYWORDS) / sizeof(KEYWORDS[0]))

static const char *lookup_keyword(const char *lexeme, long len) {
    for (size_t k = 0; k < N_KEYWORDS; k++) {
        if ((long)strlen(KEYWORDS[k].word) == len &&
            strncmp(KEYWORDS[k].word, lexeme, (size_t)len) == 0) {
            return KEYWORDS[k].token;
        }
    }
    return NULL;
}

typedef struct { const char *op; const char *token; } Operator2;
static const Operator2 COMPOUND[] = {
    {"==", "EQ"}, {"!=", "NE"}, {"<=", "LE"}, {">=", "GE"},
    {"&&", "AND"}, {"||", "OR"},
};
#define N_COMPOUND (sizeof(COMPOUND) / sizeof(COMPOUND[0]))

static const char *single_operator_token(int c) {
    switch (c) {
        case '=': return "ASSIGN"; case '<': return "LT"; case '>': return "GT";
        case '!': return "NOT"; case '+': return "PLUS"; case '-': return "MINUS";
        case '*': return "STAR"; case '/': return "SLASH"; case '%': return "PERCENT";
        case '(': return "LPAREN"; case ')': return "RPAREN";
        case '{': return "LBRACE"; case '}': return "RBRACE";
        case '[': return "LBRACKET"; case ']': return "RBRACKET";
        case ',': return "COMMA"; case ';': return "SEMICOLON"; case '.': return "DOT";
        default: return NULL;
    }
}

typedef enum { ATTR_NULL, ATTR_INT, ATTR_FLOAT, ATTR_STR } AttrKind;

typedef struct {
    const char *type;
    const char *lexeme;
    long lexeme_len;
    AttrKind attr_kind;
    long attr_int;
    double attr_float;
    const char *attr_str;
    long attr_str_len;
    int line, column;
} Token;

typedef struct { Token *items; long count; long capacity; } TokenList;

static void tl_push(TokenList *list, Token t) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 256 : list->capacity * 2;
        list->items = realloc(list->items, (size_t)list->capacity * sizeof(Token));
    }
    list->items[list->count++] = t;
}

static void skip_ws_and_comments(Scanner *s) {
    while (s->i < s->n) {
        int c = sc_peek(s, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { sc_advance(s); continue; }
        if (c == '/' && sc_peek(s, 1) == '/') {
            while (s->i < s->n && sc_peek(s, 0) != '\n') sc_advance(s);
            continue;
        }
        if (c == '/' && sc_peek(s, 1) == '*') {
            sc_advance(s); sc_advance(s);
            while (s->i < s->n) {
                if (sc_peek(s, 0) == '*' && sc_peek(s, 1) == '/') { sc_advance(s); sc_advance(s); break; }
                sc_advance(s);
            }
            continue;
        }
        break;
    }
}

static Token make_token(const char *type, const char *lexeme, long len, int line, int column) {
    Token t; memset(&t, 0, sizeof(t));
    t.type = type; t.lexeme = lexeme; t.lexeme_len = len;
    t.attr_kind = ATTR_NULL; t.line = line; t.column = column;
    return t;
}

static Token scan_identifier_or_keyword(Scanner *s, int line, int column) {
    long start = s->i;
    while (s->i < s->n && is_ident_continue(sc_peek(s, 0))) sc_advance(s);
    long len = s->i - start;
    const char *lexeme = s->src + start;
    const char *keyword = lookup_keyword(lexeme, len);
    if (keyword != NULL) return make_token(keyword, lexeme, len, line, column);
    Token t = make_token("IDENT", lexeme, len, line, column);
    t.attr_kind = ATTR_STR; t.attr_str = lexeme; t.attr_str_len = len;
    return t;
}

static long parse_long(const char *s, long len) {
    char buf[64];
    long copy = len < (long)sizeof(buf) - 1 ? len : (long)sizeof(buf) - 1;
    memcpy(buf, s, (size_t)copy); buf[copy] = '\0';
    return atol(buf);
}

static double parse_double(const char *s, long len) {
    char buf[128];
    long copy = len < (long)sizeof(buf) - 1 ? len : (long)sizeof(buf) - 1;
    memcpy(buf, s, (size_t)copy); buf[copy] = '\0';
    return atof(buf);
}

static Token scan_number(Scanner *s, int line, int column) {
    long start = s->i;
    while (s->i < s->n && isdigit((unsigned char)sc_peek(s, 0))) sc_advance(s);
    long int_len = s->i - start;
    const char *int_part = s->src + start;

    if (sc_peek(s, 0) == '.' && isdigit((unsigned char)sc_peek(s, 1))) {
        sc_advance(s);
        while (s->i < s->n && isdigit((unsigned char)sc_peek(s, 0))) sc_advance(s);
        long len = s->i - start;
        Token t = make_token("FLOAT_LIT", s->src + start, len, line, column);
        t.attr_kind = ATTR_FLOAT; t.attr_float = parse_double(s->src + start, len);
        return t;
    }
    Token t = make_token("INT_LIT", int_part, int_len, line, column);
    t.attr_kind = ATTR_INT; t.attr_int = parse_long(int_part, int_len);
    return t;
}

static Token scan_string(Scanner *s, int line, int column) {
    long start = s->i;
    long j = s->i + 1;
    long closing = -1;
    while (j < s->n && s->src[j] != '\n') {
        if (s->src[j] == '"') { closing = j; break; }
        j++;
    }
    if (closing != -1) {
        sc_advance(s);
        long content_start = s->i;
        while (s->i < closing) sc_advance(s);
        long content_len = s->i - content_start;
        sc_advance(s);
        long total_len = s->i - start;
        Token t = make_token("STRING_LIT", s->src + start, total_len, line, column);
        t.attr_kind = ATTR_STR; t.attr_str = s->src + content_start; t.attr_str_len = content_len;
        return t;
    }
    sc_advance(s);
    while (s->i < s->n && sc_peek(s, 0) != '\n' && !is_delimiter_stop(sc_peek(s, 0))) sc_advance(s);
    return make_token(NULL, NULL, 0, line, column);
}

static Token scan_char(Scanner *s, int line, int column) {
    long start = s->i;
    sc_advance(s);
    int has_content = 0;
    if (s->i < s->n && sc_peek(s, 0) != '\n') { sc_advance(s); has_content = 1; }
    if (has_content && sc_peek(s, 0) == '\'') {
        sc_advance(s);
        long len = s->i - start;
        Token t = make_token("CHAR_LIT", s->src + start, len, line, column);
        t.attr_kind = ATTR_STR; t.attr_str = s->src + start + 1; t.attr_str_len = 1;
        return t;
    }
    while (s->i < s->n && sc_peek(s, 0) != '\n') sc_advance(s);
    if (s->i < s->n) sc_advance(s);
    return make_token(NULL, NULL, 0, line, column);
}

static Token scan_operator_or_symbol(Scanner *s, int line, int column) {
    int c0 = sc_peek(s, 0);
    int c1 = sc_peek(s, 1);
    if (c1 != -1) {
        for (size_t k = 0; k < N_COMPOUND; k++) {
            if ((char)c0 == COMPOUND[k].op[0] && (char)c1 == COMPOUND[k].op[1]) {
                const char *lexeme = s->src + s->i;
                sc_advance(s); sc_advance(s);
                return make_token(COMPOUND[k].token, lexeme, 2, line, column);
            }
        }
    }
    if (c0 == '&' || c0 == '|') { sc_advance(s); return make_token(NULL, NULL, 0, line, column); }
    const char *token = single_operator_token(c0);
    if (token != NULL) {
        const char *lexeme = s->src + s->i;
        sc_advance(s);
        return make_token(token, lexeme, 1, line, column);
    }
    sc_advance(s);
    return make_token(NULL, NULL, 0, line, column);
}

static TokenList lex_all(Scanner *s) {
    TokenList list; memset(&list, 0, sizeof(list));
    for (;;) {
        skip_ws_and_comments(s);
        if (s->i >= s->n) { tl_push(&list, make_token("EOF", "", 0, s->line, s->column)); return list; }
        int line = s->line, column = s->column;
        int c = sc_peek(s, 0);
        Token t;
        if (is_ident_start(c)) t = scan_identifier_or_keyword(s, line, column);
        else if (isdigit((unsigned char)c)) t = scan_number(s, line, column);
        else if (c == '"') t = scan_string(s, line, column);
        else if (c == '\'') t = scan_char(s, line, column);
        else t = scan_operator_or_symbol(s, line, column);
        if (t.type != NULL) tl_push(&list, t);
    }
}

/* ------------------------------------------------------------------ */
/* Parser (descida recursiva), montando a S-expression em memoria      */
/* ------------------------------------------------------------------ */

#define TYPE_IS(t) ((t) && (strcmp((t), "INT") == 0 || strcmp((t), "FLOAT") == 0 || \
                             strcmp((t), "BOOL") == 0 || strcmp((t), "CHAR") == 0 || \
                             strcmp((t), "VOID") == 0))

typedef struct {
    TokenList tokens;
    long pos;
    jmp_buf error_jmp;
    char error_message[512];
    Token error_token;
} ParserState;

static Token *ps_current(ParserState *p) { return &p->tokens.items[p->pos]; }
static int ps_check(ParserState *p, const char *type) { return strcmp(ps_current(p)->type, type) == 0; }

static Token *ps_advance(ParserState *p) {
    Token *t = &p->tokens.items[p->pos];
    if (strcmp(t->type, "EOF") != 0) p->pos += 1;
    return t;
}

static void ps_fail(ParserState *p, Token *tok, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(p->error_message, sizeof(p->error_message), fmt, args);
    va_end(args);
    p->error_token = *tok;
    longjmp(p->error_jmp, 1);
}

static Token *ps_expect(ParserState *p, const char *type, const char *what) {
    if (ps_check(p, type)) return ps_advance(p);
    Token *tok = ps_current(p);
    ps_fail(p, tok, "esperado %s, encontrado '%.*s' (%s)", what,
            (int)tok->lexeme_len, tok->lexeme ? tok->lexeme : "", tok->type);
    return NULL;
}

static Token *ps_expect_type(ParserState *p) {
    Token *tok = ps_current(p);
    if (TYPE_IS(tok->type)) return ps_advance(p);
    ps_fail(p, tok, "esperado um tipo (int, float, bool, char, void), encontrado '%.*s' (%s)",
            (int)tok->lexeme_len, tok->lexeme ? tok->lexeme : "", tok->type);
    return NULL;
}

/* Buffer de texto de crescimento dinamico, usado para montar os
 * fragmentos de S-expression sem depender de tamanhos fixos. */
typedef struct { char *data; size_t len; size_t cap; } Buf;

static void buf_init(Buf *b) { b->data = malloc(64); b->data[0] = '\0'; b->len = 0; b->cap = 64; }

static void buf_ensure(Buf *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
}

static void buf_append(Buf *b, const char *text, long len) {
    if (len < 0) len = (long)strlen(text);
    buf_ensure(b, (size_t)len);
    memcpy(b->data + b->len, text, (size_t)len);
    b->len += (size_t)len;
    b->data[b->len] = '\0';
}

static void buf_appendf(Buf *b, const char *fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n < (int)sizeof(tmp)) {
        buf_append(b, tmp, n);
    } else {
        char *big = malloc((size_t)n + 1);
        va_start(args, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, args);
        va_end(args);
        buf_append(b, big, n);
        free(big);
    }
}

static void buf_free(Buf *b) { free(b->data); b->data = NULL; }

/* 'kind' identifica a forma da expressao reconhecida, usado apenas
 * para validar o alvo de uma atribuicao (precisa ser Ident ou Index). */
typedef enum { KIND_IDENT, KIND_INDEX, KIND_OTHER } ExprKind;

typedef struct { Buf buf; ExprKind kind; } ExprResult;

static void expr_free(ExprResult *e) { buf_free(&e->buf); }

static ExprResult parse_expression(ParserState *p);
static void parse_statement(ParserState *p, Buf *out);
static void parse_block(ParserState *p, Buf *out);
static void parse_var_decl_rest(ParserState *p, Token *type_tok, Token *name_tok, Buf *out);

/* -- expressoes ---------------------------------------------------------- */

static void literal_value_text(Buf *out, Token *tok) {
    if (strcmp(tok->type, "STRING_LIT") == 0) {
        buf_append(out, "\"", 1);
        buf_append(out, tok->attr_str, tok->attr_str_len);
        buf_append(out, "\"", 1);
    } else if (strcmp(tok->type, "CHAR_LIT") == 0) {
        buf_append(out, "'", 1);
        buf_append(out, tok->attr_str, tok->attr_str_len);
        buf_append(out, "'", 1);
    } else if (strcmp(tok->type, "TRUE") == 0) {
        buf_append(out, "true", -1);
    } else if (strcmp(tok->type, "FALSE") == 0) {
        buf_append(out, "false", -1);
    } else if (strcmp(tok->type, "FLOAT_LIT") == 0) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%g", tok->attr_float);
        if (strchr(tmp, '.') == NULL && strchr(tmp, 'e') == NULL &&
            strchr(tmp, 'n') == NULL /* nan/inf */) {
            strncat(tmp, ".0", sizeof(tmp) - strlen(tmp) - 1);
        }
        buf_append(out, tmp, -1);
    } else {
        buf_appendf(out, "%ld", tok->attr_int);
    }
}

static ExprResult parse_primary(ParserState *p) {
    Token *tok = ps_current(p);
    ExprResult r; buf_init(&r.buf); r.kind = KIND_OTHER;

    if (strcmp(tok->type, "INT_LIT") == 0 || strcmp(tok->type, "FLOAT_LIT") == 0 ||
        strcmp(tok->type, "STRING_LIT") == 0 || strcmp(tok->type, "CHAR_LIT") == 0 ||
        strcmp(tok->type, "TRUE") == 0 || strcmp(tok->type, "FALSE") == 0) {
        ps_advance(p);
        const char *tag = strcmp(tok->type, "INT_LIT") == 0 ? "int"
                         : strcmp(tok->type, "FLOAT_LIT") == 0 ? "real"
                         : strcmp(tok->type, "STRING_LIT") == 0 ? "string"
                         : strcmp(tok->type, "CHAR_LIT") == 0 ? "char" : "bool";
        buf_appendf(&r.buf, "Lit(%s,", tag);
        literal_value_text(&r.buf, tok);
        buf_append(&r.buf, ")", 1);
        return r;
    }
    if (strcmp(tok->type, "LPAREN") == 0) {
        ps_advance(p);
        ExprResult inner = parse_expression(p);
        ps_expect(p, "RPAREN", "')'");
        r.kind = inner.kind;
        buf_append(&r.buf, inner.buf.data, (long)inner.buf.len);
        expr_free(&inner);
        return r;
    }
    if (strcmp(tok->type, "IDENT") == 0) {
        ps_advance(p);
        if (ps_check(p, "LPAREN")) {
            ps_advance(p);
            buf_appendf(&r.buf, "Call(Id(%.*s)", (int)tok->lexeme_len, tok->lexeme);
            if (!ps_check(p, "RPAREN")) {
                ExprResult a = parse_expression(p);
                buf_append(&r.buf, ",", 1);
                buf_append(&r.buf, a.buf.data, (long)a.buf.len);
                expr_free(&a);
                while (ps_check(p, "COMMA")) {
                    ps_advance(p);
                    ExprResult b = parse_expression(p);
                    buf_append(&r.buf, ",", 1);
                    buf_append(&r.buf, b.buf.data, (long)b.buf.len);
                    expr_free(&b);
                }
            }
            ps_expect(p, "RPAREN", "')'");
            buf_append(&r.buf, ")", 1);
            r.kind = KIND_OTHER;
            return r;
        }
        if (ps_check(p, "LBRACKET")) {
            ps_advance(p);
            ExprResult index = parse_expression(p);
            ps_expect(p, "RBRACKET", "']'");
            buf_appendf(&r.buf, "Index(Id(%.*s),", (int)tok->lexeme_len, tok->lexeme);
            buf_append(&r.buf, index.buf.data, (long)index.buf.len);
            buf_append(&r.buf, ")", 1);
            expr_free(&index);
            r.kind = KIND_INDEX;
            return r;
        }
        buf_appendf(&r.buf, "Id(%.*s)", (int)tok->lexeme_len, tok->lexeme);
        r.kind = KIND_IDENT;
        return r;
    }
    ps_fail(p, tok, "expressao invalida: encontrado '%.*s' (%s)",
            (int)tok->lexeme_len, tok->lexeme ? tok->lexeme : "", tok->type);
    return r; /* nunca alcancado */
}

static ExprResult parse_unary(ParserState *p) {
    if (ps_check(p, "NOT") || ps_check(p, "MINUS")) {
        Token *op = ps_advance(p);
        ExprResult operand = parse_unary(p);
        ExprResult r; buf_init(&r.buf); r.kind = KIND_OTHER;
        buf_appendf(&r.buf, "Unary(%.*s,", (int)op->lexeme_len, op->lexeme);
        buf_append(&r.buf, operand.buf.data, (long)operand.buf.len);
        buf_append(&r.buf, ")", 1);
        expr_free(&operand);
        return r;
    }
    return parse_primary(p);
}

typedef ExprResult (*LevelFn)(ParserState *);

static ExprResult parse_binary_level(ParserState *p, LevelFn next, const char **ops, int n_ops) {
    ExprResult left = next(p);
    for (;;) {
        int matched = 0;
        for (int k = 0; k < n_ops; k++) if (ps_check(p, ops[k])) { matched = 1; break; }
        if (!matched) return left;
        Token *op = ps_advance(p);
        ExprResult right = next(p);
        ExprResult combined; buf_init(&combined.buf); combined.kind = KIND_OTHER;
        buf_appendf(&combined.buf, "Binary(%.*s,", (int)op->lexeme_len, op->lexeme);
        buf_append(&combined.buf, left.buf.data, (long)left.buf.len);
        buf_append(&combined.buf, ",", 1);
        buf_append(&combined.buf, right.buf.data, (long)right.buf.len);
        buf_append(&combined.buf, ")", 1);
        expr_free(&left);
        expr_free(&right);
        left = combined;
    }
}

static ExprResult parse_multiplicative(ParserState *p) {
    static const char *ops[] = {"STAR", "SLASH", "PERCENT"};
    return parse_binary_level(p, parse_unary, ops, 3);
}
static ExprResult parse_additive(ParserState *p) {
    static const char *ops[] = {"PLUS", "MINUS"};
    return parse_binary_level(p, parse_multiplicative, ops, 2);
}
static ExprResult parse_relational(ParserState *p) {
    static const char *ops[] = {"LT", "GT", "LE", "GE"};
    return parse_binary_level(p, parse_additive, ops, 4);
}
static ExprResult parse_equality(ParserState *p) {
    static const char *ops[] = {"EQ", "NE"};
    return parse_binary_level(p, parse_relational, ops, 2);
}
static ExprResult parse_logic_and(ParserState *p) {
    static const char *ops[] = {"AND"};
    return parse_binary_level(p, parse_equality, ops, 1);
}
static ExprResult parse_logic_or(ParserState *p) {
    static const char *ops[] = {"OR"};
    return parse_binary_level(p, parse_logic_and, ops, 1);
}

static ExprResult parse_assignment(ParserState *p) {
    ExprResult left = parse_logic_or(p);
    if (ps_check(p, "ASSIGN")) {
        Token *assign_tok = ps_advance(p);
        if (left.kind != KIND_IDENT && left.kind != KIND_INDEX) {
            ps_fail(p, assign_tok,
                    "lado esquerdo de '=' invalido (esperado um identificador ou um elemento de vetor)");
        }
        ExprResult value = parse_assignment(p);
        ExprResult r; buf_init(&r.buf); r.kind = KIND_OTHER;
        buf_append(&r.buf, "Assign(", -1);
        buf_append(&r.buf, left.buf.data, (long)left.buf.len);
        buf_append(&r.buf, ",", 1);
        buf_append(&r.buf, value.buf.data, (long)value.buf.len);
        buf_append(&r.buf, ")", 1);
        expr_free(&left);
        expr_free(&value);
        return r;
    }
    return left;
}

static ExprResult parse_expression(ParserState *p) { return parse_assignment(p); }

/* -- comandos -------------------------------------------------------------- */

static void parse_if(ParserState *p, Buf *out) {
    ps_advance(p);
    ps_expect(p, "LPAREN", "'('");
    ExprResult cond = parse_expression(p);
    ps_expect(p, "RPAREN", "')'");
    Buf then_buf; buf_init(&then_buf);
    parse_statement(p, &then_buf);
    Buf else_buf; buf_init(&else_buf);
    if (ps_check(p, "ELSE")) {
        ps_advance(p);
        parse_statement(p, &else_buf);
    } else {
        buf_append(&else_buf, "NULL", -1);
    }
    buf_append(out, "If(", -1);
    buf_append(out, cond.buf.data, (long)cond.buf.len);
    buf_append(out, ",", 1);
    buf_append(out, then_buf.data, (long)then_buf.len);
    buf_append(out, ",", 1);
    buf_append(out, else_buf.data, (long)else_buf.len);
    buf_append(out, ")", 1);
    expr_free(&cond); buf_free(&then_buf); buf_free(&else_buf);
}

static void parse_while(ParserState *p, Buf *out) {
    ps_advance(p);
    ps_expect(p, "LPAREN", "'('");
    ExprResult cond = parse_expression(p);
    ps_expect(p, "RPAREN", "')'");
    Buf body_buf; buf_init(&body_buf);
    parse_statement(p, &body_buf);
    buf_append(out, "While(", -1);
    buf_append(out, cond.buf.data, (long)cond.buf.len);
    buf_append(out, ",", 1);
    buf_append(out, body_buf.data, (long)body_buf.len);
    buf_append(out, ")", 1);
    expr_free(&cond); buf_free(&body_buf);
}

static void parse_expr_statement(ParserState *p, Buf *out) {
    ExprResult expr = parse_expression(p);
    ps_expect(p, "SEMICOLON", "';'");
    buf_append(out, "ExprStmt(", -1);
    buf_append(out, expr.buf.data, (long)expr.buf.len);
    buf_append(out, ")", 1);
    expr_free(&expr);
}

static void parse_for(ParserState *p, Buf *out) {
    ps_advance(p);
    ps_expect(p, "LPAREN", "'('");
    Buf init_buf; buf_init(&init_buf);
    if (ps_check(p, "SEMICOLON")) {
        ps_advance(p);
        buf_append(&init_buf, "NULL", -1);
    } else if (TYPE_IS(ps_current(p)->type)) {
        Token *type_tok = ps_advance(p);
        Token *name_tok = ps_expect(p, "IDENT", "um identificador");
        parse_var_decl_rest(p, type_tok, name_tok, &init_buf);
    } else {
        parse_expr_statement(p, &init_buf);
    }

    Buf cond_buf; buf_init(&cond_buf);
    if (!ps_check(p, "SEMICOLON")) {
        ExprResult cond = parse_expression(p);
        buf_append(&cond_buf, cond.buf.data, (long)cond.buf.len);
        expr_free(&cond);
    } else {
        buf_append(&cond_buf, "NULL", -1);
    }
    ps_expect(p, "SEMICOLON", "';'");

    Buf update_buf; buf_init(&update_buf);
    if (!ps_check(p, "RPAREN")) {
        ExprResult update = parse_expression(p);
        buf_append(&update_buf, update.buf.data, (long)update.buf.len);
        expr_free(&update);
    } else {
        buf_append(&update_buf, "NULL", -1);
    }
    ps_expect(p, "RPAREN", "')'");

    Buf body_buf; buf_init(&body_buf);
    parse_statement(p, &body_buf);

    buf_append(out, "For(", -1);
    buf_append(out, init_buf.data, (long)init_buf.len); buf_append(out, ",", 1);
    buf_append(out, cond_buf.data, (long)cond_buf.len); buf_append(out, ",", 1);
    buf_append(out, update_buf.data, (long)update_buf.len); buf_append(out, ",", 1);
    buf_append(out, body_buf.data, (long)body_buf.len);
    buf_append(out, ")", 1);
    buf_free(&init_buf); buf_free(&cond_buf); buf_free(&update_buf); buf_free(&body_buf);
}

static void parse_return(ParserState *p, Buf *out) {
    ps_advance(p);
    buf_append(out, "Return(", -1);
    if (!ps_check(p, "SEMICOLON")) {
        ExprResult value = parse_expression(p);
        buf_append(out, value.buf.data, (long)value.buf.len);
        expr_free(&value);
    } else {
        buf_append(out, "NULL", -1);
    }
    ps_expect(p, "SEMICOLON", "';'");
    buf_append(out, ")", 1);
}

static void parse_print(ParserState *p, Buf *out) {
    ps_advance(p);
    ps_expect(p, "LPAREN", "'('");
    ExprResult value = parse_expression(p);
    ps_expect(p, "RPAREN", "')'");
    ps_expect(p, "SEMICOLON", "';'");
    buf_append(out, "Print(", -1);
    buf_append(out, value.buf.data, (long)value.buf.len);
    buf_append(out, ")", 1);
    expr_free(&value);
}

static void parse_read(ParserState *p, Buf *out) {
    ps_advance(p);
    ps_expect(p, "LPAREN", "'('");
    Token *name_tok = ps_expect(p, "IDENT", "um identificador");
    ps_expect(p, "RPAREN", "')'");
    ps_expect(p, "SEMICOLON", "';'");
    buf_appendf(out, "Read(Id(%.*s))", (int)name_tok->lexeme_len, name_tok->lexeme);
}

static void parse_var_decl_rest(ParserState *p, Token *type_tok, Token *name_tok, Buf *out) {
    buf_append(out, "VarDecl(", -1);
    buf_appendf(out, "%.*s %.*s", (int)type_tok->lexeme_len, type_tok->lexeme,
                (int)name_tok->lexeme_len, name_tok->lexeme);
    if (ps_check(p, "LBRACKET")) {
        ps_advance(p);
        Token *size_tok = ps_expect(p, "INT_LIT", "um tamanho inteiro");
        buf_appendf(out, " size=Lit(int,%ld)", size_tok->attr_int);
        ps_expect(p, "RBRACKET", "']'");
    }
    if (ps_check(p, "ASSIGN")) {
        ps_advance(p);
        ExprResult init = parse_expression(p);
        buf_append(out, "=", 1);
        buf_append(out, init.buf.data, (long)init.buf.len);
        expr_free(&init);
    }
    ps_expect(p, "SEMICOLON", "';'");
    buf_append(out, ")", 1);
}

static void parse_statement(ParserState *p, Buf *out) {
    Token *tok = ps_current(p);
    if (strcmp(tok->type, "LBRACE") == 0) { parse_block(p, out); return; }
    if (strcmp(tok->type, "IF") == 0) { parse_if(p, out); return; }
    if (strcmp(tok->type, "WHILE") == 0) { parse_while(p, out); return; }
    if (strcmp(tok->type, "FOR") == 0) { parse_for(p, out); return; }
    if (strcmp(tok->type, "RETURN") == 0) { parse_return(p, out); return; }
    if (strcmp(tok->type, "BREAK") == 0) {
        ps_advance(p); ps_expect(p, "SEMICOLON", "';'");
        buf_append(out, "Break()", -1);
        return;
    }
    if (strcmp(tok->type, "CONTINUE") == 0) {
        ps_advance(p); ps_expect(p, "SEMICOLON", "';'");
        buf_append(out, "Continue()", -1);
        return;
    }
    if (strcmp(tok->type, "PRINT") == 0) { parse_print(p, out); return; }
    if (strcmp(tok->type, "READ") == 0) { parse_read(p, out); return; }
    if (TYPE_IS(tok->type)) {
        Token *type_tok = ps_advance(p);
        Token *name_tok = ps_expect(p, "IDENT", "um identificador");
        parse_var_decl_rest(p, type_tok, name_tok, out);
        return;
    }
    parse_expr_statement(p, out);
}

static void parse_block(ParserState *p, Buf *out) {
    ps_expect(p, "LBRACE", "'{'");
    buf_append(out, "Block(", -1);
    int first = 1;
    while (!ps_check(p, "RBRACE") && !ps_check(p, "EOF")) {
        if (!first) buf_append(out, ",", 1);
        parse_statement(p, out);
        first = 0;
    }
    ps_expect(p, "RBRACE", "'}'");
    buf_append(out, ")", 1);
}

static void parse_param(ParserState *p, Buf *out) {
    Token *type_tok = ps_expect_type(p);
    Token *name_tok = ps_expect(p, "IDENT", "um identificador");
    buf_appendf(out, "%.*s %.*s", (int)type_tok->lexeme_len, type_tok->lexeme,
                (int)name_tok->lexeme_len, name_tok->lexeme);
    if (ps_check(p, "LBRACKET")) {
        ps_advance(p);
        if (ps_check(p, "INT_LIT")) {
            Token *size_tok = ps_advance(p);
            buf_appendf(out, " size=Lit(int,%ld)", size_tok->attr_int);
        } else {
            buf_append(out, " size=NULL", -1);
        }
        ps_expect(p, "RBRACKET", "']'");
    }
}

static void parse_function_decl(ParserState *p, Token *type_tok, Token *name_tok, Buf *out) {
    ps_expect(p, "LPAREN", "'('");
    Buf params_buf; buf_init(&params_buf);
    if (!ps_check(p, "RPAREN")) {
        parse_param(p, &params_buf);
        while (ps_check(p, "COMMA")) {
            ps_advance(p);
            buf_append(&params_buf, ",", 1);
            parse_param(p, &params_buf);
        }
    }
    ps_expect(p, "RPAREN", "')'");
    Buf body_buf; buf_init(&body_buf);
    parse_block(p, &body_buf);

    buf_append(out, "Function(", -1);
    buf_appendf(out, "%.*s %.*s(", (int)type_tok->lexeme_len, type_tok->lexeme,
                (int)name_tok->lexeme_len, name_tok->lexeme);
    buf_append(out, params_buf.data, (long)params_buf.len);
    buf_append(out, ") ", 2);
    buf_append(out, body_buf.data, (long)body_buf.len);
    buf_append(out, ")", 1);
    buf_free(&params_buf); buf_free(&body_buf);
}

static void parse_top_level(ParserState *p, Buf *out) {
    Token *tok = ps_current(p);
    if (TYPE_IS(tok->type)) {
        Token *type_tok = ps_advance(p);
        Token *name_tok = ps_expect(p, "IDENT", "um identificador");
        if (ps_check(p, "LPAREN")) parse_function_decl(p, type_tok, name_tok, out);
        else parse_var_decl_rest(p, type_tok, name_tok, out);
        return;
    }
    parse_statement(p, out);
}

static void parse_program(ParserState *p, Buf *out) {
    buf_append(out, "Program(", -1);
    int first = 1;
    while (!ps_check(p, "EOF")) {
        if (!first) buf_append(out, ",", 1);
        parse_top_level(p, out);
        first = 0;
    }
    buf_append(out, ")", 1);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "uso: %s <arquivo.minic|arquivo.c>\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "erro ao abrir %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);

    Scanner sc = { buf, (long)nread, 0, 1, 1 };
    TokenList tokens = lex_all(&sc);

    ParserState p; memset(&p, 0, sizeof(p));
    p.tokens = tokens; p.pos = 0;

    Buf out; buf_init(&out);
    int rc = 0;
    if (setjmp(p.error_jmp) == 0) {
        parse_program(&p, &out);
        fwrite(out.data, 1, out.len, stdout);
        fputc('\n', stdout);
    } else {
        printf("ERRO SINTATICO: %s (linha %d, coluna %d)\n",
               p.error_message, p.error_token.line, p.error_token.column);
        rc = 1;
    }

    buf_free(&out);
    free(tokens.items);
    free(buf);
    return rc;
}
