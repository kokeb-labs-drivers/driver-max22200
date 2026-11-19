#!/usr/bin/env python3
"""Local lint for the defect classes the MISRA C++:2023 scan (CodeQL) reported on a driver's first CI run.

A token-level heuristic, standard library only. It tolerates false negatives (the CI scan stays the authority) and
is written to avoid false positives: when a construct cannot be decided without types, it is not reported.

    python3 tools/misra_lint.py                    # the repository this file is in: inc src tests examples
    python3 tools/misra_lint.py --root DIR         # another tree
    python3 tools/misra_lint.py FILE...            # just these files
    python3 tools/misra_lint.py --selftest         # check the checker on small examples

Output, one line per finding: `file:line error|warning Rule N.N.N message`, then a summary line. Exit status 1 when
there is an error. ERROR: 7.0.3, 9.4.1, 0.1.2 (class result), 13.3.3 and KB-CPP-012 in inc/ and src/. WARNING: 8.0.1
anywhere, every finding in tests/ and examples/, 0.1.2 on a scalar result, --void-casts. --strict: warnings fail too.

Rules (the number is the MISRA C++:2023 guideline, as the scan reports it):
  9.4.1   an `if ... else if` chain does not end in a plain `else` (reported at the last `else if`)
  8.0.1   an operand of `&&` or `||` that is a comparison, bitwise or arithmetic expression is not parenthesised;
          a comparison with an unparenthesised arithmetic, shift or bitwise operand
  7.0.3   arithmetic, comparison or conversion on a `char` value: `c - '0'`, `c >= '0'`, `token[0] == '-'`,
          `static_cast<char>(byte)`, `static_cast<uint8_t>(c)`
  0.1.2   a call statement that discards the result of a function the scanned tree declares with a class return
          type (a scalar result is a warning); `(void)expr;` with --void-casts (warning: use `static_cast<void>`)
  13.3.3  the parameter names of a function's declarations and definition differ
  KB-CPP-012  an include in inc/ or src/ outside the driver's library subset (handbook ADR-0017)

A deliberate exception: `// codeql::misra_deviation_next_line(MD-...)` on the line before (as the scan reads it), or
`// misra-lint: ignore <rule>` on the line itself or the line before.
"""

from __future__ import annotations

import argparse
import ast
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCAN_DIRS = ("inc", "src", "tests", "examples")
SUFFIXES = {".hpp", ".cpp", ".ipp"}
# Severity follows what CI blocks on. The scan's `failure` annotations block (7.0.3, 9.4.1, 0.1.2, 13.3.3 in the
# reports of earlier drivers) and `warning` ones (8.0.1) do not; the green mains of earlier drivers also carry such
# findings in tests/ and examples/. So a finding is an ERROR only for a blocking rule in inc/ or src/; every other
# finding is a WARNING (printed, not failing). --strict makes all of them errors.
TRACED = ("inc/", "src/")
SKIP_PARTS = {"scripts", "build", "_site", "site", "node_modules", ".git", "third_party"}  # examples/esp32/scripts is a submodule

# The driver's library subset when handbook_check.py (ci-tools) is not at hand: the list of the handbook's
# KB-CPP-012. With the checkout present its DRIVER_HEADERS is read instead, so the two never drift.
FALLBACK_HEADERS = {"algorithm", "array", "bit", "concepts", "cstddef", "cstdint", "limits", "span", "type_traits",
                    "utility"}

OPS = ["<<=", ">>=", "<=>", "->*", "...", "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
       "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ".*"]
OPEN = {"(": ")", "[": "]", "{": "}"}
CLOSE = {v: k for k, v in OPEN.items()}
BIN_LEVEL = {"*": 10, "/": 10, "%": 10, "+": 9, "-": 9, "<<": 8, ">>": 8, "<": 6, ">": 6, "<=": 6, ">=": 6,
             "==": 5, "!=": 5, "&": 4, "^": 3, "|": 2, "&&": 1, "||": 0}
ARITH = {"*", "/", "%", "+", "-"}
CHAR_OPS = {"+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=", "&", "|", "^", "<<", ">>"}
COMPOUND = {"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="}
ASSIGN = {"="} | COMPOUND
NOT_OPERAND_KW = {"return", "case", "else", "do", "throw", "co_return", "co_yield", "co_await", "new", "delete",
                  "const", "volatile", "typename", "class", "struct", "enum", "union", "using", "namespace",
                  "template", "static", "inline", "constexpr", "consteval", "constinit", "extern", "mutable",
                  "auto", "void", "char", "int", "short", "long", "unsigned", "signed", "bool", "float", "double",
                  "operator", "requires", "default", "goto", "typedef", "friend", "virtual", "explicit"}
STMT_KW = {"if", "for", "while", "switch", "return", "sizeof", "alignof", "decltype", "noexcept", "static_assert",
           "catch", "requires", "operator", "alignas", "typeid", "defined", "asm", "case", "throw", "new", "delete",
           "co_return", "co_await", "co_yield", "else", "do", "static_cast", "reinterpret_cast", "const_cast",
           "dynamic_cast", "using", "typedef", "goto", "default"}
DECL_WORDS = {"static", "inline", "constexpr", "consteval", "constinit", "virtual", "explicit", "friend", "extern",
              "typename", "mutable", "thread_local", "volatile"}


class Tok:
    __slots__ = ("kind", "text", "line")

    def __init__(self, kind: str, text: str, line: int) -> None:
        self.kind, self.text, self.line = kind, text, line


# ----------------------------------------------------------------------------------------------------------------
# Tokenizer
# ----------------------------------------------------------------------------------------------------------------
_NUM = re.compile(r"\.?[0-9](?:[eEpP][+-]|[0-9A-Za-z_.]|'[0-9A-Za-z_])*")
_ID = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_RAW = re.compile(r'(?:u8|u|U|L)?R"([^()\\ ]{0,16})\(')
_INCLUDE = re.compile(r"#\s*include\s*<([^>]+)>")


def tokenize(text: str) -> tuple[list[Tok], dict[int, str], list[tuple[int, str]]]:
    """Tokens, the comment text by line, and the `#include <...>` lines (line, header)."""
    toks: list[Tok] = []
    comments: dict[int, str] = {}
    includes: list[tuple[int, str]] = []
    i, n, line = 0, len(text), 1
    bol = True  # only blanks so far on this line
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            bol = True
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments[line] = comments.get(line, "") + text[i:j]
            i = j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            body = text[i:j]
            comments[line] = comments.get(line, "") + body
            line += body.count("\n")
            i = j
            continue
        if bol and c == "#":  # a preprocessor line, with continuations
            j = i
            while True:
                k = text.find("\n", j)
                k = n if k < 0 else k
                if k > 0 and text[k - 1] == "\\" and k < n:
                    j = k + 1
                    continue
                break
            logical = text[i:k]
            m = _INCLUDE.match(logical)
            if m:
                includes.append((line, m.group(1).strip()))
            line += text.count("\n", i, k)
            i = k
            continue
        bol = False
        m = _RAW.match(text, i)
        if m:
            end = text.find(")" + m.group(1) + '"', m.end())
            end = n if end < 0 else end + len(m.group(1)) + 2
            toks.append(Tok("str", "R", line))
            line += text.count("\n", i, end)
            i = end
            continue
        if c == '"' or (c in "uUL" and re.match(r'(?:u8|u|U|L)"', text[i:i + 3])):
            j = text.find('"', i) + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            toks.append(Tok("str", "S", line))
            i = j + 1
            continue
        pm = re.match(r"(u8|u|U|L)?'", text[i:i + 3]) if c in "uUL'" else None
        if pm:
            j = i + len(pm.group(0))
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            # a plain character literal has type char; u8'x', u'x', U'x', L'x' do not
            toks.append(Tok("chr" if not pm.group(1) else "wchr", text[i:j + 1], line))
            i = j + 1
            continue
        m = _NUM.match(text, i) if (c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit())) else None
        if m:
            toks.append(Tok("num", m.group(0), line))
            i = m.end()
            continue
        m = _ID.match(text, i)
        if m:
            toks.append(Tok("id", m.group(0), line))
            i = m.end()
            continue
        for op in OPS:
            if text.startswith(op, i):
                toks.append(Tok("op", op, line))
                i += len(op)
                break
        else:
            toks.append(Tok("op", c, line))
            i += 1
    return toks, comments, includes


