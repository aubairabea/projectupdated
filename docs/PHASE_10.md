# Phase 10 — Submission package

> **Historical note (re-verification):** the numbers in this file are development-time measurements made with the old test scripts (cached native reference in run1.sh, native pinned to CPU 0, no empty-file/md5/TC2 checks). They were not re-verified. Verified numbers: docs/VERIFICATION.md, docs/SUMMARY.md, docs/AUDIT.md.

- Clean rebuild with the exact command: zero warnings.
- submission/ = project.so (copy of obj-intel64/project.so) + src/ (project.cpp, makefile, makefile.rules,
  README.txt). md5sum of the makefiles equals the originals (f2d63a76… makefile, 8bd75ba9… makefile.rules).
- project.zip created from inside submission/: project.so and src/ at the top level (6 entries, no benchmarks,
  inputs, obj-intel64, .git or docs).
- Package test: unzipped into /tmp/pkgtest; built from src/ (needs `mkdir -p obj-intel64` first — the course
  makefile does not create it for an explicit target; added to README (b)); both the shipped project.so and the
  rebuilt one run bzip2 byte-identical to native with no ERROR line.
