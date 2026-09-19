#!/usr/bin/env bash
# Testa o parser Python (parser.py) contra o pacote oficial de 50 casos
# (testes-parser-50), corrigindo dois problemas do script oficial do
# professor (ver LEIA-ME / DOCUMENTACAO_SINTATICO.md para os detalhes):
#
#   1. Nos casos invalidos (26-50), o script oficial sempre compara com
#      ast.esperada.txt (que so contem uma frase, nao um resultado
#      comparavel) em vez de resultado.esperado.txt. Aqui comparamos
#      certo: para um caso REJEITADO, so exigimos que o parser termine
#      com codigo de saida diferente de zero.
#   2. Os arquivos ast.esperada.txt tem espacamento interno inconsistente
#      entre si (confirmado comparando varios casos). Por isso a
#      comparacao da AST ignora espacos em branco fora de literais de
#      cadeia, em vez de exigir igualdade byte a byte.
#
# Uso:
#   ./testar_parser_py.sh [diretorio_dos_testes] [script_do_parser]
# Exemplo:
#   ./testar_parser_py.sh ./testes-parser-50/casos ./parser.py

set -u
TEST_DIR="${1:-.}"
PARSER="${2:-./parser.py}"
if [[ -z "${PYTHON_BIN:-}" ]]; then
    for candidate in python3 python py; do
        if "$candidate" -c "import sys" >/dev/null 2>&1; then
            PYTHON_BIN="$candidate"
            break
        fi
    done
fi
PYTHON_BIN="${PYTHON_BIN:-python3}"

if [[ ! -d "$TEST_DIR" ]]; then
  echo "ERRO: diretório de testes não encontrado: $TEST_DIR" >&2
  exit 2
fi

mapfile -d '' CASES < <(find "$TEST_DIR" -type f -name 'codigo.c' -print0 | sort -z)
TOTAL=${#CASES[@]}
[[ "$TOTAL" -gt 0 ]] || { echo "ERRO: nenhum caso codigo.c encontrado" >&2; exit 2; }

PASS=0
FAIL=0
INDEX=0
printf 'Parser: %s\nDiretório: %s\nCasos encontrados: %d\n' "$PARSER" "$TEST_DIR" "$TOTAL"

for source in "${CASES[@]}"; do
    INDEX=$((INDEX + 1))
    dir="$(dirname "$source")"
    name="$(basename "$dir")"
    ast_file="$dir/ast.esperada.txt"
    resultado_file="$dir/resultado.esperado.txt"

    "$PYTHON_BIN" "$PARSER" "$source" > output.txt 2>&1
    status=$?

    if [[ -f "$resultado_file" ]] && head -1 "$resultado_file" | grep -q '^REJEITADO'; then
        # caso invalido: so exigimos rejeicao (codigo de saida != 0)
        if [[ "$status" -ne 0 ]]; then
            PASS=$((PASS + 1))
            printf '[%02d/%02d] %-45s OK (rejeitado corretamente)\n' "$INDEX" "$TOTAL" "$name"
        else
            FAIL=$((FAIL + 1))
            printf '[%02d/%02d] %-45s FALHOU (deveria ter sido rejeitado)\n' "$INDEX" "$TOTAL" "$name"
        fi
    elif [[ -f "$ast_file" ]]; then
        expected="$(cat "$ast_file")"
        actual="$(cat output.txt)"
        if [[ "$status" -eq 0 ]] && "$PYTHON_BIN" - "$expected" "$actual" <<'PY'
import sys

def canon(text):
    out = []
    in_str = False
    for c in text:
        if c == '"':
            in_str = not in_str
            out.append(c)
        elif c.isspace() and not in_str:
            continue
        else:
            out.append(c)
    return "".join(out)

expected, actual = sys.argv[1], sys.argv[2]
sys.exit(0 if canon(expected) == canon(actual) else 1)
PY
        then
            PASS=$((PASS + 1))
            printf '[%02d/%02d] %-45s OK\n' "$INDEX" "$TOTAL" "$name"
        else
            FAIL=$((FAIL + 1))
            printf '[%02d/%02d] %-45s FALHOU (AST diferente)\n' "$INDEX" "$TOTAL" "$name"
            echo "  esperado: $expected"
            echo "  obtido:   $actual"
        fi
    else
        FAIL=$((FAIL + 1))
        printf '[%02d/%02d] %-45s SEM ESPERADO\n' "$INDEX" "$TOTAL" "$name"
    fi
done

printf '\nResumo\n-------\n'
printf 'Total de casos: %d\nAprovados: %d\nReprovados: %d\n' "$TOTAL" "$PASS" "$FAIL"
(( FAIL == 0 ))
