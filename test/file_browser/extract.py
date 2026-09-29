"""Compile the shipping browser methods with host HAL/UI boundaries."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text()
out = pathlib.Path(sys.argv[2])
def extract(marker):
    start = source.rfind('\n', 0, source.index(marker)) + 1
    brace = source.index('{', start)
    depth = 0
    for token in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[brace:]):
        if token.group() == '{': depth += 1
        elif token.group() == '}':
            depth -= 1
            if depth == 0: return source[start:brace + token.end()]
    raise RuntimeError('Unbalanced production method: ' + marker)
chunks = [extract('bool isBookFile('), extract('void formatFileName('), extract('void formatFileExtension('),
          extract('std::string getFileExtension(const std::string& filename) {')]
methods = '''loadFiles advanceSearch openSearch clearSearch provideRow prewarmRowGlyphs activateSelected
showEntryActions deleteSelected startRename renameSelectedFile handleCustomInput handleButtons findEntry'''.split()
chunks += [extract('FileBrowserActivity::' + method + '(') for method in methods]
text = '\n\n'.join(chunks)
keys = sorted(set(re.findall(r'\bSTR_[A-Z0-9_]+', text)))
(out / 'FileBrowserStrings.h').write_text('#pragma once\nenum class StrId {' + ','.join(keys) + '};\n' + '\n'.join('inline constexpr StrId ' + key + ' = StrId::' + key + ';' for key in keys) + '\ninline const char* tr(StrId) { return "label"; }\n')
(out / 'FileBrowserProduction.cpp').write_text('#include "FileBrowserFixture.h"\n\n' + text + '\n')
