"""Extract selected production methods, failing closed when a signature moves."""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'cmake'))
from extract_functions import extract  # noqa: E402

source = pathlib.Path(sys.argv[1]).read_text()
out = pathlib.Path(sys.argv[2])
methods = '''onEnter onExit swallowHeldReleases selectedEntry showingRecents openSelectedBook activateIndex
onRowLongPress promptRemoveRecentBook openSearch stepTab onTabAction selectTab toggleSortDirection tabCount activeTab
headerTitle totalBookRowCount bookRowCount browsesGroups listCount rowFor groupable titleInitialFor buildGroupStarts groupForBook collapseGroups expandGroup
restoreExpandedList applyFilter refilterAfterBookChange filterBooks rowTextFor authorFor handleBackAction backActionTrampoline handleCustomInput handleButtons navigateButtons buildRows formatInitialHeading authorHeadingFor formatAuthorHeading
seriesFor resolveBook promptDeleteBook openBookDetails openBookOptions openOptions openGrouping openShelfFilter refreshLibrary captureRefreshSelection restoreRefreshSelection releaseIndexForChild restoreIndexAfterChild'''.split()

chunks = [extract(source, ['orderForTab(', 'LibraryListActivity::rebuildActionTrampoline(']
                   + ['LibraryListActivity::' + method + '(' for method in methods])]
repo = pathlib.Path(sys.argv[1]).parents[3]
nav_source = (repo / 'src/activities/UiTabListActivity.cpp').read_text()
chunks.append(extract(nav_source, ['UiTabListActivity::moveRingTo(']).replace('UiTabListActivity::', 'LibraryListActivity::'))
button_source = (repo / 'src/util/ButtonNavigator.cpp').read_text()
chunks.append(extract(button_source, ['ButtonNavigator::' + method + '(' for method in
                                     ['nextIndex', 'previousIndex', 'nextPageIndex', 'previousPageIndex']]))
home_source = (repo / 'src/activities/home/HomeActivity.cpp').read_text()
chunks.append(extract(home_source, ['HomeActivity::fillCoverGridFromLibrary(']))
# Share the real return contract with the UI; storage remains simulated.
index_source = (repo / 'lib/LibraryIndex/LibraryIndexFile.cpp').read_text()
author_reader = extract(index_source, ['LibraryIndexFile::readAuthor('])
chunks.append('namespace library {\n' + author_reader.replace('LibraryIndexFile::', 'Index::') + '\n}')
text = '\n\n'.join(chunks)
keys = sorted(set(re.findall(r'\bSTR_[A-Z0-9_]+', text)))
(out / 'LibraryUiStrings.h').write_text('#pragma once\nenum class StrId {' + ','.join(keys) + '};\n' + '\n'.join('inline constexpr StrId ' + key + ' = StrId::' + key + ';' for key in keys) + '\ninline const char* tr(StrId) { return "label"; }\n')
(out / 'LibraryUiProduction.cpp').write_text('#include "LibraryUiFixture.h"\n\n' + text + '\n')
