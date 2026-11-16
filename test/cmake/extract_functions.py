"""Extract whole C++ function definitions from production sources for host tests.

Usage: extract_functions.py <source> <signature>...

Each signature is exact source text that starts a function definition, such as
"void Foo::bar(" or "int Foo::count() const". It must match exactly one
definition; declarations, calls, defaulted functions, comments and literals do
not count. The result runs from the start of the signature's line to the
definition's closing brace, so edits to neighbouring functions do not move it.

This is a lexer with brace matching, not a C++ parser. It fails rather than
guess on unterminated literals or comments, braces that do not balance, braces
that differ between preprocessor branches, and definitions that start on an
earlier line than the signature (template headers, attributes).
"""

import bisect
import re
import sys

TOKEN = re.compile(
    r"""
    (?P<directive>^[ \t]*\#(?:\\\n|[^\n])*)
  | (?P<comment>//(?:\\\n|[^\n])*|/\*.*?\*/)
  | (?P<raw>(?:u8|[uUL])?R"(?P<delimiter>[^()\\\s]{0,16})\()
  | (?P<literal>(?:u8|[uUL])?(?:"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])+'))
  | (?P<ident>[A-Za-z_]\w*)
  | (?P<number>\.?\d(?:[eEpP][+-]|'\w|[\w.])*)
  | (?P<space>\n|[ \t\r\f\v]+)
  | (?P<unterminated>/\*|["'])
  | (?P<punct>::|.)
    """,
    re.MULTILINE | re.DOTALL | re.VERBOSE,
)
CONDITIONAL = re.compile(r"[ \t]*#[ \t]*(if|ifdef|ifndef|elif|elifdef|elifndef|else|endif)\b")
# Words that take a parenthesised operand but never name a function.
NOT_A_NAME = {"if", "for", "while", "switch", "catch", "return", "sizeof", "alignof", "decltype", "static_assert"}
# Tokens a line may hold before its signature: specifiers and return types.
LINE_PREFIX = re.compile(r"[\w:<>,*&\s]*")


class ExtractError(Exception):
    pass


def line_of(source, offset):
    return source.count("\n", 0, offset) + 1


def tokenize(source):
    """Returns (kind, start, end) for every token, failing on unterminated text."""
    tokens = []
    pos = 0
    while pos < len(source):
        match = TOKEN.match(source, pos)
        kind = match.lastgroup
        end = match.end()
        if kind == "unterminated":
            raise ExtractError(f"unterminated comment or literal at line {line_of(source, pos)}")
        if kind == "raw":
            close = source.find(")" + match.group("delimiter") + '"', end)
            if close < 0:
                raise ExtractError(f"unterminated raw string at line {line_of(source, pos)}")
            kind, end = "literal", close + len(match.group("delimiter")) + 2
        tokens.append((kind, pos, end))
        pos = end
    return tokens


