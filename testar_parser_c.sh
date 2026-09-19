#!/usr/bin/env bash
# Testa o parser C (parser.c) contra o pacote oficial de 50 casos
# (testes-parser-50). Compila o parser.c e aplica a mesma comparacao
# corrigida usada em testar_parser_py.sh (ver o cabecalho daquele
# arquivo, ou DOCUMENTACAO_SINTATICO.md, para os detalhes do porque).
#
# Uso:
#   ./testar_parser_c.sh [diretorio_dos_testes] [codigo_do_parser]
# Exemplo:
#   ./testar_parser_c.sh ./testes-parser-50/casos ./parser.c

set -u
TEST_DIR="${1:-.}"
PARSER_SOURCE="${2:-./parser.c}"
PARSER_BINARY="./parser"

if [[ ! -d "$TEST_DIR" ]]; then
  echo "ERRO: diretório de testes não encontrado: $TEST_DIR" >&2
  exit 2
fi

echo '== Compilando o analisador sintatico =='
gcc -Wall -Wextra -std=c11 "$PARSER_SOURCE" -o "$PARSER_BINARY" || { echo "ERRO: a compilacao falhou." >&2; exit 2; }
printf 'Executavel gerado: %s\n' "$PARSER_BINARY"

if [[ -z "${PYTHON_BIN:-}" ]]; then
    for candidate in python3 python py; do
        if "$candidate" -c "import sys" >/dev/null 2>&1; then
            PYTHON_BIN="$candidate"
            break
        fi
    done
fi
PYTHON_BIN="${PYTHON_BIN:-python3}"

mapfile -d '' CASES < <(find "$TEST_DIR" -type f -name 'codigo.c' -print0 | sort -z)
TOTAL=${#CASES[@]}
[[ "$TOTAL" -gt 0 ]] || { echo "ERRO: nenhum caso codigo.c encontrado" >&2; exit 2; }

PASS=0
FAIL=0
INDEX=0
printf 'Parser: %s\nDiretório: %s\nCasos encontrados: %d\n' "$PARSER_BINARY" "$TEST_DIR" "$TOTAL"

for source in "${CASES[@]}"; do
    INDEX=$((INDEX + 1))
    dir="$(dirname "$source")"
    name="$(basename "$dir")"
    ast_file="$dir/ast.esperada.txt"
    resultado_file="$dir/resultado.esperado.txt"

    "$PARSER_BINARY" "$source" > output.txt 2>&1
    status=$?

    if [[ -f "$resultado_file" ]] && head -1 "$resultado_file" | grep -q '^REJEITADO'; then
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
