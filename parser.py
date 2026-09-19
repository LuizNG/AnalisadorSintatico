#!/usr/bin/env python3
"""Analisador sintatico (parser) da linguagem MINIC.

Uso:
    python parser.py arquivo.minic
    python parser.py arquivo.c

Le o arquivo de entrada, executa a analise lexica e a analise sintatica
(descida recursiva) e imprime a arvore sintatica (AST) resultante como
uma S-expression compacta na saida padrao (ex.: "Program(VarDecl(int
x))"). Se houver um erro sintatico, imprime uma mensagem de erro
sintatico (linha "ERRO SINTATICO: ...") e termina com codigo diferente
de zero.

Este arquivo inclui seu proprio analisador lexico (a mesma
implementacao de scanner.py), para que o parser funcione sozinho, sem
depender de nenhum outro arquivo do projeto.
"""

import io
import sys

if hasattr(sys.stdout, "buffer"):
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", newline="\n")

# --------------------------------------------------------------------------
# Analisador lexico (identico, em espirito, ao scanner.py do projeto)
# --------------------------------------------------------------------------

KEYWORDS = {
    "int": "INT", "float": "FLOAT", "bool": "BOOL", "char": "CHAR",
    "void": "VOID", "if": "IF", "else": "ELSE", "while": "WHILE",
    "for": "FOR", "return": "RETURN", "break": "BREAK",
    "continue": "CONTINUE", "true": "TRUE", "false": "FALSE",
    "print": "PRINT", "read": "READ",
}

COMPOUND_OPERATORS = {
    "==": "EQ", "!=": "NE", "<=": "LE", ">=": "GE", "&&": "AND", "||": "OR",
}

SINGLE_OPERATORS = {
    "=": "ASSIGN", "<": "LT", ">": "GT", "!": "NOT", "+": "PLUS",
    "-": "MINUS", "*": "STAR", "/": "SLASH", "%": "PERCENT",
    "(": "LPAREN", ")": "RPAREN", "{": "LBRACE", "}": "RBRACE",
    "[": "LBRACKET", "]": "RBRACKET", ",": "COMMA", ";": "SEMICOLON",
    ".": "DOT",
}

DELIMITER_STOP_CHARS = set("(){}[],;+-*/%<>=!&|.")


def is_ident_start(c):
    return c.isalpha() or c == "_"


def is_ident_continue(c):
    return c.isalnum() or c == "_"


def is_digit(c):
    return "0" <= c <= "9"


class Token:
    __slots__ = ("type", "lexeme", "attribute", "line", "column")

    def __init__(self, type_, lexeme, attribute, line, column):
        self.type = type_
        self.lexeme = lexeme
        self.attribute = attribute
        self.line = line
        self.column = column

    def __repr__(self):
        return f"Token({self.type!r}, {self.lexeme!r})"


