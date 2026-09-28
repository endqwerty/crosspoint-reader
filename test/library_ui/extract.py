"""Extract selected production methods, failing closed when a signature moves."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text()
out = pathlib.Path(sys.argv[2])
methods = '''onEnter onExit swallowHeldReleases selectedEntry showingRecents openSelectedBook activateIndex
onRowLongPress promptRemoveRecentBook openSearch stepTab onTabAction selectTab toggleSortDirection tabCount activeTab
headerTitle totalBookRowCount bookRowCount browsesGroups listCount rowFor groupable titleInitialFor buildGroupStarts groupForBook collapseGroups expandGroup
restoreExpandedList applyFilter refilterAfterBookChange filterBooks rowTextFor authorFor handleBackAction backActionTrampoline handleCustomInput handleButtons navigateButtons buildRows formatInitialHeading authorHeadingFor formatAuthorHeading'''.split()
optional = '''seriesFor resolveBook promptDeleteBook openBookDetails openBookOptions openOptions openGrouping openShelfFilter refreshLibrary captureRefreshSelection restoreRefreshSelection releaseIndexForChild restoreIndexAfterChild'''.split()

def extract(marker, source=source):
    start = source.index(marker)
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth = 0
    # Lex string/comment tokens before counting braces in code, including lambdas.
    tokens = re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[brace:])
    for token in tokens:
        if token.group() == '{': depth += 1
        elif token.group() == '}':
            depth -= 1
            if depth == 0: return source[start:brace + token.end()]
    raise RuntimeError('Unbalanced method: ' + marker)

chunks = [extract('orderForTab('), extract('LibraryListActivity::rebuildActionTrampoline(')]
for method in methods:
    chunks.append(extract('LibraryListActivity::' + method + '('))
for method in optional:
    marker = 'LibraryListActivity::' + method + '('
    if marker in source: chunks.append(extract(marker))
    else: raise RuntimeError('Required Library method not implemented: ' + method)
repo = pathlib.Path(sys.argv[1]).parents[3]
nav_source = (repo / 'src/activities/UiTabListActivity.cpp').read_text()
chunks.append(extract('UiTabListActivity::moveRingTo(', nav_source).replace('UiTabListActivity::', 'LibraryListActivity::'))
button_source = (repo / 'src/util/ButtonNavigator.cpp').read_text()
for method in ['nextIndex', 'previousIndex', 'nextPageIndex', 'previousPageIndex']:
    chunks.append(extract('ButtonNavigator::' + method + '(', button_source))
# Share the real return contract with the UI; storage remains simulated.
index_source = (repo / 'lib/LibraryIndex/LibraryIndexFile.cpp').read_text()
author_reader = extract('LibraryIndexFile::readAuthor(', index_source)
chunks.append('namespace library {\n' + author_reader.replace('LibraryIndexFile::', 'Index::') + '\n}')
text = '\n\n'.join(chunks)
keys = sorted(set(re.findall(r'\bSTR_[A-Z0-9_]+', text)))
(out / 'LibraryUiStrings.h').write_text('#pragma once\nenum class StrId {' + ','.join(keys) + '};\n' + '\n'.join('inline constexpr StrId ' + key + ' = StrId::' + key + ';' for key in keys) + '\ninline const char* tr(StrId) { return "label"; }\n')
(out / 'LibraryUiProduction.cpp').write_text('#include "LibraryUiFixture.h"\n\n' + text + '\n')