class Unit:
    """One file: tokens, bracket matching, parents, template brackets."""

    def __init__(self, path: str, text: str) -> None:
        self.path = path
        self.t, self.comments, self.includes = tokenize(text)
        t = self.t
        self.match: dict[int, int] = {}
        self.parent: list[int] = [-1] * len(t)
        stack: list[int] = []
        for i, x in enumerate(t):
            self.parent[i] = stack[-1] if stack else -1
            if x.kind == "op" and x.text in OPEN:
                stack.append(i)
            elif x.kind == "op" and x.text in CLOSE:
                while stack and t[stack[-1]].text != CLOSE[x.text]:
                    stack.pop()
                if stack:
                    o = stack.pop()
                    self.match[o] = i
                    self.match[i] = o
                    self.parent[i] = stack[-1] if stack else -1
        self.tmpl: dict[int, int] = {}      # index of `<` -> index of its closing `>` (or `>>`)
        self.tmpl_close: set[int] = set()
        self._templates()

    def txt(self, i: int) -> str:
        return self.t[i].text if 0 <= i < len(self.t) else ""

    def _templates(self) -> None:
        t, n = self.t, len(self.t)
        for i in range(1, n):
            if t[i].text != "<" or i in self.tmpl_close or any(i == o for o in ()):
                continue
            prev = t[i - 1]
            if prev.kind != "id" or prev.text in STMT_KW - {"static_cast"} or prev.text == "operator":
                if not (prev.text in ("static_cast", "reinterpret_cast", "const_cast", "dynamic_cast")):
                    continue
            close = self._match_angle(i, prev.text == "template")
            if close is not None:
                self.tmpl[i] = close
                self.tmpl_close.add(close)

    def _match_angle(self, i: int, loose: bool) -> int | None:
        t, n = self.t, len(self.t)
        depth, j = 1, i + 1
        limit = min(n, i + (400 if loose else 80))
        while j < limit:
            x = t[j]
            s = x.text
            if x.kind == "op":
                if s == "<":
                    depth += 1
                elif s == ">":
                    depth -= 1
                    if depth == 0:
                        return j if loose or self._after_close_ok(j) else None
                elif s == ">>":
                    if depth < 2:
                        return None
                    depth -= 2
                    if depth == 0:
                        return j if loose or self._after_close_ok(j) else None
                elif s in OPEN and s in ("(", "[", "{") and j in self.match:
                    j = self.match[j]
                elif s in ("::", ",", "..."):
                    pass
                elif s in ("*", "&", "&&"):
                    nxt = self.txt(j + 1)
                    if not (nxt in (",", ">", ">>", "*", "&", "&&", "const") or loose):
                        return None
                elif loose and s in ("=", "-", "+", "!", "|", "||", "?", ":", "~", "==", "!=", "<=", "->", "."):
                    pass
                else:
                    return None
            elif x.kind in ("str", "chr"):
                return None
            j += 1
        return None

    def _after_close_ok(self, j: int) -> bool:
        nxt = self.t[j + 1] if j + 1 < len(self.t) else None
        if nxt is None:
            return True
        if nxt.kind == "op":
            return nxt.text not in ("<", "~", "!")  # a variable template may be followed by any binary operator
        return nxt.kind == "id"

    # operand helpers --------------------------------------------------------------------------------------------
    def _declarator_follows(self, i: int) -> bool:
        nxt = self.txt(i)
        if nxt in (";", "=", "{", ",", ")", "[", ":"):
            return True
        if nxt == "(" and i in self.match:  # a member function: `const T* Name() const noexcept {`
            return self.txt(self.match[i] + 1) in ("const", "noexcept", "override", "final", "{", "->", "&")
        return False

    def operand_end(self, i: int) -> bool:
        if i < 0 or i >= len(self.t):
            return False
        x = self.t[i]
        if x.kind == "id":
            return x.text not in NOT_OPERAND_KW
        if x.kind in ("num", "str", "chr", "wchr"):
            return True
        if x.text in (")", "]", "}"):
            return True
        return x.text == ">" and i in self.tmpl_close or x.text == ">>" and i in self.tmpl_close

    def operand_start(self, i: int) -> bool:
        if i >= len(self.t):
            return False
        x = self.t[i]
        if x.kind in ("id", "num", "str", "chr", "wchr"):
            return x.text not in ("const", "volatile") or x.kind != "id"
        return x.text in ("(", "[", "!", "~", "-", "+", "*", "&", "++", "--", "{", "::")

    def is_binary(self, p: int) -> bool:
        """t[p] is a binary operator: an operand ends before it and one starts after it."""
        x = self.t[p]
        if x.kind != "op" or p in self.tmpl or p in self.tmpl_close:
            return False
        if p > 0 and self.t[p - 1].text == "operator":
            return False
        if x.text in ("*", "&") and self.t[p - 1].kind == "id" and self.txt(p + 1) != "" \
                and self.t[p + 1].kind == "id" and self._declarator_follows(p + 2):
            return False  # `Type* name`, `Type& name`: a declarator, not an operation
        return self.operand_end(p - 1) and self.operand_start(p + 1)


