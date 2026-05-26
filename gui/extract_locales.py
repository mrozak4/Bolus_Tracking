#!/usr/bin/env python3
"""
Extract locale JSON files from bolus_locale.cpp for the Electron GUI.
Parses each language block and writes individual {code}.json files.
Skips LANG_EGY (Ancient Egyptian) per user request.
"""
import re, json, os, sys

LANG_MAP = {
    'LANG_EN': 'en', 'LANG_FR': 'fr', 'LANG_DE_CH': 'de_ch', 'LANG_ES': 'es',
    'LANG_IT': 'it', 'LANG_NL': 'nl', 'LANG_DA': 'da', 'LANG_NO': 'no',
    'LANG_SV': 'sv', 'LANG_FI': 'fi', 'LANG_EL': 'el', 'LANG_RU': 'ru',
    'LANG_UK': 'uk', 'LANG_BG': 'bg', 'LANG_SR': 'sr', 'LANG_TR': 'tr',
    'LANG_HI': 'hi', 'LANG_BN': 'bn', 'LANG_TA': 'ta', 'LANG_TH': 'th',
    'LANG_JA': 'ja', 'LANG_KO': 'ko', 'LANG_ZH_CN': 'zh_cn', 'LANG_VI': 'vi',
    'LANG_ID': 'id', 'LANG_TL': 'tl', 'LANG_AF': 'af', 'LANG_GA': 'ga',
    'LANG_GL': 'gl', 'LANG_IU': 'iu', 'LANG_CA': 'ca', 'LANG_HT': 'ht',
    'LANG_LA': 'la', 'LANG_SCOTS': 'scots', 'LANG_EO': 'eo',
    'LANG_GRC': 'grc', 'LANG_GENALPHA': 'genalpha', 'LANG_GENZ': 'genz',
    'LANG_KL': 'kl', 'LANG_LEET': 'leet', 'LANG_MINION': 'minion',
    'LANG_PIRATE': 'pirate', 'LANG_SHAKESPEARE': 'shakespeare', 'LANG_YODA': 'yoda',
}
SKIP = {'LANG_EGY'}

def extract_locales(cpp_path, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    with open(cpp_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Pattern to match language block starts
    block_pat = re.compile(r'(?:if|else if)\s*\(\s*m_lang\s*==\s*(LANG_\w+)\s*\)')
    assign_pat = re.compile(r'm_tr\.(\w+)\s*=\s*"((?:[^"\\]|\\.)*)"\s*;')
    # Also match function-wrapped strings: m_tr.key = func_name("value");
    func_assign_pat = re.compile(r'm_tr\.(\w+)\s*=\s*\w+\(\s*"((?:[^"\\]|\\.)*)"\s*\)\s*;')

    # Find all block boundaries
    blocks = []
    for m in block_pat.finditer(content):
        blocks.append((m.start(), m.group(1)))

    count = 0
    for i, (start, lang_const) in enumerate(blocks):
        if lang_const in SKIP:
            continue
        code = LANG_MAP.get(lang_const)
        if not code:
            print(f"  WARN: No mapping for {lang_const}, skipping")
            continue

        # Find the end of this block (next block start or end of function)
        end = blocks[i+1][0] if i+1 < len(blocks) else len(content)
        block_text = content[start:end]

        translations = {}
        for am in assign_pat.finditer(block_text):
            key = am.group(1)
            val = am.group(2)
            val = val.replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\').replace('\\t', '\t')
            translations[key] = val
        # Also match function-wrapped strings (e.g., to_klingon_piqad("..."))
        for am in func_assign_pat.finditer(block_text):
            key = am.group(1)
            if key not in translations:  # don't overwrite direct assignments
                val = am.group(2)
                val = val.replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\').replace('\\t', '\t')
                translations[key] = val

        if translations:
            out_path = os.path.join(out_dir, f'{code}.json')
            with open(out_path, 'w', encoding='utf-8') as f:
                json.dump(translations, f, ensure_ascii=False, indent=2)
            count += 1
            print(f"  ✓ {code}.json: {len(translations)} keys")
        else:
            print(f"  WARN: {lang_const} ({code}) — no translations found")

    print(f"\nDone: {count} locale files created in {out_dir}")

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    cpp_path = os.path.join(script_dir, '..', 'cpp', 'src', 'bolus_locale.cpp')
    out_dir = os.path.join(script_dir, 'locales')
    extract_locales(cpp_path, out_dir)
