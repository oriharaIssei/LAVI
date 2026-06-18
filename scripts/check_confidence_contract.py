#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_confidence_contract.py
============================
Meaning Architecture v2 — confidence 契約 (C1–C4) の静的監視網。

契約の定義は docs/meaning-architecture/CONSTITUTION.md §2 が唯一の正。
矛盾したら CONSTITUTION を見よ。本スクリプトは CONSTITUTION の写しではなく、
そこに書かれた既知の危険パターンが混入していないことを監視する道具にすぎない。

位置づけ:
  - 完全性より「再現性」を優先する。緑＝絶対安全ではない。
  - 緑＝「既知の危険パターンが（許可リストを除き）混入していない」ことの監視。
  - C5(confidence 時間減衰禁止) / C6(RouteTarget 的 enum-argmax 禁止) は静的検出が
    難しいため対象外。人間レビュー観点として別管理（CLAUDE.md 想定）。

対象契約 (InterpNode の支持度 confidence / confidence_ に作用するもの):
  C1 normalize  : 総和や最大値で割る / normalize 系関数へ渡す
  C2 clamp      : std::clamp / (std::min) / (std::max) 等で [0,1] 等に制限
  C3 softmax    : confidence 群へ softmax 適用
  C4 sum-to-one : confidence の集合を Σ=1 にする (sum/total で除算 等)

RawNode の activation（減衰する注目度）への clamp/normalize は正当。
confidence を含まない行は対象外なので、activation だけの操作は誤検出しない。

偽陽性は「許可リスト方式」で個別に黙らせる:
  安全と判断した行の行末に  // CONF-OK: 理由  を付けると、その行は検出から除外される。
  判断を消すのではなく、判断の形跡を残させる設計。

使い方:
  python3 check_confidence_contract.py [SCAN_ROOT ...]
  引数なしの場合は LAVI のソース根 (project/application/code) を既定スキャン対象とする。

終了コード:
  0 = GREEN（違反なし）  / 1 = 違反あり  / 2 = 使い方エラー
CI / コミット前フックからは終了コードで成否を判定できる。
"""

import os
import re
import sys

# ── 走査対象の拡張子と除外ディレクトリ ──────────────────────────────
SOURCE_EXTS = (".cpp", ".cc", ".cxx", ".h", ".hpp", ".hxx", ".inl")
EXCLUDE_DIRS = {
    "externals", "external", "third_party", "thirdparty", "vendor",
    "generated", "build", "out", "bin", "obj", "x64", "Win32",
    "Debug", "Release", ".git", ".vs", "node_modules", "cmake-build",
}

# 許可リスト注釈（行末）。大文字小文字は無視。
ALLOW_RE = re.compile(r"//\s*CONF-OK\b", re.IGNORECASE)

# confidence / confidence_ 識別子（前後が英数字でない＝独立トークン）。
# m_confidence, node.confidence, confidence_ などにマッチし、activation は対象外。
CONF_RE = re.compile(r"(?<![A-Za-z0-9])confidence_?(?![A-Za-z0-9])", re.IGNORECASE)

# 各契約のトリガー（confidence を含む行に対してのみ評価する）。
# (contract_id, 説明, [compiled regexes])
CONTRACTS = [
    (
        "C1", "normalize",
        [
            re.compile(r"normaliz", re.IGNORECASE),                    # normalize / normalise / normalization
            re.compile(r"/=?\s*[\w.]*max\w*\b", re.IGNORECASE),         # 最大値で除算: / max, /= curMax, / maxConfidence
            re.compile(r"/=?\s*[\w.]*norm\w*\b", re.IGNORECASE),        # norm / l2norm 等で除算
        ],
    ),
    (
        "C2", "clamp",
        [
            re.compile(r"\bstd::clamp\b", re.IGNORECASE),
            re.compile(r"\bclamp\w*\s*\(", re.IGNORECASE),             # clamp( / clampf( / Clamp(
            re.compile(r"\(?\s*std::min\s*\)?\s*\(", re.IGNORECASE),   # std::min( / (std::min)(  ※Windows マクロ回避形
            re.compile(r"\(?\s*std::max\s*\)?\s*\(", re.IGNORECASE),   # std::max( / (std::max)(
            re.compile(r"\bf?min\w*\s*\(", re.IGNORECASE),             # min(/fmin(/fminf(
            re.compile(r"\bf?max\w*\s*\(", re.IGNORECASE),             # max(/fmax(/fmaxf(
        ],
    ),
    (
        "C3", "softmax",
        [
            re.compile(r"\bsoft_?max\b", re.IGNORECASE),
        ],
    ),
    (
        "C4", "sum-to-one",
        [
            re.compile(r"/=?\s*[\w.]*(sum|total)\w*\b", re.IGNORECASE), # 総和で除算: / sum, /= total, / sumConf
            re.compile(r"\bsum[_]?to[_]?one\b", re.IGNORECASE),
        ],
    ),
]

WHY = {
    "C1": "confidence を正規化している（総和/最大値で除算、または normalize 系処理）。"
          "confidence は無界の支持度であり、正規化すると確率へ退化し Belief Distribution へ逆戻りする。",
    "C2": "confidence を clamp/min/max で制限している。confidence に上限は無い"
          "（独立した観測の累積で 1 を超えてよい）。[0,1] への clamp は確率化。",
    "C3": "confidence 群へ softmax を適用している。softmax は Σ=1 の競合的確率分布を作り、"
          "非競合・非正規化の支持度という定義に反する。",
    "C4": "confidence の集合を総和で割り Σ=1 にしている。sum-to-one は固定状態上の確率分布への逆戻り。",
}


def iter_source_files(roots):
    """走査対象ファイルを列挙（除外ディレクトリを枝刈り）。"""
    for root in roots:
        if os.path.isfile(root):
            if root.endswith(SOURCE_EXTS):
                yield root
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS]
            for name in filenames:
                if name.endswith(SOURCE_EXTS):
                    yield os.path.join(dirpath, name)


def strip_line_comment(line):
    """行末の // コメントを除去（許可リスト判定後に呼ぶ）。コメント内の語を誤検出しないため。"""
    idx = line.find("//")
    return line[:idx] if idx != -1 else line


