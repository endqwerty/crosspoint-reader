"""Compile the shipping browser methods with host HAL/UI boundaries."""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'cmake'))
from extract_functions import extract  # noqa: E402

source = pathlib.Path(sys.argv[1]).read_text()
out = pathlib.Path(sys.argv[2])
methods = '''loadFiles advanceSearch openSearch clearSearch provideRow prewarmRowGlyphs activateSelected
showEntryActions deleteSelected startRename renameSelectedFile handleCustomInput handleButtons findEntry'''.split()
text = extract(source, ['bool isBookFile(', 'void formatFileName(', 'void formatFileExtension(',
                        'std::string getFileExtension(const std::string& filename) {']
               + ['FileBrowserActivity::' + method + '(' for method in methods])
keys = sorted(set(re.findall(r'\bSTR_[A-Z0-9_]+', text)))
(out / 'FileBrowserStrings.h').write_text('#pragma once\nenum class StrId {' + ','.join(keys) + '};\n' + '\n'.join('inline constexpr StrId ' + key + ' = StrId::' + key + ';' for key in keys) + '\ninline const char* tr(StrId) { return "label"; }\n')
(out / 'FileBrowserProduction.cpp').write_text('#include "FileBrowserFixture.h"\n\n' + text + '\n')