# ----------------------------------------------------------------------------------------------------------------
# Findings and suppressions
# ----------------------------------------------------------------------------------------------------------------
class Findings:
    def __init__(self) -> None:
        self.items: list[tuple[str, int, str, str, str]] = []

    def add(self, u: Unit, line: int, rule: str, msg: str, sev: str = "error") -> None:
        for ln in (line, line - 1):
            c = u.comments.get(ln, "")
            if ln == line - 1 and "codeql::misra_deviation_next_line(" in c:
                return
            m = re.search(r"misra-lint:\s*ignore\s+([\w.\-]+)", c)
            if m and m.group(1) in (rule, rule.replace("Rule ", "")):
                return
        self.items.append((u.path, line, rule, sev, msg))


# ----------------------------------------------------------------------------------------------------------------
# Rule 9.4.1
# ----------------------------------------------------------------------------------------------------------------
def skip_stmt(u: Unit, i: int) -> int:
    t, n = u.t, len(u.t)
    if i >= n:
        return n
    s = t[i].text
    if s == "{":
        return u.match.get(i, n - 1) + 1
    if s == "if":
        return parse_if(u, i)[0]
    if s in ("for", "while", "switch"):
        j = i + 1
        if j < n and t[j].text == "(" and j in u.match:
            j = u.match[j] + 1
        return skip_stmt(u, j)
    if s == "do":
        j = skip_stmt(u, i + 1)
        while j < n and t[j].text != ";":
            j = u.match[j] + 1 if t[j].text in OPEN and j in u.match else j + 1
        return j + 1
    j = i
    while j < n:
        x = t[j].text
        if t[j].kind == "op" and x in OPEN and j in u.match:
            j = u.match[j] + 1
            continue
        if x == ";":
            return j + 1
        if x == "}":
            return j
        j += 1
    return n


def parse_if(u: Unit, i: int) -> tuple[int, int, bool, int]:
    """(end, links, ends_with_else, index of the last `if` of the chain) for the `if` at i."""
    t, n = u.t, len(u.t)
    j = i + 1
    while j < n and t[j].text in ("constexpr", "consteval", "!"):
        j += 1
    if j < n and t[j].text == "(" and j in u.match:
        j = u.match[j] + 1
    end = skip_stmt(u, j)
    if end < n and t[end].text == "else":
        if end + 1 < n and t[end + 1].text == "if":
            e, links, term, last = parse_if(u, end + 1)
            return e, links + 1, term, last
        return skip_stmt(u, end + 1), 1, True, i
    return end, 1, False, i


def check_9_4_1(u: Unit, out: Findings) -> None:
    for i, x in enumerate(u.t):
        if x.kind == "id" and x.text == "if" and u.txt(i - 1) != "else":
            _, links, term, last = parse_if(u, i)
            if links >= 2 and not term:
                out.add(u, u.t[last].line, "Rule 9.4.1",
                        f"the `if ... else if` chain starting at line {x.line} does not end in an `else`")


# ----------------------------------------------------------------------------------------------------------------
# Rule 8.0.1
# ----------------------------------------------------------------------------------------------------------------
def check_8_0_1(u: Unit, out: Findings) -> None:
    t, n = u.t, len(u.t)
    # every bracket group (and the file) is scanned on its own, nested groups being atoms of it
    groups = [(-1, 0, n)] + [(o, o + 1, c) for o, c in u.match.items() if t[o].text in OPEN]
    for _, lo, hi in groups:
        seq: list[tuple[int, str]] = []  # (token index, operator) of the binary operators of the current segment
        k = lo
        while k < hi:
            x = t[k]
            s = x.text
            if x.kind == "op" and s in OPEN and k in u.match:
                if s == "{":  # a block or a braced initialiser ends the segment (a declaration's body)
                    flag_pairs(u, seq, out)
                    seq = []
                k = u.match[k] + 1
                continue
            if k in u.tmpl:  # `name<...>`: an atom
                k = u.tmpl[k] + 1
                continue
            if x.kind == "op" and (s in (",", ";", "?", ":") or s in ASSIGN or s in ("{", "}")):
                flag_pairs(u, seq, out)
                seq = []
            elif x.kind == "id" and s in ("return", "case", "else", "co_return", "throw", "default"):
                flag_pairs(u, seq, out)
                seq = []
            elif x.kind == "op" and s in BIN_LEVEL and u.is_binary(k):
                seq.append((k, s))
            k += 1
        flag_pairs(u, seq, out)


LOGICAL = {"&&", "||"}
COMPARE = {"<", ">", "<=", ">=", "==", "!="}
BITWISE = {"&", "^", "|"}
STRICT_PRECEDENCE = False  # --strict-precedence: also arithmetic and shifts under a comparison or bitwise operator


def needs_parentheses(a: str, b: str) -> bool:
    """Whether adjacent operators a and b of different precedence need parentheses, as the scan reports them: a
    comparison or bitwise operand of && or ||, a comparison operand of a bitwise operator, && with ||."""
    if a in LOGICAL and b in LOGICAL:
        return a != b
    for x, y in ((a, b), (b, a)):
        if x in LOGICAL and (y in COMPARE or y in BITWISE):
            return True
        if x in BITWISE and y in COMPARE:
            return True
    if STRICT_PRECEDENCE:
        return not (a in ARITH and b in ARITH) and BIN_LEVEL[a] != BIN_LEVEL[b]
    return False


