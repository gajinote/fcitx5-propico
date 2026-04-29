# fcitx5-pobox 実装計画

## 前提

- バックエンド: [pobox-neo](../pobox-neo)（全フェーズ完了済み ✅）
- proto 正本: `pobox-neo/pobox.proto`（本リポジトリへはコピーで参照）
- 実装言語: C++20
- ビルドシステム: CMake + extra-cmake-modules

---

## 現状 (2026-04-29)

| フェーズ | 内容 | 状態 |
|---|---|---|
| Stage 1 | Echo エンジン（fcitx5 登録・preedit・commit） | 🔲 未着手 |
| Stage 2 | ローマ字→ひらがな変換（ローカル） | 🔲 未着手 |
| Stage 3 | Space → gRPC Search + 候補ウィンドウ | 🔲 未着手 |
| Stage 4 | 候補選択 → commit + Learn RPC | 🔲 未着手 |
| Stage 5 | Sync RPC（オプション） | 🔲 未着手 |

---

## Stage 1 — Echo エンジン

**目標:** pobox-neo に接続しない。fcitx5 プラグイン登録・キーフック・preedit 表示・Enter でコミットが動くこと。

### 成果物

| ファイル | 内容 |
|---|---|
| `CMakeLists.txt` | fcitx5 / ECM パッケージ解決、プラグイン共有ライブラリとして出力 |
| `src/pobox_engine.h` | `PoboxEngine`（InputMethodEngineV3）+ `PoboxState`（InputContextProperty）宣言 |
| `src/pobox_engine.cc` | `keyEvent()` 実装: 英数字バッファ追記、Enter でコミット、Escape でクリア |
| `src/factory.cc` | `FCITX_ADDON_FACTORY(PoboxEngine)` マクロ |
| `data/pobox-addon.conf` | アドオン登録（Category = InputMethod, Library = fcitx5-pobox） |
| `data/pobox.conf` | IME メタデータ（Name = PoBox Neo, LangCode = ja） |

### 確認ポイント

```bash
# プラグインを fcitx5 に読み込ませてテキストエディタで動作確認
# 1. アルファベットを打つと preedit に表示される
# 2. Enter で preedit がコミットされる
# 3. Escape で preedit がクリアされる
# 4. fcitx5-diagnose で "pobox" がリストに現れる
```

---

## Stage 2 — ローマ字→ひらがな変換

**目標:** アルファベット入力が preedit にひらがなで表示される。gRPC は不要。

### 成果物

| ファイル | 内容 |
|---|---|
| `src/romaji_kana.h/.cc` | ローマ字→ひらがな変換テーブル + 入力バッファ管理 |

### RomajiKana 仕様

```cpp
class RomajiKana {
public:
    // c を追加し、確定したひらがなを返す。未確定残留は pending() で取得
    std::string feed(char c);
    // 末尾 1 文字削除（BackSpace 対応）
    void backspace();
    // 現在の未確定ローマ字（preedit の末尾に表示する）
    std::string pending() const;
    // 全バッファクリア
    void reset();
};
```

### 変換ルール（主要サブセット）

| ローマ字 | ひらがな | 備考 |
|---|---|---|
| `a/i/u/e/o` | あ/い/う/え/お | |
| `ka/ki/ku/ke/ko` | か/き/く/け/こ | |
| `sa/si/su/se/so` | さ/し/す/せ/そ | `shi` も可 |
| `nn` | ん | `n` + 子音前も ん確定 |
| `tt` + 子音 | っ + 子音 | 促音: `tte` → って |
| `xa` / `la` | ぁ | 小文字 |

### 確認ポイント

```
"aiueo"  → あいうえお
"kanji"  → かんじ
"nippon" → にっぽん
"tsu"    → つ
"nn"     → ん
"nk"     → んk（n + 子音前で ん確定）
```

---

## Stage 3 — gRPC Search + 候補ウィンドウ

**目標:** Space キーで pobox-neo に検索を投げ、候補ウィンドウに結果を表示する。

### 前提条件

- pobox-neo サーバーが `localhost:50051` で動いていること
- `pobox.proto` を本リポジトリにコピー済み

### 成果物

| ファイル | 内容 |
|---|---|
| `pobox.proto` | pobox-neo からコピー（Search / Learn / Sync 定義） |
| `src/grpc_client.h/.cc` | 非同期 gRPC ラッパー（CompletionQueue スレッド） |
| `src/pobox_engine.cc` | Space キー処理: `searchAsync()` 発行 → `onSearchResult()` で候補更新 |

### CMakeLists.txt 追加要素

