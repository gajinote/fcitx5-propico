# fcitx5-propico 実装計画

## 前提

- バックエンド: [propico](../propico)（変換エンジン・辞書・学習・同期）
- proto 正本: `propico/propico.proto`（本リポジトリへはコピーで参照）
- 実装言語: C++20
- ビルドシステム: CMake + extra-cmake-modules

---

## 現状 (2026-06-19)

| フェーズ | 内容 | 状態 |
|---|---|---|
| Stage 1 | Echo エンジン（fcitx5 登録・preedit・commit） | ✅ 完了 |
| Stage 2 | ローマ字→ひらがな変換（ローカル） | ✅ 完了 |
| Stage 3 | Space → gRPC Search + 候補ウィンドウ | ✅ 完了 |
| Stage 4 | 候補選択 → commit + Learn RPC | ✅ 完了 |
| Stage 5 | Sync RPC（オプション） | 🔲 未着手 |

---

## Stage 1 — Echo エンジン ✅

**目標:** propico に接続しない。fcitx5 プラグイン登録・キーフック・preedit 表示・Enter でコミットが動くこと。

### 成果物

| ファイル | 内容 |
|---|---|
| `CMakeLists.txt` | fcitx5 / ECM パッケージ解決、プラグイン共有ライブラリとして出力 |
| `src/propico_engine.h` | `PropicoEngine`（InputMethodEngineV3）+ `PropicoState`（InputContextProperty）宣言 |
| `src/propico_engine.cc` | `keyEvent()` 実装: 英数字バッファ追記、Enter でコミット、Escape でクリア |
| `src/factory.cc` | `FCITX_ADDON_FACTORY(PropicoEngineFactory)` マクロ |
| `data/propico-addon.conf` | アドオン登録（Category = InputMethod, Library = fcitx5-propico） |
| `data/propico.conf` | IME メタデータ（Name = Propico, LangCode = ja） |

### 確認ポイント

```bash
# 1. アルファベットを打つと preedit に表示される
# 2. Enter で preedit がコミットされる
# 3. Escape で preedit がクリアされる
# 4. fcitx5-diagnose で "propico" がリストに現れる
```

---

## Stage 2 — ローマ字→ひらがな変換 ✅

**目標:** アルファベット入力が preedit にひらがなで表示される。gRPC は不要。

### 成果物

| ファイル | 内容 |
|---|---|
| `src/romaji_kana.h/.cc` | ローマ字→ひらがな変換テーブル + 入力バッファ管理 |

### RomajiKana 仕様

```cpp
class RomajiKana {
public:
    std::string feed(char c);    // c を追加し、確定したひらがなを返す
    void backspace();            // 末尾 1 文字削除（BackSpace 対応）
    std::string pending() const; // 現在の未確定ローマ字
    void reset();                // 全バッファクリア
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

---

## Stage 3 — gRPC Search + 候補ウィンドウ ✅

**目標:** Space キーで propico に検索を投げ、候補ウィンドウに結果を表示する。

### 成果物

| ファイル | 内容 |
|---|---|
| `propico.proto` | propico からコピー（Search / Learn / Sync 定義） |
| `src/grpc_client.h/.cc` | 非同期 gRPC ラッパー（detach スレッド + EventDispatcher） |
| `src/propico_engine.cc` | Space キー処理: `searchAsync()` 発行 → コールバックで候補更新 |

### 非同期フロー

```
Space キー
  │
  ├── state.mode = Selecting
  ├── updatePreedit()
  └── grpc_client_->searchAsync(state.reading, [ic](resp) {
            // GrpcClient の dispatcher_->schedule() により
            // fcitx5 メインスレッドで呼ばれる
            state.candidates = resp.candidates();
            showCandidates(ic, state);
      });
```

### 注意: 二重スケジューリングを避ける

GrpcClient 内の `dispatcher_->schedule()` がすでにメインスレッドへ戻す。
エンジン側コールバックで `scheduleWithContext()` を重ねると候補が表示前にクリアされる。

---

## Stage 4 — 候補選択 + Learn RPC ✅

**目標:** 候補ウィンドウで選択 → コミット + Learn RPC で学習が機能する。

### keyEvent ハンドリング（SELECTING 状態）

| キー | アクション |
|---|---|
| `1`〜`9` | candidates[n-1] を選択 → commit + learnAsync |
| `↓` / `→` | 次候補にカーソル移動 |
| `↑` / `←` | 前候補にカーソル移動 |
| `Enter` | カーソル中の候補を選択 |
| `Space` | 次ページ |
| `Escape` | COMPOSING に戻る（候補を閉じる） |

### commitCandidateAt の処理

```cpp
void PropicoEngine::commitCandidateAt(InputContext *ic, PropicoState &state, int idx) {
    onCandidateSelected(ic, state.candidates[idx].text, state.candidates[idx].id);
}

void PropicoEngine::onCandidateSelected(InputContext *ic,
                                         const std::string &text,
                                         const std::string &id) {
    ic->commitString(text);
    grpc_client_->learnAsync(id, state->reading);
    // state リセット
}
```

### CommonCandidateList を使用

`DisplayOnlyCandidateList` ではなく `CommonCandidateList` + `PropicoCandidateWord` を使い、
カーソル移動・クリック選択に対応する。

---

## Stage 5 — Sync RPC（オプション）

**目標:** 複数端末で学習データを同期できる仕組みを追加する。

### 実装内容

- `GrpcClient::syncAsync()` に last_sync_timestamp を渡す
- 設定ファイル（`~/.config/fcitx5/propico.conf`）に `last_sync_timestamp` を保存
- 候補コミット後 or 一定間隔で差分 Sync を発火

### 確認ポイント

```
1. 2台のマシンが同じ propico サーバーに接続している状態で
   マシンA で "あい:愛" を複数回選択
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
  ✅           ✅           ✅           ✅          🔲
```

---

## 完了基準チェックリスト

### Stage 1 ✅
- [x] `cmake --build build` が警告 0 でビルドできる
- [x] fcitx5 を再起動すると propico プラグインがロードされる
- [x] テキストエディタで英数字入力 → preedit 表示 → Enter でコミット

### Stage 2 ✅
- [x] ローマ字を打つと preedit がひらがなに変わる
- [x] `nippon` → `にっぽん` が正しく変換される
- [x] BackSpace で1文字削除できる

### Stage 3 ✅
- [x] Space で候補ウィンドウが開く
- [x] サーバー未起動でも入力がフリーズしない（タイムアウト後に COMPOSING へ戻る）

### Stage 4 ✅
- [x] 数字キーで候補を選択してコミットできる
- [x] ↑↓ カーソルキーで候補を移動できる
- [x] Learn RPC がサーバーに届く

### Stage 5
- [ ] 別端末で同じ propico サーバーに接続した場合に学習が共有される
