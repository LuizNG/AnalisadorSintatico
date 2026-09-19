# Analisador sintático da linguagem MINIC

Implementação de um analisador sintático (parser) para a linguagem
MINIC em **Python** (`parser.py`) e em **C** (`parser.c`), usando
**descida recursiva**. Testado contra o pacote oficial de 50 casos do
professor (`testes-parser-50/`): **49 de 50 corretos** nas duas
implementações (o único caso restante tem um erro no próprio arquivo
de gabarito do professor — evidência abaixo).

## Como executar

```bash
python parser.py caminho/codigo.c

gcc -Wall -Wextra -std=c11 parser.c -o parser
./parser caminho/codigo.c
```

Em caso de sucesso, imprime a AST como uma S-expression compacta (ex.:
`Program(VarDecl(int x))`) e termina com código 0. Em caso de erro
sintático, imprime uma linha `ERRO SINTATICO: ...` e termina com
código diferente de zero.

## Como rodar os testes

```bash
bash testar_parser_py.sh testes-parser-50/casos parser.py
bash testar_parser_c.sh testes-parser-50/casos parser.c
```

Isso roda os dois scripts **corrigidos** (ver seção abaixo) contra os
50 casos oficiais. Os scripts **originais do professor** (transcritos
tal como recebidos, sem nenhuma alteração) também estão aqui, para
referência ou caso ele peça para rodá-los especificamente:

```bash
bash testar_parser_py_oficial.sh testes-parser-50 parser.py
bash testar_parser_c_oficial.sh testes-parser-50 parser.c
```

Os resultados de todas essas execuções (scripts corrigidos e
originais, Python e C) estão salvos em `resultados/`.

## Por que os scripts de teste do professor foram corrigidos

Os dois scripts (`testar_parser_py_oficial.sh` / `testar_parser_c_oficial.sh`,
recebidos em PDF) têm três problemas concretos, verificados durante o
desenvolvimento:

**1. Nos 25 casos inválidos, eles comparam com o arquivo errado.**
A função `find_expected` dos scripts sempre resolve para
`ast.esperada.txt`, mesmo nos casos 26–50. Só que, nesses casos,
`ast.esperada.txt` não contém um resultado comparável — contém a frase
`"NÃO HÁ AST: o parser deve rejeitar a entrada."` (o resultado que
importa ali está em `resultado.esperado.txt`, com `REJEITADO` e uma
pista do erro esperado). Como nenhum parser sensato imprimiria essa
frase de documentação literalmente, o script original reprova **100%
dos casos inválidos independente do parser estar certo ou não**. O
`testar_parser_py.sh`/`testar_parser_c.sh` corrigidos usam o arquivo
certo: para um caso `REJEITADO`, exigem apenas que o parser termine
com código de saída diferente de zero.

**2. O script da versão C nunca encontra o binário compilado.**
Depois de compilar com `gcc ... -o "${PARSER/.c/}"`, ele tenta
executar o programa chamando só `"$PARSER"` (ex.: `parser`), sem o
prefixo `./`. Como o diretório atual normalmente não está no `PATH` do
shell, isso falha para todo caso de teste ("comando não encontrado"),
não importa o quão correto o parser esteja. Confirmado rodando o
script tal como recebido: `resultados/log_oficial_c.txt` mostra 0
aprovados e 50 "erros de execução". Os scripts corrigidos chamam
`./parser` explicitamente.

**3. Os arquivos `ast.esperada.txt` têm espaçamento interno
inconsistente entre si.** O mesmo tipo de construção aparece formatada
de dois jeitos diferentes em casos diferentes — por exemplo,
`Assign(Id(x), Lit(int,7))` (com espaço depois da vírgula) no caso 07,
mas `Assign(Index(Id(a),Lit(int,0)),Lit(int,9))` (sem espaço) no caso
17. Contando todas as vírgulas nos 25 arquivos: 133 sem espaço depois
contra 42 com espaço — a maioria não tem espaço, e foi essa a
convenção usada nos dois parsers deste projeto. Como a comparação
oficial é bit a bit (`cmp`), nenhuma implementação única consegue bater
com as duas convenções ao mesmo tempo. Por isso os scripts corrigidos
comparam a AST **ignorando espaços em branco fora de literais de
cadeia** (estrutura, não formatação).

**Evidência extra — um gabarito com parênteses desbalanceados:** o
caso `24_la_o_com_express_o_complexa` falha mesmo na comparação sem
espaços. Conferindo, o próprio `ast.esperada.txt` desse caso tem um
parêntese de fechamento sobrando (23 `(` contra 24 `)` — a árvore que
ele descreve nem fecha direito). A AST que os dois parsers produzem
para esse caso está corretamente balanceada e estruturalmente idêntica
ao resto do gabarito, então é seguro dizer que o erro está no arquivo
de gabarito, não no parser. Isso é o único dos 50 casos que não passa,
mesmo com a comparação corrigida — por isso "49 de 50", não "50 de
50".

Nada disso é uma crítica ao trabalho do professor — scripts e gabaritos
com esses detalhes acontecem. Vale mencionar isso a ele se o resultado
da correção automática vier estranho, para que ele saiba que é um
problema conhecido do pacote de testes e não do código entregue.

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

> **Nota:** o nível do programa aceita não só declarações (variáveis e
> funções), mas também comandos soltos — o caso 22 do pacote oficial
> (`int a; int b; a = b = 3;`) tem uma atribuição diretamente no nível
> global, fora de qualquer função.

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
