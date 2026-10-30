# PicoRubitto エージェント向け開発手順

このファイルは、このリポジトリ以下で作業するAIエージェント向けのプロジェクト
ルールです。人向けの導入手順は[`README.md`](README.md)、詳細な変更・
検証手順は[`docs/agent-development.md`](docs/agent-development.md)を参照してください。

## プロジェクトの境界

- RP2350実機とブラウザWASMの両方で、同じ`Game` APIを使うPicoRubyゲームを動かす。
- 実機固有処理は`src/`、`include/`、`mrbgems/picoruby-game/`のCバックエンドに置く。
- WASM固有処理は`emulator/game_adapter.rb`と`emulator/emulator.js`に置く。
- 実機/WASM共通の契約は`mrbgems/picoruby-game-common/`、`tools/runtime_profile.rb`、
  `tests/portable/`で管理する。
- `local-games/`はローカル検証用で、ゲームROMやゲーム固有アセットをこのエンジン
  リポジトリへ追加しない。生成物もコミットしない。

ゲームからはRP2350のC関数やブラウザのJavaScriptを直接呼ばず、`Game` APIだけを使う。
APIや機能を変更するときは、実機とWASMの両方を同じ変更で更新する。

## 必須ワークフロー

`src/`、`include/`、`mrbgems/`、`mruby_config.rb`、
`emulator/picoruby-wasm-gaming.rb`、`emulator/game_adapter.rb`、
`tools/runtime_profile.rb`、`CMakeLists.txt`、または`Game` API・ランタイム契約を変更
した場合は、完了前にUF2とWASMの両方をビルドする。

```sh
cd /path/to/picorubitto
export PICORUBY_ROOT=/path/to/picoruby
export ARM_TOOLCHAIN=/path/to/arm-none-eabi/bin
source /path/to/emsdk/emsdk_env.sh
export EM_NODE_JS="$EMSDK_NODE"
ruby tools/build/workflow.rb engine
```

このコマンドはPicoRubyのホストコンパイラ、RP2350ファームウェア、UF2、WASMランタイム
を更新する。通常の成果物は次の場所に出る。

- `build/firmware-release/pico_ruby_gaming.uf2`
- `build/firmware-release/pico_ruby_gaming.elf`
- `build/firmware-release/pico_ruby_gaming.map`
- `emulator/picoruby/`

エンジン変更では、ビルドだけで完了にしない。変更内容に応じて、次の仕様文書も同じ
変更で更新する。

- 公開API、引数、戻り値、データ形式、例外：`docs/api-reference.md`
- RP2350/WASMの責務分担やビルド設定：`docs/runtime-architecture.md`
- API追加・変更の実装手順：`docs/extension-guide.md`
- 契約テスト、対応機能、実機/WASM差分：`docs/compatibility-testing.md`
- TFT、スプライト、転送方式などの性能特性：`docs/performance-notes.md`
- 導入、配線、ROM作成、実機操作：`docs/getting-started.md`、`docs/hardware.md`、
  `docs/game-development.md`。ブラウザ操作は`emulator/README.md`または`studio/README.md`

新しい文書を追加・削除した場合は`docs/README.md`の索引も更新する。文書更新後は、
コード例のパスとコマンドが現行実装に一致していること、相対リンクが切れていないことを
確認する。

`--firmware-only`と`--wasm-only`は、変更範囲が明確に片方だけの短時間確認、または
依存環境の切り分けに限って使う。エンジン変更の完了確認を片方だけで済ませない。

ゲームRubyやゲーム固有アセットだけの変更は、対象ゲームのROMを再生成する。

```sh
ruby tools/build/workflow.rb game local-games/my-game
```

元データからアセットを作り直すゲームだけ、`--prepare-assets`を追加する。

## Git管理

- 作業開始前に`git status --short`、現在のブランチ、既存差分を確認する。既存の変更を
  自分の変更と決めつけず、上書き・削除・復元しない。
- 作業終了時は`git diff --check`を実行し、`git add`には変更した意図的なファイルを
  パス指定で渡す。`git add -A`や`git add .`で生成物をまとめてstageしない。