def scan_file(path):
    """1ファイルを走査し violations(list) と allowlisted(int) を返す。"""
    violations = []
    allowlisted = 0
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError as e:
        print(f"warn: skip {path}: {e}", file=sys.stderr)
        return violations, allowlisted

    for lineno, raw in enumerate(lines, start=1):
        line = raw.rstrip("\n")

        # 許可リスト: // CONF-OK が付いた行は判断済みとして除外。
        if ALLOW_RE.search(line):
            allowlisted += 1
            continue

        code = strip_line_comment(line)
        if not CONF_RE.search(code):
            continue  # confidence を含まない行は対象外（activation 単独操作はここで落ちる）

        hit_contracts = []
        for cid, _label, regexes in CONTRACTS:
            if any(rx.search(code) for rx in regexes):
                hit_contracts.append(cid)

        for cid in hit_contracts:
            violations.append((path, lineno, line.strip(), cid))

    return violations, allowlisted


def default_root():
    """LAVI のソース根 (project/application/code) を既定とする。無ければリポジトリ根。"""
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(here)
    cand = os.path.join(repo, "project", "application", "code")
    return cand if os.path.isdir(cand) else repo


def main(argv):
    roots = argv[1:] or [default_root()]
    for r in roots:
        if not os.path.exists(r):
            print(f"error: path not found: {r}", file=sys.stderr)
            return 2

    all_violations = []
    files_scanned = 0
    total_allowlisted = 0
    for path in iter_source_files(roots):
        files_scanned += 1
        v, a = scan_file(path)
        all_violations.extend(v)
        total_allowlisted += a

    print("=" * 72)
    print("confidence 契約チェック (C1–C4) — 正は CONSTITUTION.md §2")
    print(f"走査ファイル数: {files_scanned}  /  許可リスト除外行: {total_allowlisted}")
    print("=" * 72)

    if not all_violations:
        print("GREEN: 既知の危険パターンは検出されませんでした。")
        print("（注: 緑は『既知パターン不在』の監視であり、絶対安全の保証ではない）")
        return 0

    # 違反一覧
    per_contract = {}
    for path, lineno, text, cid in all_violations:
        per_contract[cid] = per_contract.get(cid, 0) + 1
        print()
        print(f"[{cid}] {path}:{lineno}")
        print(f"    {text}")
        print(f"    なぜ危険か: {WHY[cid]}")
        print(f"    対処: 修正する、または安全と判断するなら行末に  // CONF-OK: 理由  を付与")

    print()
    print("-" * 72)
    print("サマリ:")
    for cid in ("C1", "C2", "C3", "C4"):
        if cid in per_contract:
            print(f"    {cid}: {per_contract[cid]} 件")
    print(f"    合計違反: {len(all_violations)} 件")
    print("RED: confidence 契約違反を検出。CONSTITUTION.md §2 を参照。")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
