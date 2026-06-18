# LAVI — プロジェクト指示

## Meaning Architecture v2 — レビュー観点（confidence 契約 C1–C6）

会話設計は Intent/State 分類を捨て「意味の共同構築」へ移行中。
唯一の正は **`docs/meaning-architecture/CONSTITUTION.md` §2**。矛盾したら CONSTITUTION が勝つ。
`InterpNode` の `confidence` は **無界・非正規化・非競合の「支持度」**であって確率ではない。
これを破ると Belief Distribution へ逆戻りする。

### 自動チェック（C1–C4）— スクリプトが見る
`scripts/check_confidence_contract.py` が静的検出し、pre-commit フックが commit を止める。
（有効化: `git config core.hooksPath scripts/hooks`。緊急回避は `--no-verify`、判断の責任は commit 者）

- **C1** normalize 禁止 — `confidence` を総和/最大値で除算、normalize 系へ渡す
- **C2** clamp 禁止 — `std::clamp`/`(std::min)`/`(std::max)` 等で `confidence` を [0,1] 等に制限
- **C3** softmax 禁止 — `confidence` 群への softmax
- **C4** sum-to-one 禁止 — `confidence` 集合を Σ=1 にする

安全と判断した行は行末に `// CONF-OK: 理由` を付けて除外（判断を消さず形跡を残す）。
`RawNode.activation`（減衰する注目度）への clamp/normalize は**正当**で対象外。

### 人間レビュー観点（C5–C6）— 静的検出が難しいので目視で見る
コードレビュー時、以下は機械では捕まらないため必ず人間が確認する。

- **C5 confidence の時間減衰を入れていないか**
  証拠は append-only で時間では消えない。「古い証拠＝弱い証拠」という暗黙の確率モデルを
  忍び込ませない。confidence を経過時間・ターン数・フレーム数で減衰させる処理（`* decay`,
  `*= 0.9f`, `exp(-dt)` 等を `confidence` に適用）は **禁止**。
  減衰してよいのは `RawNode.activation` のみ。confidence が下がってよいのは**矛盾観測**による撤回時だけ。
  見るべき兆候: confidence の更新が `dt` / `deltaTime` / `frame` / `turn` / `age` / `decay` に依存していないか。

- **C6 RouteTarget 的な「閉じた enum を argmax して他候補を捨てる」分岐を新設していないか**
  解釈は開いた集合（文字列 label）で、矛盾仮説も並存させる。
  `enum class XxxIntent/Route/State { ... }` を作って単一値に確定し他を破棄する設計は **禁止**。
  既存の `GatekeeperManager::RouteTarget` は段階的に置換する対象であって、模倣する対象ではない。
  見るべき兆候: 新しい固定 enum＋`switch`/`argmax`/`top1` で意味を1つに畳んでいないか。