class Lexer:
    def __init__(self, src):
        self.src = src
        self.n = len(src)
        self.i = 0
        self.line = 1
        self.column = 1

    def peek(self, offset=0):
        j = self.i + offset
        return self.src[j] if j < self.n else ""

    def advance(self):
        c = self.src[self.i]
        self.i += 1
        if c == "\n":
            self.line += 1
            self.column = 1
        else:
            self.column += 1
        return c

    def tokens(self):
        result = []
        while True:
            tok = self._next_token()
            result.append(tok)
            if tok.type == "EOF":
                return result

    def _next_token(self):
        while True:
            self._skip_ws_and_comments()
            if self.i >= self.n:
                return Token("EOF", "", None, self.line, self.column)
            line, column = self.line, self.column
            c = self.peek()
            if is_ident_start(c):
                return self._scan_identifier_or_keyword(line, column)
            if is_digit(c):
                return self._scan_number(line, column)
            if c == '"':
                return self._scan_string(line, column)
            if c == "'":
                return self._scan_char(line, column)
            tok = self._scan_operator_or_symbol(line, column)
            if tok is not None:
                return tok
            # simbolo desconhecido: o scanner.c/scanner.py ja cobre o
            # diagnostico; aqui so pulamos e continuamos.

    def _skip_ws_and_comments(self):
        while self.i < self.n:
            c = self.peek()
            if c in (" ", "\t", "\r", "\n"):
                self.advance()
                continue
            if c == "/" and self.peek(1) == "/":
                while self.i < self.n and self.peek() != "\n":
                    self.advance()
                continue
            if c == "/" and self.peek(1) == "*":
                self.advance()
                self.advance()
                while self.i < self.n:
                    if self.peek() == "*" and self.peek(1) == "/":
                        self.advance()
                        self.advance()
                        break
                    self.advance()
                continue
            break

    def _scan_identifier_or_keyword(self, line, column):
        start = self.i
        while self.i < self.n and is_ident_continue(self.peek()):
            self.advance()
        lexeme = self.src[start:self.i]
        keyword = KEYWORDS.get(lexeme)
        if keyword is not None:
            return Token(keyword, lexeme, None, line, column)
        return Token("IDENT", lexeme, lexeme, line, column)

    def _scan_number(self, line, column):
        start = self.i
        while self.i < self.n and is_digit(self.peek()):
            self.advance()
        int_part = self.src[start:self.i]

        if self.peek() == "." and is_digit(self.peek(1)):
            self.advance()
            while self.i < self.n and is_digit(self.peek()):
                self.advance()
            lexeme = self.src[start:self.i]
            return Token("FLOAT_LIT", lexeme, float(lexeme), line, column)

        return Token("INT_LIT", int_part, int(int_part), line, column)

    def _scan_string(self, line, column):
        start = self.i
        j = self.i + 1
        closing = -1
        while j < self.n and self.src[j] != "\n":
            if self.src[j] == '"':
                closing = j
                break
            j += 1

        if closing != -1:
            self.advance()
            content = []
            while self.i < closing:
                content.append(self.peek())
                self.advance()
            self.advance()
            lexeme = self.src[start:self.i]
            return Token("STRING_LIT", lexeme, "".join(content), line, column)

        self.advance()
        while (self.i < self.n and self.peek() != "\n"
               and self.peek() not in DELIMITER_STOP_CHARS):
            self.advance()
        return self._next_token()

    def _scan_char(self, line, column):
        start = self.i
        self.advance()
        content_char = None
        if self.i < self.n and self.peek() != "\n":
            content_char = self.peek()
            self.advance()
        if content_char is not None and self.peek() == "'":
            self.advance()
            lexeme = self.src[start:self.i]
            return Token("CHAR_LIT", lexeme, content_char, line, column)
        while self.i < self.n and self.peek() != "\n":
            self.advance()
        if self.i < self.n:
            self.advance()
        return self._next_token()

    def _scan_operator_or_symbol(self, line, column):
        two = self.src[self.i:self.i + 2]
        if two in COMPOUND_OPERATORS:
            self.advance()
            self.advance()
            return Token(COMPOUND_OPERATORS[two], two, None, line, column)
        c = self.peek()
        if c == "&" or c == "|":
            self.advance()
            return None
        if c in SINGLE_OPERATORS:
            self.advance()
            return Token(SINGLE_OPERATORS[c], c, None, line, column)
        self.advance()
        return None


# --------------------------------------------------------------------------
# Analisador sintatico (descida recursiva)
# --------------------------------------------------------------------------

TYPE_TOKENS = {"INT", "FLOAT", "BOOL", "CHAR", "VOID"}

# Mapeia o tipo de literal para o nome usado dentro de Lit(<tag>,valor).
LITERAL_TAG = {
    "INT_LIT": "int", "FLOAT_LIT": "real", "STRING_LIT": "string",
    "CHAR_LIT": "char", "TRUE": "bool", "FALSE": "bool",
}


class SyntaxErrorMinic(Exception):
    def __init__(self, message, token):
        super().__init__(message)
        self.message = message
        self.token = token