- エンジンリポジトリに含めるのは、エンジン本体、共通API、テスト、ビルドツール、
  利用者向けドキュメント。`build/`、`deps/`、`emulator/picoruby/`、ゲームROM、
  ゲーム固有アセットはコミットしない。
- `local-games/`はローカル検証用で、このリポジトリのコミット対象外。ゲームを公開・
  共有するときは、ゲームごとに別のGitリポジトリとして管理する。
- コミットは1つの論理的な変更単位にまとめ、メッセージに変更内容を簡潔に書く。コミット
  前にstage内容を`git diff --cached --stat`と`git diff --cached --check`で確認する。
- `git reset --hard`、`git checkout --`、強制push、履歴改変、他人のコミットへのamendは、
  ユーザーが対象と操作を明示的に依頼した場合だけ行う。
- push前に`git remote -v`、`git branch --show-current`、追跡先を確認し、remote名、
  ブランチ名、push先を推測しない。追跡先が設定されていない場合やGitHub以外を指す場合は、
  pushせずに状況を報告する。
- コミットやpushを行った場合は、最後の報告にコミットID、ブランチ、pushの有無、検証結果を
  記載する。

### 変更後のコミットとプッシュ

- 変更後の検証が成功し、ユーザーから「pushしない」と指定されていなければ、意図した
  ファイルだけをstageしてコミットし、確認済みのGitHub追跡先へpushする。
- push前に`git diff --cached --stat`と`git diff --cached --check`を確認する。生成物、依存物、
  ゲームROM、他の作業者の差分は含めない。
- 認証エラー、保護ブランチ、追跡先不在などで安全にpushできない場合は、無理に回避せず、
  コミットまでの状態と原因を報告する。

## 検証の最低条件

変更後は変更範囲に応じて次を実行する。

- 共通APIまたはエンジン変更：`ruby tools/build/workflow.rb engine`、WASM契約テスト、実機用
  契約ROMの生成。可能なら実機でも起動する。
- Cバックエンド変更：上記に加えてUSBシリアルの初期化ログ、TFT、SD、入力を確認する。
- WASMアダプタ／JavaScript変更：WASM契約テスト、通常エミュレータ、ブラウザコンソール。
- ゲームだけの変更：ゲームROM生成と通常エミュレータ。エンジンを変更していない限り
  UF2/WASMの再ビルドは不要。

契約テストの詳細は[`docs/compatibility-testing.md`](docs/compatibility-testing.md)を
参照する。コードを変更した場合は、可能な範囲で次も確認する。

```sh
node --check emulator/emulator.js
git diff --check
```

Rubyファイルを変更した場合は、対象ファイルへ`ruby -c path/to/file.rb`も実行する。

## 変更時の注意

- APIを追加・変更したら、RP2350 C実装、WASMアダプタ、契約テスト、ドキュメントを
  同時に更新する。任意機能は`Game::Runtime.capability?`または`supported?`で明示する。
- `Game::Display`の描画はRGB565、スプライトの透明キー色は`0xF81F`、画面は320×240。
- WASMのビルドやランタイムを確認するときは`file://`を使わず、HTTPサーバー経由で
  `http://localhost:8080/emulator/`を開く。
- PicoRuby本体は別 checkout で管理する。`PICORUBY_ROOT`を明示できるようにし、そこに
  ある既存の未コミット変更を削除・復元しない。
- `build/`、`emulator/picoruby/`、`local-games/`、`deps/`の生成・依存ファイルを
  まとめてコミットしない。変更対象を確認して必要なソースだけをステージする。
- ゲームリポジトリの作成や、現在のリポジトリ以外へのpushは、ユーザーが明示的に依頼した
  場合だけ行う。

## 完了報告

最後の報告には、変更概要、実行した検証、未実施の実機確認や環境依存事項、生成した
UF2/WASMの場所を短く記載する。ビルドを実行できなかった場合は、失敗したコマンドと
不足している環境だけを明示し、成功した検証と混同しない。
