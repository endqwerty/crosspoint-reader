"""Tests for the shared production function extractor.

Run: python3 test/cmake/test_extract_functions.py [path/to/cmake]
"""

import pathlib
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from extract_functions import ExtractError, extract  # noqa: E402

CMAKE = sys.argv.pop(1) if len(sys.argv) > 1 else shutil.which("cmake")


def src(text):
    return textwrap.dedent(text).lstrip("\n")


class ExtractTest(unittest.TestCase):
    def assertFails(self, source, signatures, *fragments):
        with self.assertRaises(ExtractError) as caught:
            extract(source, signatures)
        for fragment in fragments:
            self.assertIn(fragment, str(caught.exception))

    def test_returns_one_whole_definition(self):
        source = src("""
            #include "A.h"

            void A::before() { first(); }

            void A::target(int x) {
              if (x) {
                go();
              }
            }

            void A::after() {}
            """)
        self.assertEqual(
            extract(source, ["void A::target("]),
            "void A::target(int x) {\n  if (x) {\n    go();\n  }\n}\n",
        )

    def test_joins_definitions_in_the_order_given(self):
        source = "void A::one() { a(); }\nvoid A::two() { b(); }\n"
        self.assertEqual(
            extract(source, ["void A::two()", "void A::one()"]),
            "void A::two() { b(); }\n\nvoid A::one() { a(); }\n",
        )

    def test_missing_signature_fails(self):
        self.assertFails("void A::other() {}\n", ["void A::target("], "void A::target(", "no definition")

    def test_reports_every_failing_signature(self):
        self.assertFails("void A::other() {}\n", ["void A::one(", "void A::two("], "void A::one(", "void A::two(")

    def test_duplicate_definition_fails(self):
        source = "bool A::load(int a) { return a; }\nbool A::load(char b) { return b; }\n"
        self.assertFails(source, ["bool A::load("], "2 definitions")
        self.assertEqual(extract(source, ["bool A::load(char"]), "bool A::load(char b) { return b; }\n")

    def test_same_definition_twice_fails(self):
        self.assertFails("void A::f() {}\n", ["void A::f(", "void A::f()"], "same definition")

    def test_unbalanced_body_fails(self):
        self.assertFails("void A::f() {\n  if (x) {\n}\n", ["void A::f("], "unbalanced")

    def test_unterminated_literal_or_comment_fails(self):
        self.assertFails('void A::f() { s = "open; }\n', ["void A::f("], "line 1")
        self.assertFails("void A::f() { /* open }\n", ["void A::f("], "line 1")

    def test_declarations_calls_and_comments_are_not_definitions(self):
        source = src("""
            // void A::f() { stale(); }
            const char* doc = "void A::f() {";
            void A::f();
            void A::g() {
              A::f();
              if (A::f()) {
                use();
              }
              auto p = &A::f;
            }
            void A::f() { real(); }
            """)
        self.assertEqual(extract(source, ["A::f("]), "void A::f() { real(); }\n")

    def test_defaulted_and_deleted_are_not_definitions(self):
        source = "A::A() = default;\nA::A(const A&) = delete;\nA::A(int) { real(); }\n"
        self.assertEqual(extract(source, ["A::A("]), "A::A(int) { real(); }\n")

    def test_braces_inside_literals_and_comments_are_ignored(self):
        body = src(r"""
            void A::f() {
              const char* s = "}{\"}";
              char c = '}';
              char d = '\'';
              const char* r = R"x(} ")" {)x";
              const char* u = u8R"(})";
              int n = 1'000'000;
              char e = '{';
              int h = 0xFF'FF;
              // }
              /* { */
            }
            """)
        self.assertEqual(extract(body + "void A::g() {}\n", ["void A::f("]), body)

    def test_nested_lambdas_stay_inside_the_body(self):
        body = src("""
            void A::f() {
              run([this](int x) {
                return [x]() { return x; }();
              });
              auto g = [] {};
            }
            """)
        self.assertEqual(extract(body + "void A::g() {}\n", ["void A::f("]), body)

    def test_constructor_initializer_list(self):
        body = src("""
            A::A(int x, std::string name)
                : Base("A", x), count_(x), name_{std::move(name)}, items_{}, map_{{1, 2}} {
              init();
            }
            """)
        self.assertEqual(extract(body + "void A::g() {}\n", ["A::A("]), body)

    def test_qualifiers_and_trailing_return(self):
        source = "auto A::f() const noexcept(true) -> std::optional<int> { return {}; }\n"
        self.assertEqual(extract(source, ["auto A::f()"]), source)

    def test_template_header_must_be_in_the_signature(self):
        source = "template <typename T>\nT A::get() const {\n  return {};\n}\n"
        self.assertFails(source, ["T A::get() const"], "before the signature line")
        self.assertEqual(extract(source, ["template <typename T>\nT A::get() const"]), source)

    def test_return_type_on_the_line_is_included(self):
        source = "static int helper(int x) { return x; }\nint use() { return helper(1); }\n"
        self.assertEqual(extract(source, ["helper("]), "static int helper(int x) { return x; }\n")

    def test_signature_must_match_whole_tokens(self):
        source = "void MyA::f() {}\nvoid A::fx() {}\n"
        self.assertFails(source, ["A::f("], "no definition")

    def test_balanced_preprocessor_branches_are_supported(self):
        body = src("""
            void A::f() {
            #if BOARD
              one();
            #else
              two();
            #endif
            }
            """)
        self.assertEqual(extract(body, ["void A::f("]), body)

    def test_unbalanced_preprocessor_branches_fail(self):
        source = src("""
            void A::f() {
            #if BOARD
              if (a) {
            #else
              if (b) {
            #endif
              }
            }
            """)
        self.assertFails(source, ["void A::f("], "preprocessor")

    def test_statements_and_lambdas_are_not_functions(self):
        source = src("""
            void A::f() {
              if (ready) {
                go();
              } else if (late) {
                wait();
              }
              while (busy) {
                spin();
              }
              auto handler = [this](int result) {
                use(result);
              };
            }
            """)
        for statement in ["if (ready) {", "else if (late) {", "while (busy) {", "auto handler = [this](int result) {"]:
            self.assertFails(source, [statement], "no definition")

    def test_class_is_not_a_function(self):
        self.assertFails("class Keeper {\n};\n", ["class Keeper"], "no definition")


