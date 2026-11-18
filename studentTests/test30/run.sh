#!/usr/bin/env bash
# Runs only this test; extra arguments are passed to runAllTests.py
# (e.g. --renderer "/path/to/renderer {scene}").
cd "$(dirname "$0")/.." && exec python3 runAllTests.py --only test30 "$@"