def flag_pairs(u: Unit, seq: list[tuple[int, str]], out: Findings) -> None:
    for (ia, a), (ib, b) in zip(seq, seq[1:]):
        la, lb = BIN_LEVEL[a], BIN_LEVEL[b]
        if la == lb or not needs_parentheses(a, b):
            continue
        child, parent = (ia, b) if la > lb else (ib, a)
        # `a == b == c` style chains of the same level and `a < b == c` are reported too: different operators
        out.add(u, u.t[child].line, "Rule 8.0.1",
                f"operator '{u.t[child].text}' is an unparenthesised operand of '{parent}'; add parentheses", "warning")


# ----------------------------------------------------------------------------------------------------------------
# Rule 7.0.3
# ----------------------------------------------------------------------------------------------------------------
CHAR_WORDS = {"char", "signed", "unsigned"}
SEQ_TEMPLATES = {"span", "array", "string_view", "basic_string_view"}
INT_TYPES = {"uint8_t", "int8_t", "uint16_t", "int16_t", "uint32_t", "int32_t", "uint64_t", "int64_t", "int", "unsigned",
             "size_t", "long", "short"}


def scope_end(u: Unit, i: int) -> int:
    """The last token the declaration at i is visible in: the enclosing block, or a function's whole body."""
    g = u.parent[i]
    if g < 0:
        return len(u.t)
    if u.t[g].text == "(" and g in u.match:
        c = u.match[g]
        k = c + 1
        while k < len(u.t) and u.t[k].text not in ("{", ";", "=", ","):
            if u.t[k].text in OPEN and k in u.match:
                k = u.match[k]
            k += 1
        if k < len(u.t) and u.t[k].text == "{" and k in u.match and (u.txt(g - 1) != "" and u.t[g - 1].kind == "id"):
            return u.match[k]
        return c
    return u.match.get(g, len(u.t))


def char_declarations(u: Unit) -> tuple[list[tuple[str, int, int]], list[tuple[str, int, int]], list[tuple[str, int, int]]]:
    """(scalar chars, char sequences, byte scalars) as (name, first token, last token of its scope)."""
    t, n = u.t, len(u.t)
    scal: list[tuple[str, int, int]] = []
    seq: list[tuple[str, int, int]] = []
    u8: list[tuple[str, int, int]] = []

    def after_name(j: int) -> bool:
        return u.txt(j + 1) in ("=", ";", ",", ")", "{", "[", ":")

    for i, x in enumerate(t):
        if x.kind != "id":
            continue
        if x.text in ("uint8_t", "unsigned", "char") or x.text in SEQ_TEMPLATES:
            pass
        else:
            continue
        j = i + 1
        if x.text == "unsigned" and u.txt(j) == "char":
            j += 1
            is_u8 = True
        elif x.text == "unsigned":
            continue
        else:
            is_u8 = x.text == "uint8_t"
        if x.text in SEQ_TEMPLATES:
            if u.txt(j) != "<" or j not in u.tmpl:
                continue
            inner = [y.text for y in t[j + 1:u.tmpl[j]]]
            first = inner[:1] + inner[1:2]
            if not ("char" in first and "unsigned" not in inner[:2]):
                continue
            j = u.tmpl[j] + 1
            kind = seq
        elif x.text == "char" or (x.text == "unsigned"):
            if i > 0 and t[i - 1].text in ("unsigned", "signed"):
                continue
            kind = scal
        else:
            kind = u8
        if x.text == "char" and i > 0 and t[i - 1].text == "<":
            continue  # a template argument, not a declaration
        while u.txt(j) in ("const", "volatile"):
            j += 1
        ptr = False
        while u.txt(j) in ("*", "&", "&&"):
            ptr = ptr or u.txt(j) == "*"
            j += 1
        while u.txt(j) in ("const", "volatile"):
            j += 1
        if j >= n or t[j].kind != "id" or t[j].text in NOT_OPERAND_KW or not after_name(j):
            continue
        k = seq if (ptr and kind is scal) else kind
        if u.txt(j + 1) == "[" and k is scal:
            k = seq
        k.append((t[j].text, j, scope_end(u, j)))
        if kind is scal and is_u8:
            u8.append((t[j].text, j, scope_end(u, j)))
    return scal, seq, u8


def check_7_0_3(u: Unit, out: Findings) -> None:
    t, n = u.t, len(u.t)
    reported: set[tuple[int, str]] = set()

    def report(line: int, msg: str) -> None:
        if (line, msg) not in reported:
            reported.add((line, msg))
            out.add(u, line, "Rule 7.0.3", msg)

    def binary_neighbour(p: int, *, left: bool) -> str | None:
        """The operator that makes the operand ending at (left=False) / starting at (left=True) p arithmetic."""
        if left:  # operand starts at p: the operator before it
            q = p - 1
            if q >= 0 and t[q].kind == "op" and (t[q].text in CHAR_OPS or t[q].text in COMPOUND) and (
                    u.is_binary(q) or t[q].text in COMPOUND):
                return t[q].text
        else:  # operand ends at p: the operator after it
            q = p + 1
            if q < n and t[q].kind == "op" and (t[q].text in CHAR_OPS or t[q].text in COMPOUND) and (
                    u.is_binary(q) or t[q].text in COMPOUND):
                return t[q].text
        return None

    # a character literal operand of arithmetic, comparison or bitwise operators
    for i, x in enumerate(t):
        if x.kind == "chr":
            op = binary_neighbour(i, left=True) or binary_neighbour(i, left=False)
            if op:
                report(x.line, f"character literal {x.text} used as a number with '{op}'; "
                               "compare and convert through a table or std::bit_cast, not its numerical value")
    scal, seq, u8 = char_declarations(u)
    for name, first, last in scal:
        for i in range(first + 1, min(last + 1, n)):
            x = t[i]
            if x.kind != "id" or x.text != name or u.txt(i - 1) in (".", "->", "::"):
                continue
            op = binary_neighbour(i, left=True) or binary_neighbour(i, left=False)
            if op:
                report(x.line, f"character '{name}' used as a number with '{op}'")
    for name, first, last in seq:
        for i in range(first + 1, min(last + 1, n)):
            x = t[i]
            if x.kind != "id" or x.text != name or u.txt(i - 1) in (".", "->", "::") or u.txt(i + 1) != "[":
                continue
            close = u.match.get(i + 1, i + 1)
            op = binary_neighbour(i, left=True) or binary_neighbour(close, left=False)
            if op:
                report(x.line, f"character '{name}[...]' used as a number with '{op}'")
    # conversions: static_cast<integer>(char) and static_cast<char>(byte)
    char_names = {nm: (f, l) for nm, f, l in scal}
    seq_names = {nm: (f, l) for nm, f, l in seq}
    u8_names = {nm: (f, l) for nm, f, l in u8}
    for i, x in enumerate(t):
        if x.kind != "id" or x.text != "static_cast" or u.txt(i + 1) != "<" or (i + 1) not in u.tmpl:
            continue
        close = u.tmpl[i + 1]
        target = [y.text for y in t[i + 2:close]]
        p = close + 1
        if u.txt(p) != "(" or p not in u.match:
            continue
        arg = t[p + 1:u.match[p]]
        if not arg:
            continue
        tgt = target[-1] if target else ""
        to_char = target in (["char"], ["signed", "char"], ["unsigned", "char"])
        to_int = tgt in INT_TYPES and not to_char

        def declared(names: dict[str, tuple[int, int]], name: str, at: int) -> bool:
            return name in names and names[name][0] < at <= names[name][1]

        a0 = arg[0]
        single = len(arg) == 1
        if to_int and single and a0.kind == "chr":
            report(x.line, f"conversion of the character {a0.text} to '{tgt}' uses its numerical value")
        elif to_int and single and a0.kind == "id" and declared(char_names, a0.text, i):
            report(x.line, f"conversion of character '{a0.text}' to '{tgt}' uses its numerical value")
        elif to_int and a0.kind == "id" and len(arg) >= 4 and arg[1].text == "[" and arg[-1].text == "]" \
                and declared(seq_names, a0.text, i):
            report(x.line, f"conversion of character '{a0.text}[...]' to '{tgt}' uses its numerical value")
        elif to_char and single and a0.kind == "id" and declared(u8_names, a0.text, i):
            report(x.line, f"conversion of byte '{a0.text}' to 'char' uses its numerical value; use std::bit_cast")
        elif to_char and single and a0.kind == "num" and not re.fullmatch(r"0[xX]?0*", a0.text):
            report(x.line, f"conversion of the number {a0.text} to 'char' uses it as a character value")


