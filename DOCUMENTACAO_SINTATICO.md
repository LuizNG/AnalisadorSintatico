GRUPO - INTEGRANTES

Raphael Garcia| RA: 2401142

Gabriel Erthal Silva | RA: 2400814

Luiz Guilherme Nogueira Ortiz | RA: 2401554

João Pedro Marques Nardi | RA: 2401622




# Analisador sintático da linguagem MINIC

Implementação de um analisador sintático (parser) para a linguagem
MINIC em **Python** (`parser.py`) e em **C** (`parser.c`)

## Técnica: descida recursiva

Cada não-terminal da gramática vira uma função. O parser é preditivo
(LL(1)): decide a regra a aplicar olhando só o token atual, sem
*backtracking*. A precedência dos operadores segue a ordem de chamada
entre os níveis de expressão (do menor para o maior): atribuição →
`||` → `&&` → igualdade → relacional → aditivo → multiplicativo →
unário → primário.

Ao encontrar um erro sintático, o parser para na primeira falha (sem
recuperação com múltiplos diagnósticos — o enunciado só pede
aceitar/rejeitar, então não foi necessário).

O parser reaproveita a mesma lógica de análise léxica de `scanner.py`
/ `scanner.c` (embutida no próprio arquivo, para funcionar sozinho).

## Gramática (EBNF)

```
programa        := (declaracao | comando)* EOF   (* ver nota abaixo *)

declaracao      := tipo IDENT ( funcao_resto | var_decl_resto )
funcao_resto    := '(' lista_params? ')' bloco
lista_params    := param (',' param)*
param           := tipo IDENT ( '[' INT_LIT? ']' )?

var_decl_resto  := ( '[' INT_LIT ']' )? ( '=' expressao )? ';'
tipo            := 'int' | 'float' | 'bool' | 'char' | 'void'

bloco           := '{' comando* '}'
comando         := bloco
                  | 'if' '(' expressao ')' comando ('else' comando)?
                  | 'while' '(' expressao ')' comando
                  | 'for' '(' for_init? ';' expressao? ';' expressao? ')' comando
                  | 'return' expressao? ';'
                  | 'break' ';' | 'continue' ';'
                  | 'print' '(' expressao ')' ';'
                  | 'read' '(' IDENT ')' ';'
                  | tipo IDENT var_decl_resto
                  | expressao ';'
for_init        := tipo IDENT var_decl_resto | expressao ';'

expressao       := atribuicao
atribuicao      := (IDENT ('[' expressao ']')? '=' atribuicao) | logico_ou
logico_ou       := logico_e ('||' logico_e)*
logico_e        := igualdade ('&&' igualdade)*
igualdade       := relacional (('==' | '!=') relacional)*
relacional      := aditivo (('<' | '>' | '<=' | '>=') aditivo)*
aditivo         := multiplicativo (('+' | '-') multiplicativo)*
multiplicativo  := unario (('*' | '/' | '%') unario)*
unario          := ('!' | '-') unario | primario
primario        := INT_LIT | FLOAT_LIT | STRING_LIT | CHAR_LIT | 'true' | 'false'
                  | IDENT '(' lista_args? ')'
                  | IDENT '[' expressao ']'
                  | IDENT
                  | '(' expressao ')'
lista_args      := expressao (',' expressao)*
```

## Formato da AST (S-expression)

Convenção adotada (a mesma usada pelo professor no `README.md` do
pacote de testes, com a formatação compacta descrita acima):

| Construção | Forma |
|---|---|
| Programa | `Program(d1,d2,...)` |
| Função | `Function(<tipo> <nome>(<p1>,<p2>,...) <Block>)` |
| Parâmetro (dentro da lista) | `<tipo> <nome>` ou `<tipo> <nome> size=Lit(int,N)` |
| Declaração de variável | `VarDecl(<tipo> <nome>)`, `VarDecl(<tipo> <nome>=<expr>)`, ou `VarDecl(<tipo> <nome> size=Lit(int,N))` |
| Bloco | `Block(c1,c2,...)` |
| If | `If(<cond>,<então>,<senão ou NULL>)` |
| While | `While(<cond>,<corpo>)` |
| For | `For(<init ou NULL>,<cond ou NULL>,<update ou NULL>,<corpo>)` |
| Return | `Return(<valor ou NULL>)` |
| Break / Continue | `Break()` / `Continue()` |
| Print | `Print(<expr>)` |
| Read | `Read(Id(<nome>))` |
| Comando-expressão | `ExprStmt(<expr>)` |
| Atribuição | `Assign(<alvo>,<valor>)` |
| Binária | `Binary(<op>,<esq>,<dir>)` |
| Unária | `Unary(<op>,<operando>)` |
| Chamada | `Call(Id(<nome>),<arg1>,...)` |
| Indexação | `Index(Id(<nome>),<índice>)` |
| Identificador | `Id(<nome>)` |
| Literal | `Lit(int,N)`, `Lit(real,N)`, `Lit(bool,true\|false)`, `Lit(string,"...")`, `Lit(char,'c')` |
| Ausência de ramo/valor | `NULL` |

`For`, `Print`, `Read`, `Break` e `Continue` não aparecem nos 50 casos
oficiais (que não exercitam essas construções), mas a linguagem MINIC
do léxico as inclui, então o parser as suporta com essa convenção
adotada por consistência com o resto da notação.

## Estrutura do repositório (pasta deste analisador)

```
.
├── DOCUMENTACAO_SINTATICO.md
├── parser.py
├── parser.c
├── testar_parser_py.sh              (corrigido — ver secao acima)
├── testar_parser_c.sh               (corrigido)
├── testar_parser_py_oficial.sh      (recebido do professor, sem alteracoes)
├── testar_parser_c_oficial.sh       (recebido do professor, sem alteracoes)
├── testes-parser-50/                (pacote de testes recebido do professor)
│   ├── README.md, EXECUCAO.txt, manifesto.json
│   └── casos/01_.../ ... 50_.../    (codigo.c, ast.esperada.txt, resultado.esperado.txt)
└── resultados/
    ├── python/*.output.txt          (saida real do parser.py, por caso)
    ├── c/*.output.txt               (saida real do parser.c, por caso)
    ├── log_corrigido_python.txt     (log do testar_parser_py.sh: 49/50)
    ├── log_corrigido_c.txt          (log do testar_parser_c.sh: 49/50)
    ├── log_oficial_python.txt       (log do script original do professor)
    └── log_oficial_c.txt            (log do script original do professor)
```
