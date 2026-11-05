#!/usr/bin/env python3
"""проверка компиляции отдельных .c в объект, без сборки образа и линковки"""

import sys, os
import json
import shlex
import subprocess

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 objcheck.py <file1.c> [file2.c ...]", file=sys.stderr)
        sys.exit(1)

    compile_db_path = "/project/build-e1/compile_commands.json"
    try:
        with open(compile_db_path, 'r', encoding='utf-8') as f:
            compile_db = json.load(f)
    except Exception as e:
        print(f"Error loading compile_commands.json: {e}", file=sys.stderr)
        sys.exit(1)

    tpl = None
    for entry in compile_db:
        if entry.get('file', '').endswith('main/af_store.c'):
            tpl = entry
            break

    if tpl is None:
        print("Template entry for main/af_store.c not found in compile_commands.json", file=sys.stderr)
        sys.exit(1)

    cmd_list = shlex.split(tpl['command'])
    cwd = tpl['directory']

    # Filter out specific arguments
    filtered_cmd = []
    i = 0
    while i < len(cmd_list):
        arg = cmd_list[i]
        
        # Drop the template source argument (ends with af_store.c)
        if arg.endswith('af_store.c'):
            i += 1
            continue
            
        # Drop -c
        if arg == '-c':
            i += 1
            continue
            
        # Drop -o and its value
        if arg == '-o':
            i += 2
            continue
            
        # Drop dependency flags
        if arg in ['-MD', '-MMD']:
            i += 1
            continue
            
        if arg in ['-MF', '-MT']:
            i += 2
            continue
            
        filtered_cmd.append(arg)
        i += 1

    ored_rc = 0

    for name in sys.argv[1:]:
        # Construct new command
        new_cmd = filtered_cmd + os.environ.get('OBJX', '').split() + ['-c', f'/project/main/{name}', '-o', f'/tmp/objcheck_{name}.o']
        
        try:
            result = subprocess.run(
                new_cmd,
                cwd=cwd,
                capture_output=True,
                text=True
            )
            rc = result.returncode
            ored_rc |= rc
            print(f"== {name} rc={rc}")
            if result.stderr:
                print(result.stderr[-3000:])
        except Exception as e:
            print(f"Error running command for {name}: {e}", file=sys.stderr)
            ored_rc |= 1

    sys.exit(ored_rc)

if __name__ == '__main__':
    main()