# ----------------------------------------------------------------------------------------------------------------
# Rule 0.1.2
# ----------------------------------------------------------------------------------------------------------------
BOUNDARY = {";", "{", "}"}


SCALAR_RET = re.compile(r"u?int(?:8|16|32|64)_t|size_t|ptrdiff_t|bool|char|int|unsigned|signed|long|short|float|double|std|::|const")


def enclosing_class(u: Unit, idx: int) -> str | None:
    g = u.parent[idx]
    if g < 0 or u.t[g].text != "{":
        return None
    for j in range(g - 1, max(g - 60, -1), -1):
        if u.t[j].text in (";", "}", "{"):
            return None
        if u.t[j].text in ("class", "struct") and j + 1 < len(u.t):
            return u.t[j + 1].text
    return None


def declared_functions(u: Unit, sink: list | None = None) -> dict[str, set[str]]:
    """name -> {'void', 'value'} for every function declaration or definition token-pattern found."""
    t, n = u.t, len(u.t)
    found: dict[str, set[str]] = {}
    for k, x in enumerate(t):
        if x.text != "(" or k < 2 or k not in u.match or t[k - 1].kind != "id" or t[k - 1].text in STMT_KW:
            continue
        # the name may be qualified: A<T>::name
        c = u.match[k]
        j = c + 1
        while j < n and t[j].text in ("const", "noexcept", "override", "final", "&", "&&", "volatile"):
            j = u.match[j + 1] + 1 if t[j].text == "noexcept" and u.txt(j + 1) == "(" and (j + 1) in u.match else j + 1
        trailing = None
        if j < n and t[j].text == "->":
            e = j + 1
            while e < n and t[e].text not in ("{", ";", "="):
                e += 1
            trailing = [y.text for y in t[j + 1:e]]
            j = e
        if j >= n or t[j].text not in ("{", ";", "="):
            continue
        # walk back over `Class<..>::` qualifiers, then to the statement boundary
        p = k - 2
        qual = None
        while p >= 1 and t[p].text == "::":
            p -= 1
            if t[p].text in (">", ">>") and p in u.tmpl_close:
                o = next((a for a, b in u.tmpl.items() if b == p), None)
                if o is None:
                    break
                p = o - 1
            qual = qual or t[p].text
            p -= 1
        q = p
        while q >= 0 and t[q].text not in BOUNDARY and not (t[q].text == ":" and q > 0 and t[q - 1].kind == "id"
                                                            and t[q - 1].text in ("public", "private", "protected")):
            if t[q].text in (")", "]") and q in u.match:
                q = u.match[q]
            q -= 1
        seg = [y for y in t[q + 1:p + 1]]
        words = [y.text for y in seg]
        if "template" in words[:1]:
            e = 1
            if len(words) > 1 and words[1] == "<" and (q + 2) in u.tmpl:
                e = u.tmpl[q + 2] - q
            seg, words = seg[e:], words[e:]
        # drop attributes [[...]] and declaration specifiers
        cleaned: list[str] = []
        s = 0
        while s < len(seg):
            if seg[s].text == "[" and s + 1 < len(seg) and seg[s + 1].text == "[":
                while s < len(seg) and not (seg[s].text == "]" and s > 0 and seg[s - 1].text == "]"):
                    s += 1
                s += 1
                continue
            if seg[s].text not in DECL_WORDS:
                cleaned.append(seg[s].text)
            s += 1
        if not cleaned:
            continue
        if any(w in ("=", "return", ".", "->", "(", ")", "operator", "else", "new", "throw", "case", ",") for w in cleaned
               if w != ","):
            continue
        if cleaned[0] in STMT_KW or t[k - 1].text == "operator":
            continue
        ret = trailing if (cleaned == ["auto"] and trailing is not None) else cleaned
        kind = "void" if ret == ["void"] else "ref" if ret[-1] == "&" else \
            "scalar" if all(SCALAR_RET.fullmatch(w) for w in ret) else "class"
        found.setdefault(t[k - 1].text, set()).add(kind)
        if sink is not None:
            sink.append((t[k - 1].text, qual or enclosing_class(u, k - 1), k))
    return found


