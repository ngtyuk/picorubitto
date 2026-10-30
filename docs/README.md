# 文書一覧とリポジトリの配置

## 利用者向け

| 文書 | 内容 |
| --- | --- |
| [プロジェクト概要・初回セットアップ（English）](../README.md) | できること、PC側ツールの導入、ブラウザ・実機の初回起動 |
| [プロジェクト概要・初回セットアップ（日本語）](../README.ja.md) | 英語版READMEと同じセットアップ手順 |
| [セットアップ](getting-started.md) | 依存関係、ボード別ビルド、ブラウザでの初回起動 |
| [実機ガイド](hardware.md) | GPIO配線、UF2書き込み、SDカードからの起動 |
| [ゲーム作成](game-development.md) | ディレクトリ、最小コード、アセット変換、ROM生成 |
| [APIリファレンス](api-reference.md) | `Game` APIの引数・戻り値・形式 |
| [サンプル一覧](../examples/README.md) | 2D・3Dサンプルの操作とROM生成 |
| [エミュレータ](../emulator/README.md) | ローカルゲームの検出、操作、確認ページ |
| [Browser Studio](../studio/README.md) | コード・ドット絵の編集、コンパイル、SDカード用ZIP |

## エンジン開発者向け

| 文書 | 内容 |
| --- | --- |
| [依存バージョン](dependencies.md) | 固定版、取得・更新、ROMの移行 |
| [Webフロントエンド](web-frontend.md) | Funicular画面、ブラウザブリッジ、起動順 |
| [開発ガイド](agent-development.md) | 変更範囲ごとのビルドと検証 |
| [互換性テスト](compatibility-testing.md) | 実機の統合ROM、WASM契約テスト、ホスト・ブラウザ回帰テスト |
| [ランタイム構成](runtime-architecture.md) | 共通部分とRP2350・WASMの責務 |
| [API拡張](extension-guide.md) | `Game` APIの追加・変更手順 |
| [性能メモ](performance-notes.md) | 描画・音声・メモリの制約、release設定、測定 |
| [エージェント向けルール](../AGENTS.md) | 検証、生成物、Git操作のルール |

## ソースの配置

実行先・役割ごとに配置します。ゲームからは`Game` APIだけを呼び出します。

| 場所 | 責務 |
| --- | --- |
| `src/`, `include/` | RP2350のTFT、SD、入力、PSRAM、音声と内部C境界 |
| `mrbgems/picoruby-game/` | 実機のRuby APIとC拡張 |
| `mrbgems/picoruby-game-common/` | 両実行先に組み込む共通gemのビルドプロファイル |
| `tools/runtime_profile.rb` | 両実行先へ組み込む共通gemの一覧 |
| `emulator/` | WASMビルド設定、Rubyアダプタ、描画・入力・保存、起動ページ |
| `web/` | Funicular共通コンポーネントとPicoRuby WASMのUI起動 |
| `studio/` | コード・ドット絵エディタ、コンパイラWorker、エミュレータ連携 |
| `examples/games/2d/`, `examples/games/3d/` | リポジトリだけで実行できるAPIサンプル |
| `tests/portable/` | 実機とWASMで共有する契約テストと固定アセット |
| `tests/native/` | C回帰テストと依存取得ツールのRubyテスト |
| `tests/web/` | Funicularの制御と画素編集・素材変換の回帰テスト |
| `tools/build/` | ビルド入口`workflow.rb`、ROM・UF2生成、Studioコンパイラ設定 |
| `tools/assets/` | PNG・フォントの実行時形式への変換 |
| `tools/fixtures/` | サンプル音声・契約テスト用フォントの再生成 |
| `tools/diagnostics/` | 本番コードをホストで実行する処理回数の診断 |

`emulator/setup_runtime.rb`はWASM生成物をコピーし、ブラウザ向けのスケジューラ調整を
適用するビルド工程です。`workflow.rb engine`から使うため、実行時のJavaScriptと同様に
必要なファイルです。

## 名前と追加先の目安

- Ruby・C・Pythonは`snake_case`、ブラウザ側の複数語ファイル名と文書名は`kebab-case`を
  基本とします。`mruby_config.rb`やgem名など、ビルドツールが使う既存名は維持します。
- 新しいゲームの検証は`local-games/<ゲーム名>/main.rb`に置きます。ローカル専用の
  ゲームごとにエミュレータHTMLを増やさず、`/emulator/?game=<ゲーム名>`を使います。
- 公開するサンプルは`examples/games/`、共通契約は`tests/portable/`、ホスト側の
  回帰テストは`tests/native/`、ブラウザ側ロジックのテストは`tests/web/`に置きます。
- READMEは概要と最初の入口に絞り、配線・形式・検証の詳細は上記の専用文書へ追記します。
  文書を追加・削除するときは、この索引と参照リンクも更新します。

## 生成物と固定データ

| Git管理対象外 | 内容 |
| --- | --- |
| `build/`, `dist/` | ファームウェア、ROM、コンパイラなどの出力 |
| `deps/` | Pico SDKなどの外部依存物 |
| `emulator/picoruby/`, `studio/runtime/` | 生成したWASM・JavaScript（`.gitkeep`だけ管理） |
| `studio/dist/`, `studio/node_modules/` | Studio用の出力・ローカル依存物 |
| `local-games/` | 個人のゲーム・検証ROM・アセット |

ゲームROM（`*.mrb`）やローカルキャッシュも管理対象外です。一方、`examples/`の画像・
音声と`tests/portable/assets/`は、サンプルやテストを再現するための固定データとして管理します。
`*.raw`・`*.wav`・`*.pbf`を一律に除外しないでください。
