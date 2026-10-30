# 開発ガイド

このリポジトリの変更は、影響する実行先に合わせてビルドと検証を行います。

依存関係は`ruby tools/build/workflow.rb setup`で準備します。固定する版・コミットと更新時の
確認は[依存バージョン](dependencies.md)を参照してください。

## 変更と必要な確認

| 変更 | ビルド・確認 |
| --- | --- |
| `src/`、`include/`、`mrbgems/`、`CMakeLists.txt`、`mruby_config.rb`、共通API | `ruby tools/build/workflow.rb engine`、契約テスト、可能なら実機 |
| `emulator/game_adapter.rb`、WASM設定 | `ruby tools/build/workflow.rb engine`、WASM契約テスト、ブラウザ |
| `emulator/emulator.js`、HTML、CSSだけ | ブラウザ。APIを変えたら実機も含める |
| `web/`、`studio/`、`emulator/shell.rb`、Studio用ビルド設定 | `ruby tools/build/workflow.rb studio`、Studioの構文チェック・実行・ZIP出力 |
| ゲームのRubyコードやアセット | `ruby tools/build/workflow.rb game GAME_DIR`、ブラウザまたは実機 |

エンジンや`Game` APIを変更したときは、UF2とWASMの両方を生成します。

実機の対象ボードは`engine --board`で指定します。既定値は
`pimoroni_pico_plus2_rp2350`です。WeAct RP2350B Core Board（8MB PSRAM）用のUF2とWASMは
次で生成します。

```sh
ruby tools/build/workflow.rb engine --board weact_studio_rp2350b_core
```

ボードごとのUF2出力先を分けるため、Pimoroni向けの既定生成物を上書きしません。