def check_0_1_2(units: list[Unit], out: Findings, void_casts: bool = False) -> None:
    value_only: set[str] = set()
    names: dict[str, set[str]] = {}
    for u in units:
        for nm, kinds in declared_functions(u).items():
            names.setdefault(nm, set()).update(kinds)
    value_only = {nm for nm, kinds in names.items() if kinds and kinds <= {"class", "scalar"}}
    class_only = {nm for nm, kinds in names.items() if kinds == {"class"}}
    for u in units:
        t, n = u.t, len(u.t)
        for i, x in enumerate(t):
            # (void)expr; -- a C-style cast
            if void_casts and x.text == "(" and u.txt(i + 1) == "void" and u.txt(i + 2) == ")":
                before = t[i - 1] if i > 0 else None
                nxt = t[i + 3] if i + 3 < n else None
                if nxt is None or nxt.text in ("const", "noexcept", ";", "{", "->", "override", "final", ")", ",", "=",
                                                "volatile", "requires", "&", "&&"):
                    continue
                if before is not None and before.kind == "id" and before.text not in ("return", "else", "case", "do"):
                    continue
                if before is not None and before.text in (">", "operator") and before.text == "operator":
                    continue
                out.add(u, x.line, "Rule 0.1.2", "C-style cast to void; discard with static_cast<void>(...)",
                        "warning")
            # a call statement whose result is dropped
            if i == 0 or t[i - 1].text in BOUNDARY or t[i - 1].text == "else" or t[i - 1].text == "do" \
                    or (t[i - 1].text == ")" and (t[i - 1] and t[i - 1].text == ")")
                        and (i - 1) in u.match and u.txt(u.match[i - 1] - 1) in ("if", "for", "while")) \
                    or (t[i - 1].text == ":" and _after_label(u, i - 1)):
                if x.kind == "id" and x.text not in STMT_KW and x.text not in NOT_OPERAND_KW:
                    name = _discarded_call(u, i)
                    if name and name in value_only and not in_requires_body(u, i):
                        out.add(u, x.line, "Rule 0.1.2",
                                f"the value returned by '{name}' is discarded; use it, or static_cast<void>(...)",
                                "error" if name in class_only else "warning")


_NOT_PARAM_NAME = {"const", "volatile", "unsigned", "signed", "long", "short", "struct", "class", "enum", "typename",
                   "int", "char", "bool", "float", "double", "void", "auto"}


def parse_params(u: Unit, k: int) -> list[tuple[str | None, str]]:
    """(name or None, type text) of each parameter of the function whose `(` is at k."""
    t, c = u.t, u.match[k]
    items: list[list[Tok]] = []
    cur: list[Tok] = []
    j = k + 1
    while j < c:
        if t[j].text in ("(", "[", "{") and j in u.match:
            cur.extend(t[j:u.match[j] + 1])
            j = u.match[j] + 1
            continue
        if j in u.tmpl:
            cur.extend(t[j:u.tmpl[j] + 1])
            j = u.tmpl[j] + 1
            continue
        if t[j].text == ",":
            items.append(cur)
            cur = []
        else:
            cur.append(t[j])
        j += 1
    if cur:
        items.append(cur)
    out: list[tuple[str | None, str]] = []
    for toks in items:
        for e, x in enumerate(toks):
            if x.text == "=":
                toks = toks[:e]
                break
        name = None
        if len(toks) >= 2 and toks[-1].kind == "id" and toks[-1].text not in _NOT_PARAM_NAME \
                and toks[-2].text not in ("::", "const", "volatile", "unsigned", "signed", "long", "short", "struct",
                                          "class", "enum", "typename", "<", ","):
            name = toks[-1].text
            toks = toks[:-1]
        out.append((name, " ".join(x.text for x in toks)))
    return out


def check_13_3_3(units: list[Unit], out: Findings) -> None:
    groups: dict[tuple, list[tuple[Unit, int, list[str | None]]]] = {}
    for u in units:
        sink: list = []
        declared_functions(u, sink)
        for name, cls, k in sink:
            params = parse_params(u, k)
            # classes of tests and examples are local to their file; the driver's header and implementation pair up
            key = (cls, name, tuple(ty for _, ty in params), "" if u.path.startswith(TRACED) else u.path)
            groups.setdefault(key, []).append((u, k, [nm for nm, _ in params]))
    for (cls, name, _, _), decls in groups.items():
        if len(decls) < 2:
            continue
        mismatch = any(len({names[i] for _, _, names in decls if names[i] is not None}) > 1
                       for i in range(len(decls[0][2])))
        if mismatch:
            for u, k, names in decls:
                out.add(u, u.t[k - 1].line, "Rule 13.3.3",
                        f"the parameter names of '{name}' ({', '.join(n or '_' for n in names)}) differ from its "
                        "other declaration")


def in_requires_body(u: Unit, i: int) -> bool:
    g = u.parent[i]
    while g >= 0:
        if u.t[g].text == "{" and g > 0:
            b = u.t[g - 1]
            if b.text == "requires" or (b.text == ")" and (g - 1) in u.match and u.txt(u.match[g - 1] - 1) == "requires"):
                return True
        g = u.parent[g]
    return False


def _after_label(u: Unit, colon: int) -> bool:
    j = colon - 1
    for _ in range(10):
        if j < 0:
            return False
        s = u.t[j].text
        if s == "default":
            return True
        if s == "case":
            return True
        if s in BOUNDARY or s in ("?", "(", ")"):
            return False
        j -= 1
    return False


def _discarded_call(u: Unit, i: int) -> str | None:
    """The callee name when the statement starting at i is a plain call chain `a.b->c::f(args)...;`."""
    t, n = u.t, len(u.t)
    j = i
    last: str | None = None
    while j < n:
        x = t[j]
        if x.kind == "id" and x.text not in STMT_KW:
            if last is not None and j > i and (t[j - 1].kind == "id" or j - 1 in u.tmpl_close):
                return None  # `Type name(...)`, `Type<T> name(...)`: a declaration
            last = x.text
            j += 1
            if j in u.tmpl:
                j = u.tmpl[j] + 1
            continue
        if x.text in (".", "->", "::") and j + 1 < n and t[j + 1].kind == "id":
            j += 1
            continue
        if x.text == "(" and j in u.match and last is not None and t[j - 1].kind in ("id",) or (
                x.text == "(" and j in u.match and t[j - 1].text in (")", ">") and j - 1 in (set(u.tmpl_close) | set(u.match))):
            j = u.match[j] + 1
            if j < n and t[j].text == ";":
                return last
            if j < n and t[j].text in (".", "->"):
                continue
            return None
        if x.text == "[" and j in u.match:
            j = u.match[j] + 1
            last = None
            continue
        return None
    return None


