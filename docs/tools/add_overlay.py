"""Clean draw.io SVG + add dynamic overlays. KEEPS foreignObject labels."""
import re

with open('d:/10_Work/03_Team_Competition/risc-v/docs/流水线.drawio.svg', 'r', encoding='utf-8') as f:
    svg = f.read()

# === Strip only metadata, NOT rendered content ===

# Remove XML declaration, DOCTYPE, comments
svg = re.sub(r'^<\?xml[^>]*\?>\s*', '', svg)
svg = re.sub(r'^<!--[^>]*-->\s*', '', svg)
svg = re.sub(r'^<!DOCTYPE[^>]*>\s*', '', svg)

# Remove the massive content= attribute (mxGraphModel XML ~43KB)
svg = re.sub(r'\s*content="[^"]*"', '', svg, count=1)

# Remove draw.io generated ID
svg = re.sub(r'\s*id="ge-svg-[^"]*"', '', svg, count=1)

# Remove draw.io adaptive style block (generates dark/light variables)
svg = re.sub(r'<style[^>]*>.*?</style>', '', svg, flags=re.DOTALL)

# Fix background
svg = svg.replace(
    'style="background: transparent; background-color: transparent; color-scheme: light dark;"',
    'style="background: white;"')

# Remove xmlns:xlink (not needed in HTML5)
svg = re.sub(r'\s*xmlns:xlink="[^"]*"', '', svg)

# Responsive sizing
svg = re.sub(r'width="1113px"', 'width="100%"', svg)
svg = re.sub(r'height="589px"', 'height="100%"', svg)
svg = svg.replace('<svg ', '<svg preserveAspectRatio="xMidYMid meet" ')

# Fix draw.io adaptive stroke/fill (light-dark() not supported everywhere)
svg = re.sub(
    r'style="fill: var\(--ge-adaptive-bg, #ffffff\); stroke: light-dark\(rgb\(0, 0, 0\), rgb\(255, 255, 255\)\);"',
    'fill="#ffffff" stroke="#000000"', svg)
svg = re.sub(
    r'style="stroke: light-dark\(rgb\(0, 0, 0\), rgb\(255, 255, 255\)\);"',
    'stroke="#000000"', svg)
svg = re.sub(
    r'style="fill: light-dark\(rgb\(0, 0, 0\), rgb\(255, 255, 255\)\); stroke: light-dark\(rgb\(0, 0, 0\), rgb\(255, 255, 255\)\);"',
    'fill="#000000" stroke="#000000"', svg)

# Remove data-cell-id (debug metadata, not needed for rendering)
svg = re.sub(r'\s*data-cell-id="[^"]*"', '', svg)