class Node:
    """No da AST: guarda o texto em S-expression ja renderizado e, para
    expressoes, a 'forma' (Ident/Index/Other) usada para validar o alvo
    de uma atribuicao."""
    __slots__ = ("text", "kind")

    def __init__(self, text, kind="Other"):
        self.text = text
        self.kind = kind

    def __str__(self):
        return self.text


def join(nodes):
    return ",".join(n.text for n in nodes)


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def _current(self):
        return self.tokens[self.pos]

    def _check(self, type_):
        return self._current().type == type_

    def _check_any(self, types):
        return self._current().type in types

    def _advance(self):
        tok = self.tokens[self.pos]
        if tok.type != "EOF":
            self.pos += 1
        return tok

    def _expect(self, type_, what):
        if self._check(type_):
            return self._advance()
        tok = self._current()
        raise SyntaxErrorMinic(
            f"esperado {what}, encontrado '{tok.lexeme}' ({tok.type})", tok)

    def _expect_type(self):
        tok = self._current()
        if tok.type in TYPE_TOKENS:
            return self._advance()
        raise SyntaxErrorMinic(
            f"esperado um tipo (int, float, bool, char, void), "
            f"encontrado '{tok.lexeme}' ({tok.type})", tok)

    # -- programa ---------------------------------------------------------

    def parse_program(self):
        decls = []
        while not self._check("EOF"):
            decls.append(self._parse_top_level())
        return f"Program({join(decls)})"

    def _parse_top_level(self):
        # No nivel do programa aceitamos declaracoes de funcao/variavel
        # (o caso comum) e, para permitir tambem comandos soltos fora de
        # funcao (como "a = b = 3;"), qualquer outro comando valido.
        if self._check_any(TYPE_TOKENS):
            type_tok = self._advance()
            name_tok = self._expect("IDENT", "um identificador")
            if self._check("LPAREN"):
                return self._parse_function_decl(type_tok, name_tok)
            return self._parse_var_decl_rest(type_tok, name_tok)
        return self._parse_statement()

    # -- funcoes ------------------------------------------------------------

    def _parse_function_decl(self, type_tok, name_tok):
        self._expect("LPAREN", "'('")
        params = []
        if not self._check("RPAREN"):
            params.append(self._parse_param())
            while self._check("COMMA"):
                self._advance()
                params.append(self._parse_param())
        self._expect("RPAREN", "')'")
        body = self._parse_block()
        params_text = ",".join(params)
        return Node(f"Function({type_tok.lexeme} {name_tok.lexeme}({params_text}) {body})")

    def _parse_param(self):
        type_tok = self._expect_type()
        name_tok = self._expect("IDENT", "um identificador")
        text = f"{type_tok.lexeme} {name_tok.lexeme}"
        if self._check("LBRACKET"):
            self._advance()
            if self._check("INT_LIT"):
                size_tok = self._advance()
                text += f" size=Lit(int,{size_tok.attribute})"
            else:
                text += " size=NULL"
            self._expect("RBRACKET", "']'")
        return text

    # -- declaracao de variavel ---------------------------------------------

    def _parse_var_decl_rest(self, type_tok, name_tok):
        text = f"{type_tok.lexeme} {name_tok.lexeme}"
        if self._check("LBRACKET"):
            self._advance()
            size_tok = self._expect("INT_LIT", "um tamanho inteiro")
            text += f" size=Lit(int,{size_tok.attribute})"
            self._expect("RBRACKET", "']'")
        if self._check("ASSIGN"):
            self._advance()
            init = self._parse_expression()
            text += f"={init.text}"
        self._expect("SEMICOLON", "';'")
        return Node(f"VarDecl({text})")

    # -- bloco e comandos -----------------------------------------------------

    def _parse_block(self):
        self._expect("LBRACE", "'{'")
        stmts = []
        while not self._check("RBRACE") and not self._check("EOF"):
            stmts.append(self._parse_statement())
        self._expect("RBRACE", "'}'")
        return Node(f"Block({join(stmts)})")

    def _parse_statement(self):
        tok = self._current()
        if tok.type == "LBRACE":
            return self._parse_block()
        if tok.type == "IF":
            return self._parse_if()
        if tok.type == "WHILE":
            return self._parse_while()
        if tok.type == "FOR":
            return self._parse_for()
        if tok.type == "RETURN":
            return self._parse_return()
        if tok.type == "BREAK":
            self._advance()
            self._expect("SEMICOLON", "';'")
            return Node("Break()")
        if tok.type == "CONTINUE":
            self._advance()
            self._expect("SEMICOLON", "';'")
            return Node("Continue()")
        if tok.type == "PRINT":
            return self._parse_print()
        if tok.type == "READ":
            return self._parse_read()
        if tok.type in TYPE_TOKENS:
            type_tok = self._advance()
            name_tok = self._expect("IDENT", "um identificador")
            return self._parse_var_decl_rest(type_tok, name_tok)
        return self._parse_expr_statement()

    def _parse_if(self):
        self._advance()
        self._expect("LPAREN", "'('")
        cond = self._parse_expression()
        self._expect("RPAREN", "')'")
        then_branch = self._parse_statement()
        if self._check("ELSE"):
            self._advance()
            else_branch = self._parse_statement()
            else_text = else_branch.text
        else:
            else_text = "NULL"
        return Node(f"If({cond.text},{then_branch.text},{else_text})")

    def _parse_while(self):
        self._advance()
        self._expect("LPAREN", "'('")
        cond = self._parse_expression()
        self._expect("RPAREN", "')'")
        body = self._parse_statement()
        return Node(f"While({cond.text},{body.text})")

    def _parse_for(self):
        self._advance()
        self._expect("LPAREN", "'('")
        if self._check("SEMICOLON"):
            self._advance()
            init_text = "NULL"
        elif self._check_any(TYPE_TOKENS):
            type_tok = self._advance()
            name_tok = self._expect("IDENT", "um identificador")
            init_text = self._parse_var_decl_rest(type_tok, name_tok).text
        else:
            init_text = self._parse_expr_statement().text

        if not self._check("SEMICOLON"):
            cond_text = self._parse_expression().text
        else:
            cond_text = "NULL"
        self._expect("SEMICOLON", "';'")

        if not self._check("RPAREN"):
            update_text = self._parse_expression().text
        else:
            update_text = "NULL"
        self._expect("RPAREN", "')'")

        body = self._parse_statement()
        return Node(f"For({init_text},{cond_text},{update_text},{body.text})")

    def _parse_return(self):
        self._advance()
        if not self._check("SEMICOLON"):
            value = self._parse_expression().text
        else:
            value = "NULL"
        self._expect("SEMICOLON", "';'")
        return Node(f"Return({value})")

    def _parse_print(self):
        self._advance()
        self._expect("LPAREN", "'('")
        value = self._parse_expression()
        self._expect("RPAREN", "')'")
        self._expect("SEMICOLON", "';'")
        return Node(f"Print({value.text})")

    def _parse_read(self):
        self._advance()
        self._expect("LPAREN", "'('")
        name_tok = self._expect("IDENT", "um identificador")
        self._expect("RPAREN", "')'")
        self._expect("SEMICOLON", "';'")
        return Node(f"Read(Id({name_tok.lexeme}))")

    def _parse_expr_statement(self):
        expr = self._parse_expression()
        self._expect("SEMICOLON", "';'")
        return Node(f"ExprStmt({expr.text})")

    # -- expressoes (precedencia crescente) ----------------------------------

    def _parse_expression(self):
        return self._parse_assignment()

    def _parse_assignment(self):
        expr = self._parse_logic_or()
        if self._check("ASSIGN"):
            assign_tok = self._advance()
            if expr.kind not in ("Ident", "Index"):
                raise SyntaxErrorMinic(
                    "lado esquerdo de '=' invalido (esperado um identificador "
                    "ou um elemento de vetor)", assign_tok)
            value = self._parse_assignment()
            return Node(f"Assign({expr.text},{value.text})", "Other")
        return expr

    def _parse_binary_level(self, next_level, operator_types):
        expr = next_level()
        while self._check_any(operator_types):
            op_tok = self._advance()
            right = next_level()
            expr = Node(f"Binary({op_tok.lexeme},{expr.text},{right.text})", "Other")
        return expr

    def _parse_logic_or(self):
        return self._parse_binary_level(self._parse_logic_and, {"OR"})

    def _parse_logic_and(self):
        return self._parse_binary_level(self._parse_equality, {"AND"})

    def _parse_equality(self):
        return self._parse_binary_level(self._parse_relational, {"EQ", "NE"})

    def _parse_relational(self):
        return self._parse_binary_level(self._parse_additive, {"LT", "GT", "LE", "GE"})

    def _parse_additive(self):
        return self._parse_binary_level(self._parse_multiplicative, {"PLUS", "MINUS"})

    def _parse_multiplicative(self):
        return self._parse_binary_level(self._parse_unary, {"STAR", "SLASH", "PERCENT"})

    def _parse_unary(self):
        if self._check_any({"NOT", "MINUS"}):
            op_tok = self._advance()
            operand = self._parse_unary()
            return Node(f"Unary({op_tok.lexeme},{operand.text})", "Other")
        return self._parse_primary()

    def _literal_value_text(self, tok):
        if tok.type == "STRING_LIT":
            escaped = tok.attribute.replace("\\", "\\\\").replace('"', '\\"')
            return f'"{escaped}"'
        if tok.type == "CHAR_LIT":
            return f"'{tok.attribute}'"
        if tok.type in ("TRUE", "FALSE"):
            return "true" if tok.type == "TRUE" else "false"
        return str(tok.attribute)

    def _parse_primary(self):
        tok = self._current()
        if tok.type in ("INT_LIT", "FLOAT_LIT", "STRING_LIT", "CHAR_LIT", "TRUE", "FALSE"):
            self._advance()
            tag = LITERAL_TAG[tok.type]
            value = self._literal_value_text(tok)
            return Node(f"Lit({tag},{value})", "Lit")
        if tok.type == "LPAREN":
            self._advance()
            expr = self._parse_expression()
            self._expect("RPAREN", "')'")
            return Node(expr.text, expr.kind)
        if tok.type == "IDENT":
            self._advance()
            if self._check("LPAREN"):
                self._advance()
                args = []
                if not self._check("RPAREN"):
                    args.append(self._parse_expression())
                    while self._check("COMMA"):
                        self._advance()
                        args.append(self._parse_expression())
                self._expect("RPAREN", "')'")
                args_text = ",".join(a.text for a in args)
                sep = "," if args_text else ""
                return Node(f"Call(Id({tok.lexeme}){sep}{args_text})", "Call")
            if self._check("LBRACKET"):
                self._advance()
                index = self._parse_expression()
                self._expect("RBRACKET", "']'")
                return Node(f"Index(Id({tok.lexeme}),{index.text})", "Index")
            return Node(f"Id({tok.lexeme})", "Ident")
        raise SyntaxErrorMinic(
            f"expressao invalida: encontrado '{tok.lexeme}' ({tok.type})", tok)


def main():
    if len(sys.argv) != 2:
        print("uso: parser.py <arquivo.minic|arquivo.c>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()

    lexer = Lexer(src)
    tokens = lexer.tokens()

    parser = Parser(tokens)
    try:
        ast_text = parser.parse_program()
    except SyntaxErrorMinic as exc:
        print(f"ERRO SINTATICO: {exc.message} "
              f"(linha {exc.token.line}, coluna {exc.token.column})")
        return 1

    print(ast_text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
