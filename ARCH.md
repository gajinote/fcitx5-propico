# fcitx5-pobox アーキテクチャ設計

## 全体概観

```
ユーザーキー入力
      │
      ▼
┌─────────────────────────────────────────┐
│ fcitx5 コア                              │
│  InputContext, KeyEvent, CandidateList  │
└────────────────┬────────────────────────┘
                 │ keyEvent() / activate() / deactivate()
                 ▼
┌─────────────────────────────────────────┐
│ PoboxEngine  (InputMethodEngineV3)      │  ← 本プラグイン
│                                         │
│  ┌─────────────┐  ┌──────────────────┐  │
│  │ RomajiKana  │  │ PoboxState       │  │
│  │  (ローカル) │  │  (per-context)   │  │
│  └─────────────┘  └──────────────────┘  │
│                                         │
│  ┌──────────────────────────────────┐   │
│  │ GrpcClient  (非同期スタブ)       │   │
│  └──────────────┬───────────────────┘   │
└─────────────────┼───────────────────────┘
                  │ gRPC (localhost:50051)
                  ▼
┌─────────────────────────────────────────┐
│ pobox-neo サーバー                       │
│  Search / Learn / Sync RPC              │
│  Dictionary + Scorer + History(SQLite)  │
└─────────────────────────────────────────┘
```

## コンポーネント責務

| コンポーネント | ファイル | 責務 |
|---|---|---|
| `PoboxEngine` | `src/pobox_engine.h/.cc` | fcitx5 API の窓口。`keyEvent()` 実装。状態機械の駆動 |
| `PoboxState` | `src/pobox_engine.h` | InputContext 単位の可変状態（バッファ・候補リスト・モード） |
| `RomajiKana` | `src/romaji_kana.h/.cc` | ローマ字→ひらがな変換（ローカル処理・gRPC 不要） |
| `GrpcClient` | `src/grpc_client.h/.cc` | pobox-neo との非同期 gRPC 通信ラッパー |
| `factory.cc` | `src/factory.cc` | `FCITX_ADDON_FACTORY` マクロ。プラグイン登録エントリポイント |

---

## 状態機械

```
            activate()
  ─────────────────────────▶
  INACTIVE                  IDLE
  ◀─────────────────────────
            deactivate()
                              │  英数字キー
                              ▼
                          COMPOSING ─────── BackSpace ──▶ (文字削除)
                              │                            (空なら IDLE)
                              │  Space
                              ▼
                          SELECTING ────── Escape ──────▶ COMPOSING
                              │
                              │  Enter / 数字 / Tab
                              ▼
                           commit → IDLE
```

### 各状態での主なキーマップ

| 状態 | キー | アクション |
|---|---|---|
| IDLE | 英数字 | バッファに追加、COMPOSING へ |
| COMPOSING | 英数字 | バッファ追記 |
| COMPOSING | BackSpace | バッファ末尾削除、空なら IDLE |
| COMPOSING | Space | GrpcClient::Search 非同期発行、SELECTING へ |
| COMPOSING | Enter | preedit をそのままコミット |
| COMPOSING | Escape | バッファクリア、IDLE へ |
| SELECTING | 1〜9 / Tab | 候補選択 → commit + GrpcClient::Learn 非同期発行 → IDLE |
| SELECTING | Space | 次候補へ（ページング） |
| SELECTING | Escape | COMPOSING に戻す |

---

## 非同期 gRPC 設計

fcitx5 の `keyEvent()` は同期的に呼ばれ、長時間ブロックすると UI が固まる。
そのため gRPC 呼び出しはすべて `GrpcClient` 内でスレッドに投げ、結果を fcitx5 のイベントループへ戻す。

```
keyEvent()
  │
  ├── RomajiKana::convert() ── 同期（μs オーダー）
  │
  └── GrpcClient::searchAsync(prefix, callback)
           │
           └── completion_queue_ スレッドへ投げる
                     │  (数 ms 後)
                     └── fcitx5 EventDispatcher::scheduleWithContext()
                               │
                               └── PoboxEngine::onSearchResult()
                                     候補ウィンドウ更新
```

### GrpcClient 内部構造

```cpp
class GrpcClient {
    // 非同期スタブ（CompletionQueue ベース）
    std::unique_ptr<pobox::PoBox::Stub> stub_;
    grpc::CompletionQueue cq_;
    std::thread cq_thread_;   // CompletionQueue を回すスレッド

    // fcitx5 イベントループへの戻し口
    fcitx::EventDispatcher* dispatcher_;

public:
    void searchAsync(const std::string& prefix,
                     std::function<void(SearchResponse)> callback);
    void learnAsync(const std::string& candidate_id,
                    const std::string& prefix);
    void syncAsync(int64_t last_sync_ts,
                   std::function<void(SyncResponse)> callback);
};
```