WeActでI2SのDINをGP28、音量ダイヤルをGP42、出力上限を50%にする構成は、
[実機ガイドのビルド例](hardware.md#ダイヤル音量adc)を参照してください。ダイヤル併用時、
`--i2s-data 28`を省略するとDINはGP17になります。

## 主なエンジンビルドオプション

`ruby tools/build/workflow.rb engine --help`で全項目を確認できます。

| 項目 | 指定 |
| --- | --- |
| 対象ボード | `--board BOARD`。既定はPico Plus 2 |
| 音声出力 | `--audio-output pwm\|i2s`、`--volume-knob`、`--audio-max-gain 0..100` |
| I2S配線 | `--i2s-bclk GPIO`、`--i2s-data GPIO`。組み合わせは[実機ガイド](hardware.md)参照 |
| VM | `--vm-opt size\|speed`。既定はsize |
| ビルド対象 | `--firmware-only`、`--wasm-only`、`--no-emulator-runtime` |
| パス | `--picoruby-root`、`--build-root`、`--firmware-dir`、`--arm-toolchain` |
| 調査 | `--debug`、`--profiling` |
| 表示調整 | `--tft-clock`、`--tft-stripe-rows`、`--dirty-merge-extra` |

SRAMフレームバッファ、背景、差分転送、Core 1 presenterと速度重視Cコンパイル、
3D最適化、I2SのDMAチェーンと32-bit補間は標準構成です。旧方式との比較コードは
ホスト回帰テストとGit履歴で利用します。

## エンジンを変更したとき

1. プロジェクトルートで環境を設定します。

   ```sh
   export ARM_TOOLCHAIN=/path/to/arm-none-eabi/bin
   source /path/to/emsdk/emsdk_env.sh
   export EM_NODE_JS="$EMSDK_NODE"
   ```

2. 次のコマンドを実行します。

   ```sh
   ruby tools/build/workflow.rb engine
   ```

3. `ruby tools/build/workflow.rb checks`で実機向けの統合ROMを生成します。
   操作とログは[統合テストROM](compatibility-testing.md#実機の統合テストrom)を参照してください。
   通常の性能確認は最適化済みUF2と標準ROMの保持モデル3Dを使います。遅い従来方式の
   性能測定は、必要な比較実験だけ`checks --compare-3d`の別ROMで実行します。

4. `http://localhost:8080/emulator/contract.html`を開き、緑画面を確認します。HTTP
   サーバーはプロジェクトルートで次のように起動できます。

   ```sh
   python3 -m http.server 8080 --bind 127.0.0.1
   ```

5. 実機に関係する変更は、UF2を書き込み、契約ROMまたは対象ゲームをSDカードから
   起動します。USB CDCシリアルに起動エラーがないことも確認します。

短時間の切り分けには`ruby tools/build/workflow.rb engine --firmware-only`または
`ruby tools/build/workflow.rb engine --wasm-only`を使えますが、
API変更の完了確認には使いません。

## VM最適化の比較をするとき

描画設定を固定し、RP2350のmrubyコアだけを比較します。標準releaseはsizeのままです。

```sh
ruby tools/build/workflow.rb engine --vm-opt size
ruby tools/build/workflow.rb engine --vm-opt speed
ruby tools/build/workflow.rb engine --vm-opt size
ruby tools/build/workflow.rb checks
ruby tests/native/vm_profile_test.rb
ruby tests/native/performance_test.rb
python3 tests/native/minimum_profile_regression.py
```

speedのUF2は`build/firmware-release-vm-speed/pico_ruby_gaming.uf2`、クロスライブラリは
`build/mruby-vm-speed/pico-gaming/`です。sizeは通常の出力先です。`--build-root`も使えます。
同じ`--firmware-dir`を指定して切り替えた場合も、リンク先のライブラリと起動ログを確認します。
`gaming-build.json`の設定差でクロスターゲットを作り直すため、別構成のオブジェクトを流用しません。
実際のコンパイル引数は各オブジェクト隣の`.o.flags`で確認できます。
WASM・ホストの最適化は比較要因に含めず、共通契約を確認します。`--wasm-only --vm-opt speed`は拒否します。
手順とログの範囲は[性能の計測](performance-notes.md#計測する)を参照してください。

## ゲームを変更したとき

```sh
ruby tools/build/workflow.rb game path/to/game
```

アセットの元データを再生成するゲームでは、必要に応じて次を使います。

```sh
ruby tools/build/workflow.rb game path/to/game --prepare-assets --convert-assets
```

出力された`game.mrb`と`assets/`をSDカードのルートへコピーします。ブラウザ側は対象の
HTMLを再読み込みします。ゲームだけの変更ではUF2やWASMの再ビルドは不要です。

## Browser Studio・Web画面を変更したとき

画面はFunicularのRubyコンポーネントで管理します。配置、ブリッジとの境界、起動順は
[Webフロントエンド](web-frontend.md)を参照してください。ビルド・構文チェック・素材一覧は
`studio/studio.rb`、ドット絵の操作画面は`studio/pixel-editor.rb`、ゲーム画面は
`emulator/shell.rb`と`studio/runner.rb`に置きます。

Web画面の共通配色・文字・フォーカス表示は`emulator/theme.css`、画面ごとの配置は
`emulator/emulator.css`と`studio/studio.css`で管理します。温かい明色とドットの枠を使い、
文言は操作と用途を短く表し、機能の説明はできることに絞ります。
広い画面とスマートフォン幅で表示・入力を確認します。

Rubyの色分けは[Prism 1.30.0](https://github.com/PrismJS/prism/tree/v1.30.0)の
core・C-like・Rubyを`studio/vendor/prism/`へ同梱しています（MIT、同ディレクトリの
`LICENSE`を保持）。`studio/studio-bridge.js`が編集用textareaと表示用preの内容・縦横スクロールを
同期します。編集・選択・Undo、長い行・末尾改行、サイズ変更、エラー行への移動でも
色分けとカーソルが揃うことを確認します。

ドット絵の画素・履歴・スプライトシート変換は`studio/pixel-art-model.js`、
Canvas描画は`studio/pixel-editor.js`、配置は`studio/pixel-editor.css`で管理します。
`studio/studio-bridge.js`が下書き・素材の編集データをIndexedDBへ保存し、ゲームには既存形式のRAWだけを
渡します。透明画素は`0xf81f`、不透明色が同じ値に量子化された場合は`0xf81e`へ寄せます。

```sh
ruby tests/web/components_test.rb
node --test tests/web/pixel-art-model.test.js
```

ブラウザでは描画・塗りつぶし・Undo/Redo、コマの複製・並べ替え・再生、PNGの保存と
再読み込み、素材の追加・更新、ページ再読み込み後の復元を確認します。複数コマをゲームで
表示し、ZIP内のRAWでも横並びのコマ順・透明色・ビッグエンディアンRGB565が保たれることを
確認します。長方形の画像とスマートフォン幅の表示も対象にします。

Studio用のコンパイラと通常のWASMランタイムをまとめて生成します。

```sh
source /path/to/emsdk/emsdk_env.sh
export EM_NODE_JS="$EMSDK_NODE"
ruby tools/build/workflow.rb studio
python3 -m http.server 8080 --bind 127.0.0.1
```

`http://localhost:8080/studio/`で、構文エラーの行・列表示、`game.mrb`のビルド、エミュレータ
実行、素材読み込み、IndexedDBからの復元、SDカード用ZIPの生成を確認します。Studioのコンパイラ
とランタイムは生成物なので、`studio/runtime/`や`emulator/picoruby/`をコミットしません。

## `Game` APIを変更したとき

次の4つを同じ変更に含めます。

1. RP2350バックエンド（C）
2. WASMバックエンド（RubyアダプタまたはJavaScript）
3. `tests/portable/game_api_contract.rb`
4. [`api-reference.md`](api-reference.md)と必要な設計文書

実機だけ、またはWASMだけで使える機能は、`Game::Runtime.capability?`で問い合わせられる
ようにします。両方で同じゲームコードを動かすため、ゲームからターゲット固有のC関数や
JavaScriptを呼び出さないでください。

後方互換性を壊す変更では`Game::API_VERSION`も更新します。

## 変更後の確認

```sh
git status --short
git diff --check
node --check emulator/emulator.js
```

Rubyファイルを変更した場合は、対象ファイルごとに次も実行します。

```sh
ruby -c path/to/file.rb
```

ビルド生成物、依存物、生成されたWASM、ゲームROMをソース変更と一緒にコミットしないで
ください。生成物の扱いとGit操作の詳細は[`AGENTS.md`](../AGENTS.md)を参照します。