```cmake
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

# proto コンパイル
get_filename_component(PROTO_FILE "pobox.proto" ABSOLUTE)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})
grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_FILE})

target_sources(fcitx5-pobox PRIVATE
    ${PROTO_SRCS} ${GRPC_SRCS}
    src/grpc_client.cc)
target_link_libraries(fcitx5-pobox PRIVATE
    protobuf::libprotobuf gRPC::grpc++)
```

### 非同期フロー

```
Space キー
  │
  ├── state.mode = Selecting
  ├── updatePreedit() でスピナー表示（オプション）
  └── grpc_client_->searchAsync(state.reading, [ic](resp) {
            // fcitx5 メインスレッドで呼ばれる
            state.candidates = resp.candidates();
            showCandidates(ic, state);
      });
```

### 確認ポイント

```
1. "ai" + Space → pobox-neo に prefix="あい" で Search RPC が飛ぶ
2. 候補ウィンドウに「愛」「挨拶」などが並ぶ
3. サーバー未起動でも入力がフリーズしない（タイムアウト後に COMPOSING へ戻る）
```

---

## Stage 4 — 候補選択 + Learn RPC

**目標:** 候補ウィンドウで選択 → コミット + Learn RPC で学習が機能する。

### keyEvent 追加ハンドリング（SELECTING 状態）

| キー | アクション |
|---|---|
| `1`〜`9` | candidates[n-1] を選択 → commit + learnAsync |
| `Tab` / `↓` | 次候補にフォーカス移動 |
| `Shift+Tab` / `↑` | 前候補にフォーカス移動 |
| `Enter` | フォーカス中の候補を選択 |
| `Space` | 次ページ |
| `Escape` | COMPOSING に戻る（候補を閉じる） |

### commitCandidate の処理

```cpp
void PoboxEngine::commitCandidate(fcitx::InputContext* ic,
                                   PoboxState& state, int index) {
    const auto& c = state.candidates[index];
    ic->commitString(c.text());
    grpc_client_->learnAsync(c.id(), state.reading);  // 非同期（戻りは無視可）
    state = PoboxState{};  // リセット
    updatePreedit(ic, state);
}
```

### 確認ポイント

```
1. 候補選択 → テキストがコミットされる
2. 同じプレフィックスで再検索すると、選択した候補のスコアが上昇する
3. pobox-neo サーバーを再起動しても学習が引き継がれる（SQLite 永続化）
```

---

## Stage 5 — Sync RPC（オプション）

**目標:** 複数端末で学習データを同期できる仕組みを追加する。

### 実装内容

- `GrpcClient::syncAsync()` に last_sync_timestamp を渡す
- 設定ファイル（`~/.config/fcitx5/pobox.conf`）に `last_sync_timestamp` を保存
- 候補コミット後 or 一定間隔で差分 Sync を発火

### 確認ポイント

```
1. 2台のマシンが同じ pobox-neo サーバーに接続している状態で
   マシンA で "あい:愛" を3回選択
   マシンB で sync → "愛" のスコアが上昇している
```

---

## 依存パッケージ一覧

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

| パッケージ | 使用 Stage |
|---|---|
| `fcitx5`, `libfcitx5-dev`, `fcitx5-modules-dev` | Stage 1〜 |
| `extra-cmake-modules` | Stage 1〜 |
| `libprotobuf-dev`, `protobuf-compiler`, `libgrpc++-dev`, `protobuf-compiler-grpc` | Stage 3〜 |

---

## 優先順位と依存関係

```
Stage 1 ──▶ Stage 2 ──▶ Stage 3 ──▶ Stage 4 ──▶ Stage 5
```

各 Stage は前の Stage の完了を前提とする。
Stage 3 は pobox-neo サーバーが起動していること（別プロセス）が必要。

---

## 完了基準チェックリスト

### Stage 1
- [ ] `cmake --build build` が警告 0 でビルドできる
- [ ] fcitx5 を再起動すると pobox プラグインがロードされる
- [ ] テキストエディタで英数字入力 → preedit 表示 → Enter でコミット

### Stage 2
- [ ] ローマ字を打つと preedit がひらがなに変わる
- [ ] `nippon` → `にっぽん` が正しく変換される
- [ ] BackSpace で1文字削除できる

### Stage 3
- [ ] Space で候補ウィンドウが開く
- [ ] サーバー未起動でも入力がフリーズしない

### Stage 4
- [ ] 数字キーで候補を選択してコミットできる
- [ ] 再検索で選択候補のスコアが上昇している

### Stage 5
- [ ] 別端末で同じ pobox-neo サーバーに接続した場合に学習が共有される