# ----------------------------------------------------------------------------------------------------------------
# KB-CPP-012
# ----------------------------------------------------------------------------------------------------------------
def load_driver_headers(ci_tools: Path | None) -> tuple[set[str], str]:
    """DRIVER_HEADERS of handbook_check.py when its checkout is at hand, else the fallback list."""
    cands = []
    if ci_tools:
        cands.append(ci_tools / "tools/handbook_check/handbook_check.py")
    env = os.environ.get("CI_TOOLS_DIR")
    if env:
        cands.append(Path(env) / "tools/handbook_check/handbook_check.py")
    cands.append(ROOT.parent / "ci-tools/tools/handbook_check/handbook_check.py")
    for c in cands:
        try:
            tree = ast.parse(c.read_text(encoding="utf-8"))
        except (OSError, SyntaxError):
            continue
        for node in tree.body:
            if isinstance(node, ast.Assign) and any(isinstance(g, ast.Name) and g.id == "DRIVER_HEADERS"
                                                   for g in node.targets):
                try:
                    return set(ast.literal_eval(node.value)), str(c)
                except ValueError:
                    pass
    return set(FALLBACK_HEADERS), "built-in list"


def check_includes(u: Unit, out: Findings, allowed: set[str]) -> None:
    if not u.path.startswith(("inc/", "src/")):
        return
    for line, header in u.includes:
        if header not in allowed:
            out.add(u, line, "KB-CPP-012", f"<{header}> is not in the driver's library subset ({', '.join(sorted(allowed))})")


# ----------------------------------------------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------------------------------------------
def collect(root: Path, files: list[str]) -> list[str]:
    if files:
        return files
    found: list[str] = []
    for d in SCAN_DIRS:
        base = root / d
        if not base.is_dir():
            continue
        for dirpath, dirs, names in os.walk(base):
            dirs[:] = sorted(x for x in dirs if x not in SKIP_PARTS and not x.startswith("build"))
            for nm in sorted(names):
                if Path(nm).suffix in SUFFIXES:
                    found.append(os.path.relpath(os.path.join(dirpath, nm), root).replace(os.sep, "/"))
    return found


def lint_texts(sources: dict[str, str], allowed: set[str], void_casts: bool = False,
               strict: bool = False) -> list[tuple[str, int, str, str, str]]:
    """Findings as (path, line, rule, severity, message). A finding blocks (error) when the CI scan blocks on its
    rule and it is in inc/ or src/; everything else is a warning. `strict` makes every finding an error."""
    units = [Unit(p, s) for p, s in sources.items()]
    out = Findings()
    for u in units:
        check_9_4_1(u, out)
        check_includes(u, out, allowed)
        check_8_0_1(u, out)
        check_7_0_3(u, out)
    check_0_1_2(units, out, void_casts)
    check_13_3_3(units, out)
    best: dict[tuple[str, int, str], tuple[str, int, str, str, str]] = {}
    for path, line, rule, sev, msg in out.items:  # one finding per file, line and rule, as the scan reports them
        if strict:
            sev = "error"
        elif not path.startswith(TRACED):
            sev = "warning"
        key = (path, line, rule)
        if key not in best or (sev == "error" and best[key][3] != "error"):
            best[key] = (path, line, rule, sev, msg)
    return sorted(best.values(), key=lambda f: (f[0], f[1], f[2]))


def run(root: Path, files: list[str], ci_tools: Path | None, quiet: bool, void_casts: bool = False,
        strict: bool = False) -> int:
    rels = collect(root, files)
    sources = {}
    for r in rels:
        p = root / r
        sources[r] = p.read_text(encoding="utf-8", errors="replace")
    allowed, where = load_driver_headers(ci_tools)
    found = lint_texts(sources, allowed, void_casts, strict)
    for path, line, rule, sev, msg in found:
        print(f"{path}:{line} {sev} {rule} {msg}")
    errors = sum(1 for f in found if f[3] == "error")
    if not quiet or found:
        print(f"misra_lint: {len(rels)} file(s), {errors} error(s), {len(found) - errors} warning(s); "
              f"include subset from {where}")
    return 1 if errors else 0


