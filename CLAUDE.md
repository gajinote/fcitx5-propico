# プロジェクトの目的

* fcitx5 プラグインとして Linux デスクトップ上で日本語入力を提供する
* バックエンドとして propico（別リポジトリの gRPC サーバー）を利用する
* POBox の「軽量フロントエンド × サーバー型変換エンジン」思想を fcitx5 の Input Method Framework 上で実現する

# 前提プロジェクト: propico

* 変換エンジン・辞書・学習・同期を担当するサーバー実装（別リポジトリ）
* gRPC プロトコル定義（`propico.proto`）は propico 側が**正本**
* 本プロジェクトは proto をコピーで参照し、**クライアント**として振る舞う

# 大きな設計方針

## サーバー型分離を厳守する

* UI 層（本プロジェクト = fcitx5 プラグイン）と エンジン層（propico サーバー）を分離
* プラグイン側の責務: キーイベント処理・状態管理・ローマ字かな変換・候補ウィンドウ表示
* サーバー側の責務: 辞書検索・スコアリング・学習永続化・同期
* 両者の契約は `propico.proto` のみ

### 理由

* POBox の根幹思想「軽量フロントエンド × サーバー型変換エンジン」を構造で実現する
* Linux/Android/macOS など将来の IME 実装が対等な地位で並ぶ前提にする

## 入力中に UI をブロックしない

* fcitx5 の `keyEvent()` は同期的に呼ばれるため、gRPC 呼び出しは**必ず非同期化**する
* gRPC 結果は fcitx5 のイベントループに戻して preedit / 候補を更新する
* ブロッキング通信は UX を破壊する

## 既存 IME との棲み分け

* mozc や anthy とは同居可能にする（fcitx5 の IM 切替で選べる）
* ユーザー辞書・学習データは propico サーバー側に集約

# 段階的実装ロードマップ

| 段階 | 実装内容 | 確認ポイント | 状態 |
|---|---|---|---|
| 1 | Echo エンジン（入力→preedit→Enter commit） | fcitx5 登録・キーフックが動くこと | ✅ 完了 |
| 2 | ローマ字→ひらがな変換（ローカル） | 日本語が preedit に出ること | ✅ 完了 |
| 3 | Space キーで propico に gRPC Search | 候補ウィンドウに予測が並ぶこと | ✅ 完了 |
| 4 | 候補選択 → commit + Learn RPC | 学習の往復が機能すること | ✅ 完了 |
| 5 | Sync RPC（オプション） | 他端末連携 | 🔲 未着手 |

段階 1 は **propico に接続しない**。fcitx5 API 単体の統合確認に専念する。

# 技術スタック

* C++20
* fcitx5（`libfcitx5-dev` / `fcitx5-modules-dev`）
* gRPC + Protocol Buffers（段階 3 以降）
* CMake（`extra-cmake-modules` 経由で Fcitx5 のパッケージを解決）

# 開発環境（Ubuntu/Debian）

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

# 想定ディレクトリ構成

```
fcitx5-propico/
├─ CMakeLists.txt
├─ propico.proto            ← propico からコピー
├─ src/
│  ├─ propico_engine.h     ← InputMethodEngineV3 継承
│  ├─ propico_engine.cc    ← keyEvent 実装
│  ├─ factory.cc           ← FCITX_ADDON_FACTORY マクロ
│  ├─ romaji_kana.h/.cc    ← 段階 2 以降
│  └─ grpc_client.h/.cc    ← 段階 3 以降
└─ data/
   ├─ propico-addon.conf   ← fcitx5 へのプラグイン登録
   └─ propico.conf         ← IME メタデータ（名前・言語・アイコン）
```

# コーディング規約

* インデント: 2スペース
* 既存のコード規約・スタイルに従う
* 不要な機能追加や "改善" は行わない
* コメント・コミットメッセージ: 日本語 または 英語（既存の規約に従う）
* SQLite などのデータ永続化層は**本プロジェクトでは持たない**（サーバー側に集約）

# 参考

* propico リポジトリ — 変換エンジン・proto 定義の正本
* [fcitx5 開発ドキュメント](https://github.com/fcitx/fcitx5) — API リファレンス
* POBox 原典（増井俊之, 1996）— 設計思想の源流
