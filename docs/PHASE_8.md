# Phase 8 — Audit

> **Historical note (re-verification):** the numbers in this file are development-time measurements made with the old test scripts (cached native reference in run1.sh, native pinned to CPU 0, no empty-file/md5/TC2 checks). They were not re-verified. Verified numbers: docs/VERIFICATION.md, docs/SUMMARY.md, docs/AUDIT.md.

Full matrix in docs/AUDIT.md (script phase8.sh, raw log bench_logs/phase8.txt). Everything correct; bzip2
10-round median +13.5%, worst +5.1%. Ablation and cc1 tables included. No code changes in this phase.
