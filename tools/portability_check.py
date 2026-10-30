#!/usr/bin/env python3
"""Plan 33 portability lint for first-party C/C++ (no LLVM needed).

Rules (each names its plan 33 task):
  A1  no __int128 outside src/wide_math.hpp
  A2  std::sort / partial_sort / nth_element in src/ carry a `total-order:`
      comment on the line or within the two lines above
  A6  POSIX headers (<unistd.h>, <sys/...>) only under a _WIN32 guard
      (server/, which is Linux-only, is exempt); #pragma GCC only under
      __GNUC__
  A11 no platform-libm transcendental calls in src/ outside src/det_math.cpp
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
EXTENSIONS = {".cpp", ".hpp", ".h", ".c"}
LIBM = re.compile(r"\bstd::(exp2?|expm1|log2?|log10|log1p|pow|sinh?|cosh?|tanh?|asin|acos|atan2?|cbrt|hypot|erfc?)\s*\(|"
                  r"\b(expf|logf|log2f|log10f|powf|sinf|cosf|tanf|atan2f|exp2f)\s*\(")
SORT = re.compile(r"\bstd::(sort|partial_sort|nth_element)\s*\(")
POSIX = re.compile(r"#\s*include\s*<(unistd\.h|sys/[^>]+)>")
PRAGMA_GCC = re.compile(r"#\s*pragma\s+GCC\b")


def files(*dirs):
    for d in dirs:
        for path in sorted((ROOT / d).rglob("*")):
            if path.suffix in EXTENSIONS and path.is_file():
                yield path


def strip_comment(line):
    return line.split("//", 1)[0]


def guarded(lines, index, token):
    """True when an enclosing #if/#ifdef mentioning token is open at index."""
    depth_tokens = []
    for line in lines[:index]:
        s = line.strip()
        if s.startswith(("#if", "#ifdef", "#ifndef")):
            depth_tokens.append(token in s)
        elif s.startswith("#elif") or s.startswith("#else"):
            if depth_tokens:
                depth_tokens[-1] = depth_tokens[-1] or token in s
        elif s.startswith("#endif") and depth_tokens:
            depth_tokens.pop()
    return any(depth_tokens)


def main():
    problems = []
    for path in files("src", "tests", "oracle", "tools", "fuzz"):
        rel = path.relative_to(ROOT).as_posix()
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        in_src = rel.startswith("src/")
        for i, line in enumerate(lines):
            code = strip_comment(line)
            if "__int128" in code and rel != "src/wide_math.hpp" and in_src:
                problems.append(f"{rel}:{i + 1}: A1 __int128 outside src/wide_math.hpp")
            if in_src and SORT.search(code):
                window = lines[max(0, i - 2):i + 1]
                if not any("total-order:" in w for w in window):
                    problems.append(f"{rel}:{i + 1}: A2 sort without a `total-order:` justification")
            if in_src and rel != "src/det_math.cpp" and LIBM.search(code):
                problems.append(f"{rel}:{i + 1}: A11 platform libm call; use src/det_math.hpp")
            if POSIX.search(code) and not guarded(lines, i, "_WIN32"):
                problems.append(f"{rel}:{i + 1}: A6 POSIX header without a _WIN32 guard")
            if PRAGMA_GCC.search(code) and not guarded(lines, i, "__GNUC__"):
                problems.append(f"{rel}:{i + 1}: A6 #pragma GCC without an __GNUC__ guard")
    for p in problems:
        print(p)
    print(f"portability check: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
