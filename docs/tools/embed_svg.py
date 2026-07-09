"""Read pipeline SVG and generate C-escaped string for embedding in web_server.c"""
import re

# Read the enhanced SVG
with open('d:/10_Work/03_Team_Competition/risc-v/src/src/debugger/datapath_pipeline_dynamic.svg', 'r', encoding='utf-8') as f:
    svg = f.read()

# Escape for C string literal:
# - Replace \ with \\
# - Replace " with \"
# - Replace newlines with \r\n" + newline + " (multi-line C string)
svg_escaped = svg.replace('\\', '\\\\').replace('"', '\\"')

# Split into lines and wrap as C string lines
lines = svg_escaped.split('\n')
c_lines = []
for line in lines:
    if line.strip():
        c_lines.append(f'        "{line}\\r\\n"')
    else:
        c_lines.append(f'        "\\r\\n"')

# Write the C string to a file for reference
c_code = '\n'.join(c_lines)
out = f'// Auto-generated pipeline SVG (do not edit manually)\n// Generated from src/src/debugger/datapath_pipeline_dynamic.svg\nstatic const char *pipeline_svg =\n{c_code};\n'

with open('d:/10_Work/03_Team_Competition/risc-v/docs/tools/pipeline_svg_c.txt', 'w', encoding='utf-8') as f:
    f.write(out)

print(f'C string: {len(out)} chars, {len(c_lines)} lines')
print('Written to docs/tools/pipeline_svg_c.txt')
