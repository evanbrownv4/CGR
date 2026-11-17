#!/usr/bin/env bash
# Runs every test in sequence and writes ../output/tests/test_results.log.
# All arguments are passed to runAllTests.py (see README.md), e.g.
#   ./runAllTests.sh --renderer "/path/to/renderer {scene}" --category error
cd "$(dirname "$0")" && exec python3 runAllTests.py "$@"