`searchAsync` コールバックは必ず fcitx5 メインスレッドで呼ばれるよう `EventDispatcher::scheduleWithContext()` でラップする。

---

## PoboxState（InputContext 単位の状態）

```cpp
struct PoboxState : public fcitx::InputContextProperty {
    enum class Mode { Idle, Composing, Selecting };

    Mode        mode     = Mode::Idle;
    std::string romaji;      // 入力中のローマ字バッファ（"aig" など）
    std::string reading;     // 変換済みひらがな（"あい" + 未確定 "g"）
    std::vector<pobox::Candidate> candidates;
    int         candidate_page = 0;  // ページング用
};
```

`InputContextProperty` は fcitx5 が `InputContext` ごとに管理するため、複数アプリが同時に開いていても状態が混ざらない。

---

## PoboxEngine クラス概観

```cpp
class PoboxEngine : public fcitx::InputMethodEngineV3 {
public:
    explicit PoboxEngine(fcitx::Instance* instance);

    void keyEvent(const fcitx::InputMethodEntry& entry,
                  fcitx::KeyEvent& event) override;

    void activate(const fcitx::InputMethodEntry& entry,
                  fcitx::InputContextEvent& event) override;

    void deactivate(const fcitx::InputMethodEntry& entry,
                    fcitx::InputContextEvent& event) override;

private:
    fcitx::Instance*              instance_;
    std::unique_ptr<RomajiKana>   romaji_kana_;
    std::unique_ptr<GrpcClient>   grpc_client_;
    fcitx::FactoryFor<PoboxState> state_factory_;

    void updatePreedit(fcitx::InputContext* ic, PoboxState& state);
    void showCandidates(fcitx::InputContext* ic, PoboxState& state);
    void commitCandidate(fcitx::InputContext* ic, PoboxState& state, int index);
    void onSearchResult(fcitx::InputContext* ic,
                        const pobox::SearchResponse& resp);
};
```

---

## ローマ字→かな変換（RomajiKana）

サーバーに依存しないローカル処理。

```
入力: "aisat" → 途中バッファ管理あり
出力: 確定ひらがな "あいさ" + 未確定ローマ字 "t"
```

変換テーブルは静的配列として組み込み、標準的なヘボン式/訓令式を網羅する。
「n」の処理（「na」まで待つ vs 子音前は確定）など境界ケースを明示的にテストする。

---

## gRPC 契約

| RPC | 呼び出しタイミング | 引数 |
|---|---|---|
| `Search` | Space キー（COMPOSING → SELECTING） | `prefix` = 現在のひらがな読み |
| `Learn` | 候補確定時（SELECTING → IDLE） | `candidate_id`, `prefix` |
| `Sync` | 明示的なトリガー（Phase 5） | `last_sync_timestamp` |

`prefix` には常にひらがな文字列を渡す（ローマ字のまま渡さない）。

---

## ファイル・ディレクトリ構成

```
fcitx5-pobox/
├── CMakeLists.txt
├── pobox.proto                  ← pobox-neo からコピー（proto 正本は pobox-neo 側）
├── src/
│   ├── pobox_engine.h           ← PoboxEngine + PoboxState 宣言
│   ├── pobox_engine.cc          ← keyEvent / activate / deactivate 実装
│   ├── factory.cc               ← FCITX_ADDON_FACTORY マクロ
│   ├── romaji_kana.h/.cc        ← ローマ字→ひらがな（Stage 2〜）
│   └── grpc_client.h/.cc        ← 非同期 gRPC ラッパー（Stage 3〜）
└── data/
    ├── pobox-addon.conf         ← fcitx5 アドオン登録
    └── pobox.conf               ← IME メタデータ（名前・言語）
```

---

## 依存関係グラフ

```
factory.cc
  └── PoboxEngine
        ├── RomajiKana          (Stage 2〜)  依存: なし（スタンドアロン）
        ├── PoboxState          (State 1〜)  依存: fcitx5 API
        └── GrpcClient          (Stage 3〜)  依存: gRPC/protobuf, pobox.proto
              └── pobox-neo サーバー（外部プロセス）
```

Stage 1 は `GrpcClient` を持たず、`RomajiKana` も持たないシンプルな構成からスタートする。

---

## 設計上の制約と対処

| 制約 | 対処 |
|---|---|
| `keyEvent()` は同期 | gRPC 呼び出しはすべて非同期スレッドに委譲 |
| pobox-neo サーバーが未起動の場合 | 接続失敗時は COMPOSING のまま候補なしで継続（入力は止めない） |
| 複数 InputContext が並列 | `PoboxState` は InputContext ごとに独立して保持 |
| fcitx5 のメインスレッド以外からの UI 更新禁止 | コールバックは `EventDispatcher::scheduleWithContext()` 経由 |