class Source:
    def __init__(self, text):
        self.text = text
        self.tokens = tokenize(text)
        self.starts = [start for _, start, _ in self.tokens]

    def token_text(self, index):
        _, start, end = self.tokens[index]
        return self.text[start:end]

    def code_after(self, index):
        """Indexes of code and directive tokens from index onwards."""
        for i in range(index, len(self.tokens)):
            if self.tokens[i][0] not in ("comment", "space"):
                yield i

    def definitions(self, signature):
        """Spans of the definitions that start with signature."""
        found = []
        pos = self.text.find(signature)
        while pos >= 0:
            first = bisect.bisect_left(self.starts, pos)
            last = bisect.bisect_left(self.starts, pos + len(signature))
            aligned = first < len(self.tokens) and self.starts[first] == pos
            aligned = aligned and self.tokens[last - 1][2] == pos + len(signature)
            if aligned and all(self.tokens[i][0] in ("ident", "number", "punct", "space") for i in range(first, last)):
                span = self.definition_at(first)
                if span:
                    found.append(span)
            pos = self.text.find(signature, pos + 1)
        return found

    def definition_at(self, first):
        """Span of the function defined from token first, or None if it is not a definition."""
        paren = 0
        seen_parameters = False
        initializers = False
        previous = None
        tokens = self.code_after(first)
        for i in tokens:
            kind = self.tokens[i][0]
            if kind == "directive":
                continue
            text = self.token_text(i)
            if text in ("(", "["):
                if text == "(" and paren == 0 and not seen_parameters and not self.names_function(previous):
                    return None
                paren += 1
            elif text in (")", "]"):
                paren -= 1
                if paren < 0:
                    return None
                seen_parameters = seen_parameters or (paren == 0 and text == ")")
            elif paren > 0:
                pass
            elif text in (";", "=", "}") or (text == "," and not initializers):
                return None
            elif text == ":" and seen_parameters:
                initializers = True
            elif text == "{":
                if not seen_parameters:
                    return None
                if initializers and previous is not None and self.names_member(previous):
                    self.skip_braces(tokens)
                    previous = i
                    continue
                return self.span(first, self.body_end(i, tokens))
            previous = i
        return None

    def names_function(self, index):
        """True if the token before a parameter list can be a function name."""
        return index is not None and self.tokens[index][0] == "ident" and self.token_text(index) not in NOT_A_NAME

    def names_member(self, index):
        """True if a brace after this initializer-list token is a member's brace initializer."""
        return self.tokens[index][0] == "ident" or self.token_text(index) == ">"

    def skip_braces(self, tokens):
        depth = 1
        for i in tokens:
            text = self.token_text(i)
            depth += (text == "{") - (text == "}")
            if depth == 0:
                return
        raise ExtractError("unbalanced braces in an initializer")

    def body_end(self, opening, tokens):
        depth = 1
        branches = []
        for i in tokens:
            kind = self.tokens[i][0]
            text = self.token_text(i)
            if kind == "directive":
                self.check_branch(text, depth, branches, i)
                continue
            depth += (text == "{") - (text == "}")
            if depth == 0:
                return self.tokens[i][2]
        raise ExtractError(f"unbalanced braces in the body opened at line {line_of(self.text, self.starts[opening])}")

    def check_branch(self, directive, depth, branches, index):
        match = CONDITIONAL.match(directive)
        if not match:
            return
        word = match.group(1)
        if word.startswith("if"):
            branches.append(depth)
        elif branches and branches[-1] != depth:
            raise ExtractError(
                f"braces differ between preprocessor branches at line {line_of(self.text, self.starts[index])}"
            )
        elif word == "endif" and branches:
            branches.pop()

    def span(self, first, end):
        start = self.text.rfind("\n", 0, self.starts[first]) + 1
        if not LINE_PREFIX.fullmatch(self.text, start, self.starts[first]):
            raise ExtractError(f"unexpected code before the signature at line {line_of(self.text, start)}")
        before = bisect.bisect_left(self.starts, start) - 1
        while before >= 0 and self.tokens[before][0] in ("comment", "space"):
            before -= 1
        after_declaration = before < 0 or self.tokens[before][0] == "directive"
        if not after_declaration and self.token_text(before) not in (";", "{", "}", ":"):
            raise ExtractError(
                f"the definition at line {line_of(self.text, start)} starts before the signature line; "
                "include the preceding lines in the signature"
            )
        return start, end


def extract(source, signatures):
    """Returns each signature's definition, in the order given, separated by a blank line."""
    parsed = Source(source)
    chunks = []
    errors = []
    seen = {}
    for signature in signatures:
        try:
            spans = parsed.definitions(signature)
        except ExtractError as error:
            errors.append(f"{signature}: {error}")
            continue
        if len(spans) != 1:
            errors.append(f"{signature}: {len(spans) or 'no'} definitions found; expected exactly one")
        elif spans[0] in seen:
            errors.append(f"{signature}: same definition as {seen[spans[0]]}")
        else:
            seen[spans[0]] = signature
            chunks.append(source[spans[0][0] : spans[0][1]] + "\n")
    if errors:
        raise ExtractError("\n".join(errors))
    return "\n".join(chunks)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    path = argv[1]
    try:
        with open(path, encoding="utf-8", newline="") as handle:
            text = extract(handle.read(), argv[2:])
    except ExtractError as error:
        sys.exit(f"{path}:\n{error}")
    sys.stdout.buffer.write(text.encode("utf-8"))


if __name__ == "__main__":
    main(sys.argv)
