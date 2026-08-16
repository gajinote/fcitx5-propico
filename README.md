# fcitx5-propico

Linux デスクトップ向け **fcitx5** 日本語入力プラグイン。  
バックエンドに [propico](https://github.com/gajinote/propico) を使用し、propico の「軽量フロントエンド × サーバー型変換エンジン」思想を fcitx5 上で実現します。

---

## 概要

| 項目 | 内容 |
|---|---|
| IMF | [fcitx5](https://github.com/fcitx/fcitx5) |
| バックエンド | [propico](https://github.com/gajinote/propico)（gRPC サーバー） |
| 言語 | C++20 |
| ビルド | CMake + extra-cmake-modules |

### 設計思想

- **フロントエンド（本リポジトリ）**: キーイベント処理・ローマ字かな変換・preedit 表示・候補ウィンドウ
- **バックエンド（propico）**: 辞書検索・スコアリング・学習永続化（SQLite）・複数端末同期
- 両者の契約は `propico.proto` のみ。将来 Android/macOS IME を作っても同じサーバーを使える

```
キーボード
    │
    ▼
fcitx5-propico (本プラグイン)        propico (別プロセス)
┌──────────────────────┐  gRPC    ┌────────────────────────┐
│ ローマ字→かな変換    │ ───────▶ │ 辞書検索（前方一致）   │
│ 候補ウィンドウ表示   │ ◀─────── │ 学習スコア（SQLite）   │
│ preedit 管理         │ Search   │ 複数端末同期           │
└──────────────────────┘ Learn    └────────────────────────┘
```

---

## 実装状況

| Stage | 内容 | 状態 |
|---|---|---|
| 1 | Echo エンジン（fcitx5 登録・preedit・commit） | ✅ 完了 |
| 2 | ローマ字→ひらがな変換（ローカル） | ✅ 完了 |
| 3 | Space → gRPC Search + 候補ウィンドウ | ✅ 完了 |
| 4 | 候補選択 → commit + Learn RPC | ✅ 完了 |
| 5 | Sync RPC（複数端末同期） | ✅ 完了 |

---

## 依存関係

### 実行時

- [fcitx5](https://github.com/fcitx/fcitx5) 5.x
- [propico](https://github.com/gajinote/propico) サーバーが `localhost:50051` で稼働していること（Stage 3 以降）

### ビルド時

```bash
sudo apt install -y \
  fcitx5 \
  libfcitx5-dev \
  fcitx5-modules-dev \
  extra-cmake-modules \
  libprotobuf-dev \
  protobuf-compiler \
  libgrpc++-dev \
  protobuf-compiler-grpc
```

---

## ビルド

```bash
git clone https://github.com/gajinote/fcitx5-propico.git
cd fcitx5-propico
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

インストール先:

```
/usr/lib/x86_64-linux-gnu/fcitx5/fcitx5-propico.so
/usr/share/fcitx5/addon/propico-addon.conf
/usr/share/fcitx5/inputmethod/propico.conf
```

---

## 使い方

### 1. propico サーバーを起動

```bash
# propico リポジトリをビルド済みの前提
./propico/build/propico_server dict/default.tsv ~/.local/share/propico/history.db
# → propico server listening on 0.0.0.0:50051
```

### 2. fcitx5 を再起動

```bash
fcitx5 -r
```

### 3. 入力メソッドに "propico" を追加

fcitx5 設定ツール → 入力メソッド → 追加 → "propico"

### 4. 入力操作

| キー | 動作 |
|---|---|
| 英数字 | ローマ字入力（preedit に変換されたひらがな表示） |
| `Space` | propico に検索を投げ、候補ウィンドウを開く |
| `1`〜`9` | 候補を選択してコミット |
| `Tab` / `↓↑` | 候補フォーカス移動 |
| `Enter` | preedit をそのままコミット |
| `Escape` | 候補を閉じる / preedit クリア |
| `BackSpace` | 1文字削除 |

---

## アーキテクチャ

詳細は [ARCH.md](ARCH.md) を参照。

---

## 実装計画

段階的な実装計画は [PLAN.md](PLAN.md) を参照。

---

## 関連リポジトリ

- [propico](https://github.com/gajinote/propico) — 変換エンジン・辞書・学習・同期サーバー（proto 正本）

---

## 参考

- propico 原典: 増井俊之, "POBox: A Pen-Based Interface Using Gesture Prediction", UIST 1999
- [fcitx5 開発ドキュメント](https://github.com/fcitx/fcitx5)
- [fcitx5-chinese-addons](https://github.com/fcitx/fcitx5-chinese-addons) — プラグイン実装の参考例

---

## ライセンス

LGPL-2.1-or-later（fcitx5 に合わせる）