# ============================================================
# Dynamic overlay — positioned on top of the pipeline diagram
# Font sizes: 10-12px with white background boxes for readability
# ============================================================
overlay = '''
  <g id="dv-overlay" font-family="monospace" font-weight="600">
    <!-- [1] PC value -->
    <rect x="4" y="215" width="112" height="22" fill="#fff" stroke="#1a73e8" stroke-width="2" rx="3"/>
    <text id="dv_pc" x="60" y="231" text-anchor="middle" font-size="11" fill="#1a73e8">PC=----</text>

    <!-- [2] Instruction code -->
    <rect x="85" y="403" width="132" height="22" fill="#fff" stroke="#1a73e8" stroke-width="2" rx="3"/>
    <text id="dv_instr" x="151" y="419" text-anchor="middle" font-size="11" fill="#1a73e8">INST=--------</text>

    <!-- [3] Disassembly -->
    <rect x="85" y="427" width="132" height="20" fill="#fff" stroke="#555" stroke-width="1.5" rx="3"/>
    <text id="dv_disasm" x="151" y="441" text-anchor="middle" font-size="10" fill="#555">----</text>

    <!-- [4] rs1 value -->
    <rect x="410" y="353" width="130" height="20" fill="#fff" stroke="#1a73e8" stroke-width="2" rx="3"/>
    <text id="dv_rs1" x="475" y="367" text-anchor="middle" font-size="11" fill="#1a73e8">RS1=----</text>

    <!-- [5] rs2 value -->
    <rect x="410" y="375" width="130" height="20" fill="#fff" stroke="#1a73e8" stroke-width="2" rx="3"/>
    <text id="dv_rs2" x="475" y="389" text-anchor="middle" font-size="11" fill="#1a73e8">RS2=----</text>

    <!-- [6] Immediate -->
    <rect x="375" y="473" width="92" height="20" fill="#fff" stroke="#e65100" stroke-width="2" rx="3"/>
    <text id="dv_imm" x="421" y="487" text-anchor="middle" font-size="11" fill="#e65100">IMM=----</text>

    <!-- [7] ALU input A -->
    <rect x="510" y="190" width="110" height="20" fill="#fff" stroke="#1a73e8" stroke-width="2" rx="3"/>
    <text id="dv_alu_a" x="565" y="204" text-anchor="middle" font-size="11" fill="#1a73e8">ALU_A=----</text>

    <!-- [8] ALU input B -->
    <rect x="510" y="230" width="110" height="20" fill="#fff" stroke="#1a73e8" stroke-width="2" rx="3"/>
    <text id="dv_alu_b" x="565" y="244" text-anchor="middle" font-size="11" fill="#1a73e8">ALU_B=----</text>

    <!-- [9] ALU operation -->
    <rect x="610" y="275" width="48" height="20" fill="#fff" stroke="#2e7d32" stroke-width="2" rx="3"/>
    <text id="dv_alu_op" x="634" y="289" text-anchor="middle" font-size="11" fill="#2e7d32">OP=?</text>

    <!-- [10] ALU result -->
    <rect x="640" y="254" width="100" height="20" fill="#fff" stroke="#e65100" stroke-width="2" rx="3"/>
    <text id="dv_alu_out" x="690" y="268" text-anchor="middle" font-size="11" fill="#e65100">ALU_OUT=----</text>

    <!-- [11] Memory address -->
    <rect x="850" y="300" width="120" height="20" fill="#fff" stroke="#e65100" stroke-width="2" rx="3"/>
    <text id="dv_mem_addr" x="910" y="314" text-anchor="middle" font-size="11" fill="#e65100">MEM_ADDR=----</text>

    <!-- [12] Memory read data -->
    <rect x="935" y="368" width="72" height="20" fill="#fff" stroke="#e65100" stroke-width="2" rx="3"/>
    <text id="dv_mem_rd" x="971" y="382" text-anchor="middle" font-size="11" fill="#e65100">MEM_RD=--</text>

    <!-- [13] Memory write data -->
    <rect x="855" y="453" width="72" height="20" fill="#fff" stroke="#e65100" stroke-width="2" rx="3"/>
    <text id="dv_mem_wd" x="891" y="467" text-anchor="middle" font-size="11" fill="#e65100">MEM_WD=--</text>

    <!-- [14] Write-back value -->
    <rect x="940" y="557" width="100" height="20" fill="#fff" stroke="#2e7d32" stroke-width="2" rx="3"/>
    <text id="dv_wb" x="990" y="571" text-anchor="middle" font-size="11" fill="#2e7d32">WB=----</text>

    <!-- [15] Next PC -->
    <rect x="150" y="150" width="100" height="20" fill="#fff" stroke="#888" stroke-width="2" rx="3"/>
    <text id="dv_next_pc" x="200" y="164" text-anchor="middle" font-size="11" fill="#888">NPC=----</text>

    <!-- [16] Forwarding active -->
    <rect x="660" y="145" width="85" height="18" fill="#fff" stroke="#c62828" stroke-width="1.5" rx="3"/>
    <text id="dv_fwd" x="702" y="158" text-anchor="middle" font-size="10" fill="#c62828">FWD=--</text>

    <!-- [17] Stall cycles -->
    <rect x="595" y="78" width="65" height="18" fill="#fff" stroke="#c62828" stroke-width="1.5" rx="3"/>
    <text id="dv_stall" x="627" y="91" text-anchor="middle" font-size="10" fill="#c62828">STALL=0</text>

    <!-- [18] Flush cycles -->
    <rect x="805" y="78" width="65" height="18" fill="#fff" stroke="#c62828" stroke-width="1.5" rx="3"/>
    <text id="dv_flush" x="837" y="91" text-anchor="middle" font-size="10" fill="#c62828">FLUSH=0</text>
  </g>
'''

svg = svg.replace('</svg>', overlay + '\n</svg>')

# === Save ===
outpath = 'd:/10_Work/03_Team_Competition/risc-v/src/src/debugger/datapath_pipeline_dynamic.svg'
with open(outpath, 'w', encoding='utf-8') as f:
    f.write(svg)

import os
size = os.path.getsize(outpath)
print(f'Saved: {size} bytes ({size/1024:.0f} KB)')
print(f'Dynamic labels: {svg.count("id=\"dv_\"")} ')
print(f'foreignObjects kept: {svg.count("<foreignObject")} ')
