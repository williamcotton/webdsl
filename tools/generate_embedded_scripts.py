#!/usr/bin/env python3

import os
import sys
from pathlib import Path

CHUNK_SIZE = 2000  # Safe size for string literals

HEADER_TEMPLATE = '''/* Generated file - DO NOT EDIT */
#ifndef GENERATED_SCRIPTS_H
#define GENERATED_SCRIPTS_H

#include <stddef.h>

typedef struct {
    const char* name;
    const char** chunks;
    size_t num_chunks;
} EmbeddedScript;

/* Table of all embedded scripts */
extern const EmbeddedScript EMBEDDED_SCRIPTS[];

#endif /* GENERATED_SCRIPTS_H */
'''

def escape_string(s):
    """Escape a string for C string literal."""
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')

def chunk_string(s, chunk_size=CHUNK_SIZE):
    """Split a string into chunks of maximum size."""
    return [s[i:i + chunk_size] for i in range(0, len(s), chunk_size)]

def generate_scripts():
    # Get project root directory (two levels up from this script)
    project_root = Path(__file__).parent.parent
    scripts_dir = project_root / 'scripts'
    output_dir = project_root / 'src' / 'server'
    
    # Ensure output directory exists
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Find all Lua scripts (excluding those in build/ directory)
    script_files = []
    for script_path in scripts_dir.glob('*.lua'):
        if 'build' not in script_path.parts:
            script_files.append(script_path)
    
    # Generate header file
    with open(output_dir / 'generated_scripts.h', 'w') as f:
        f.write(HEADER_TEMPLATE)
    
    # Generate source file
    with open(output_dir / 'generated_scripts.c', 'w') as f:
        f.write('/* Generated file - DO NOT EDIT */\n')
        f.write('#include "generated_scripts.h"\n')
        f.write('#include <stddef.h>\n\n')
        
        # Generate chunk arrays for each script
        for script_path in script_files:
            with open(script_path, 'r') as sf:
                content = sf.read()
                chunks = chunk_string(content)
                var_prefix = script_path.stem.upper()
                
                f.write(f'static const char* {var_prefix}_CHUNKS[] = {{\n')
                for chunk in chunks:
                    f.write(f'    "{escape_string(chunk)}",\n')
                f.write('};\n\n')
        
        # Generate scripts array
        f.write('const EmbeddedScript EMBEDDED_SCRIPTS[] = {\n')
        for script_path in script_files:
            var_prefix = script_path.stem.upper()
            with open(script_path, 'r') as sf:
                num_chunks = len(chunk_string(sf.read()))
            f.write('    {\n')
            f.write(f'        .name = "{script_path.stem}",\n')
            f.write(f'        .chunks = {var_prefix}_CHUNKS,\n')
            f.write(f'        .num_chunks = {num_chunks}\n')
            f.write('    },\n')
        f.write('    { NULL, NULL, 0 }  /* Terminator */\n')
        f.write('};\n')

if __name__ == '__main__':
    generate_scripts() 