TARGET_FILE = ROOT / "src/activities/reader/EpubReaderActivity.cpp"
TARGET = "void EpubReaderActivity::addBookmark()"


class MutationTest(unittest.TestCase):
    """Edits a temporary copy of a production file the way upstream changes do."""

    def setUp(self):
        self.original = TARGET_FILE.read_text()
        self.body = extract(self.original, [TARGET])
        self.end = self.original.index(self.body) + len(self.body)
        self.tmp = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)

    def mutated(self):
        """Changes the target body, inserts a function after it and renames the next one."""
        target_start = self.original.index(self.body)
        changed_body = self.body[: self.body.rindex("}")] + "  mutationProbe();\n}\n"
        rest = self.original[self.end :]
        next_def = rest.index("EpubReaderActivity::")
        name_end = rest.index("(", next_def)
        renamed = rest[:name_end] + "Renamed" + rest[name_end:]
        inserted = "\nvoid EpubReaderActivity::insertedProbe() {\n  int x = 1;\n}\n"
        source = self.original[:target_start] + changed_body + inserted + renamed
        path = self.tmp / "EpubReaderActivity.cpp"
        path.write_text(source)
        return path, changed_body

    def test_cli_returns_changed_body_after_neighbour_edits(self):
        path, changed_body = self.mutated()
        result = subprocess.run(
            [sys.executable, str(HERE / "extract_functions.py"), str(path), TARGET],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, changed_body)
        self.assertNotIn("insertedProbe", result.stdout)

    def test_cli_reports_a_renamed_target(self):
        path = self.tmp / "EpubReaderActivity.cpp"
        path.write_text(self.original.replace(TARGET, TARGET.replace("addBookmark", "addMark")))
        result = subprocess.run(
            [sys.executable, str(HERE / "extract_functions.py"), str(path), TARGET],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(TARGET, result.stderr)

    @unittest.skipUnless(CMAKE, "pass the cmake executable to run the CMake wrapper check")
    def test_cmake_wrapper_extracts_and_tracks_dependencies(self):
        path, changed_body = self.mutated()
        project = self.tmp / "project"
        project.mkdir()
        (project / "CMakeLists.txt").write_text(
            src(f"""
                cmake_minimum_required(VERSION 3.16)
                project(extract_probe NONE)
                include("{HERE / 'ProductionExtract.cmake'}")
                crosspoint_extract_functions(body "${{SOURCE}}" "{TARGET}")
                file(WRITE "${{CMAKE_BINARY_DIR}}/body.cpp" "${{body}}")
                get_property(deps DIRECTORY PROPERTY CMAKE_CONFIGURE_DEPENDS)
                file(WRITE "${{CMAKE_BINARY_DIR}}/deps.txt" "${{deps}}")
                if(MISSING)
                  crosspoint_extract_functions(gone "${{SOURCE}}" "void EpubReaderActivity::noSuchMethod()")
                endif()
                """)
        )
        build = self.tmp / "build"
        ok = subprocess.run(
            [CMAKE, "-S", str(project), "-B", str(build), f"-DSOURCE={path}"], capture_output=True, text=True
        )
        self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)
        self.assertEqual((build / "body.cpp").read_text(), changed_body)
        deps = (build / "deps.txt").read_text().split(";")
        self.assertIn(str(path), deps)
        self.assertIn(str(HERE / "extract_functions.py"), deps)

        failed = subprocess.run(
            [CMAKE, "-S", str(project), "-B", str(build), f"-DSOURCE={path}", "-DMISSING=ON"],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("void EpubReaderActivity::noSuchMethod()", failed.stderr)


if __name__ == "__main__":
    unittest.main()
