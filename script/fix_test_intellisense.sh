#!/bin/bash
uv run python .vscode/generate_compile_commands.py
uv run python .vscode/patch_intellisense.py
pio test -e native