# ----------------------------------------------------------------------------------------------------------------
# Self-test: each case is (name, file, source, expected [(line, rule)]); a good case expects nothing.
# ----------------------------------------------------------------------------------------------------------------
CASES: list[tuple[str, str, str, list[tuple[int, str]]]] = [
    ("else-if without else", "inc/a.hpp", """\
int f(int x) {
  if (x == 1) {
    return 1;
  } else if (x == 2) {
    return 2;
  }
  return 0;
}
""", [(4, "Rule 9.4.1", "error")]),
    ("else-if with else", "inc/a.hpp", """\
int f(int x) {
  if (x == 1) {
    return 1;
  } else if (x == 2) {
    return 2;
  } else {
    return 0;
  }
}
int g(int x) {
  if (x == 1) {
    return 1;
  }
  return 0;
}
""", []),
    ("nested chain inside a lambda", "src/a.ipp", """\
void h(int x) {
  auto l = [&](int y) {
    if (y == 1) {
      x = 1;
    } else if (y == 2) {
      x = 2;
    }
  };
  if (x) l(1);
  else l(2);
}
""", [(5, "Rule 9.4.1", "error")]),
    ("else-if chain with a braceless last", "tests/a.cpp", """\
void h(int x, int& y) {
  if (x == 1) y = 1;
  else if (x == 2) y = 2;
  else if (x == 3) y = 3;
}
""", [(4, "Rule 9.4.1", "warning")]),
    ("and/or with comparisons", "inc/b.hpp", """\
constexpr bool f(int a, int b) {
  return a == 1 && b == 2;
}
static_assert(f(1, 2) == true && f(2, 2) == false);
constexpr bool g(unsigned a, unsigned b) {
  return (a >= 4U) && (a < b + 8U) || (a & 3U) == 0U;
}
""", [(2, "Rule 8.0.1", "warning"), (4, "Rule 8.0.1", "warning"), (6, "Rule 8.0.1", "warning")]),
    ("parenthesised and templates", "inc/b.hpp", """\
template <typename T>
  requires(std::is_same_v<T, int> || std::is_same_v<T, long>)
constexpr bool f(T a, T b) {
  const auto x = std::array<int, 3>{1, 2, 3};
  auto lam = [&](int q) { return (q < 3) && (q > 1); };
  return ((a == 1) && (b == 2)) || (lam(a) && !(a < b));
}
constexpr bool g(unsigned a, unsigned b) {
  return (a < (b + 8U)) && ((sizeof(T) * 2 + 1) > 3) ? a * b + 1 : b;
}
template <class T, class U>
concept Both = std::is_same_v<T, U> && std::is_integral_v<T>;
""", []),
    ("char arithmetic", "inc/c.hpp", """\
constexpr int Digit(char c) {
  if (c >= '0') {
    return c - '0';
  }
  return 0;
}
constexpr char Next(std::span<const char> token) {
  const auto b = static_cast<uint8_t>(token[0]);
  if (token[0] == '-') {
    return static_cast<char>('0' + 1);
  }
  return static_cast<char>(b);
}
""", [(2, "Rule 7.0.3", "error"), (3, "Rule 7.0.3", "error"), (8, "Rule 7.0.3", "error"),
                            (9, "Rule 7.0.3", "error"), (10, "Rule 7.0.3", "error")]),
    ("char done without numbers", "inc/c.hpp", """\
constexpr char kA = 'a';
constexpr std::array<char, 2> kTable{'0', '1'};
constexpr bool Same(char a, char b) {
  return std::array<char, 1>{a} == std::array<char, 1>{b};
}
constexpr int Count(int c, std::span<const int> v) {
  return c - 1 + v[0];
}
constexpr char Make(std::uint8_t byte) {
  return std::bit_cast<char>(byte);
}
""", []),
    ("void cast and dropped result", "src/d.ipp", """\
struct Step {
  bool ended;
};
class Foo {
public:
  Step Fail(int e) noexcept;
  void Reset() noexcept;
  bool Read() noexcept;
};
Step Foo::Fail(int e) noexcept {
  return Step{e != 0};
}
void Use(Foo& f, int tx) {
  (void)tx;
  f.Fail(1);
  f.Reset();
  static_cast<void>(f.Fail(2));
  const auto s = f.Fail(3);
  if (s.ended) f.Read();
}
""", [(14, "Rule 0.1.2", "warning"), (15, "Rule 0.1.2", "error"), (19, "Rule 0.1.2", "warning")]),
    ("examples are warnings only", "examples/esp32/main/x.cpp", """\
bool f(int a, int b) {
  (void)a;
  return a == 1 || b != 2;
}
""", [(2, "Rule 0.1.2", "warning"), (3, "Rule 8.0.1", "warning")]),
    ("void parameter list is no cast", "inc/e.hpp", """\
int f(void) noexcept;
int g(void);
""", []),
    ("include subset", "inc/f.hpp", """\
#include <cstdint>
#include <string>
#include <cstring>
""", [(2, "KB-CPP-012", "error"), (3, "KB-CPP-012", "error")]),
    ("deviation and ignore comments", "inc/g.hpp", """\
constexpr bool f(int a, int b) {
  // codeql::misra_deviation_next_line(MD-CPP-001)
  return a == 1 && b == 2;
}
constexpr bool g(int a, int b) {
  return a == 1 && b == 2; // misra-lint: ignore 8.0.1
}
""", []),
    ("char arithmetic in tests is a warning", "tests/i.cpp", """\
int Digit(char c) {
  return c - '0';
}
""", [(2, "Rule 7.0.3", "warning")]),
    ("tests may include anything", "tests/h.cpp", """\
#include <string>
#include <vector>
""", []),
]


def selftest() -> int:
    bad = 0
    for name, path, src, want in CASES:
        got = [(ln, rule, sev) for _, ln, rule, sev, _ in lint_texts({path: src}, set(FALLBACK_HEADERS), True)]
        if got != sorted(want):
            bad += 1
            print(f"selftest {name!r}: got {got}, expected {sorted(want)}")
    # strict: every finding is an error
    got = [sev for *_, sev, _ in lint_texts({"tests/i.cpp": CASES[-2][2]}, set(FALLBACK_HEADERS), False, True)]
    if got != ["error"]:
        bad += 1
        print(f"selftest 'strict': got {got}, expected ['error']")
    # 13.3.3 across a header and its implementation
    header = "class Foo {\npublic:\n  int Check(int channel, bool fast) noexcept;\n  int Same(int a) noexcept;\n};\n"
    impl = "int Foo::Check(int ch, bool fast) noexcept {\n  return ch;\n}\nint Foo::Same(int a) noexcept {\n  return a;\n}\n"
    got = [(p, ln, rule, sev) for p, ln, rule, sev, _ in
           lint_texts({"inc/foo.hpp": header, "src/foo.ipp": impl}, set(FALLBACK_HEADERS))]
    want = [("inc/foo.hpp", 3, "Rule 13.3.3", "error"), ("src/foo.ipp", 1, "Rule 13.3.3", "error")]
    if got != want:
        bad += 1
        print(f"selftest '13.3.3': got {got}, expected {want}")
    if bad:
        return 1
    print("misra_lint selftest passed")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="files relative to --root (default: every C++ file of inc src tests examples)")
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--ci-tools", default="", help="a ci-tools checkout: the include subset comes from its handbook_check.py")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--strict", action="store_true", help="warnings fail too (for reviewers)")
    ap.add_argument("--strict-precedence", action="store_true",
                    help="8.0.1: also arithmetic and shifts under comparison and bitwise operators (`a < b + 1`)")
    ap.add_argument("--void-casts", action="store_true",
                    help="also report C-style `(void)expr;` (the scan accepts it; MISRA 8.2.2 does not)")
    ap.add_argument("--selftest", action="store_true", help="check the checker on small examples")
    a = ap.parse_args()
    global STRICT_PRECEDENCE
    STRICT_PRECEDENCE = a.strict_precedence
    if a.selftest:
        return selftest()
    return run(Path(a.root).resolve(), a.files, Path(a.ci_tools) if a.ci_tools else None, a.quiet, a.void_casts, a.strict)


if __name__ == "__main__":
    sys.exit(main())
