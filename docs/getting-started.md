# セットアップと初回起動

コマンドはリポジトリのルートで実行します。実機とブラウザで同じ`Game` APIを使います。

初めて環境を用意する場合は、[日本語README](../README.ja.md#イチからセットアップする)
または[英語README](../README.md#set-up-from-scratch)から始めてください。
PC側ツールの導入、リポジトリの取得、Emscriptenの準備から説明しています。
以下はそれらを準備した後の詳細手順です。

## 必要なもの

- Ruby 3.1以上、Rake、PC用のC/C++コンパイラ、Make、CMake、Python 3
- Git（依存関係の取得用）
- Pico SDK 2.3.1
- Arm GNU Toolchain（`arm-none-eabi`）
- Emscripten SDK（ブラウザ版もビルドする場合）
- 実機で確認する場合は、FAT32のmicroSDカード

ブラウザだけで試す場合はArm GNU Toolchainと実機は不要です。Emscriptenの環境を
読み込んだ後、`ruby tools/build/workflow.rb studio`でエミュレータとStudio用コンパイラを
まとめて生成できます。`engine --wasm-only`はエミュレータだけを生成し、Studio用と
既定のPC用ROMコンパイラは生成しません。

## 依存関係の準備

PicoRuby 4.0.5、Funicular 0.5.2、Pico SDK 2.3.1とSD用FatFsを、固定したコミットから取得します。
`deps/`はGit管理対象外です。

```sh
cd /path/to/picoruby_gaming
ruby tools/build/workflow.rb setup
ruby tools/build/workflow.rb doctor
```

ビルドは既定で`deps/picoruby`を使います。別のPicoRuby checkoutを使う場合は
`PICORUBY_ROOT`または`--picoruby-root`で指定できます。準備コマンドは外部checkoutを更新しません。
版と更新手順は[依存バージョン](dependencies.md)を参照してください。

## エンジンをビルドする

```sh
cd /path/to/picoruby_gaming
export ARM_TOOLCHAIN=/path/to/arm-none-eabi/bin
source /path/to/emsdk/emsdk_env.sh
export EM_NODE_JS="$EMSDK_NODE"

ruby tools/build/workflow.rb engine
```

オプションなしの`engine`は、2Dゲームの表示性能を優先した現行のrelease設定
（SRAM framebuffer、dirty rect、background layer、TFT 75MHz、Core 1 presenter、
速度最適化）でビルドします。このコマンドで、次の2つを生成します。

- 実機用UF2：`build/firmware-release/pico_ruby_gaming.uf2`
- ブラウザ用ランタイム：`emulator/picoruby/`

WeAct RP2350B Core Board向けのUF2も、同じコマンドでボード名を指定して生成できます。
このボードにはPico SDK 2.3.1の標準ボード定義を使い、8MB PSRAM（CS1はGP0）を有効にします。
UF2はPimoroni向けビルドと混ざらないよう、別ディレクトリに出力されます。

```sh
ruby tools/build/workflow.rb engine --board weact_studio_rp2350b_core
```

生成先は`build/firmware-release-weact-studio-rp2350b-core/pico_ruby_gaming.uf2`です。
WASMランタイムも同時に更新されます。Pico Plus 2向けの標準ビルドは従来どおり
`build/firmware-release/`へ出力されます。

標準releaseの詳細と比較用オプションは[性能メモ](performance-notes.md)を参照してください。
実機だけ・ブラウザだけのビルドは、それぞれ`engine --firmware-only`・`engine --wasm-only`です。
エンジンや共通APIを変更した場合は、両方をビルドします。

## ブラウザでサンプルを動かす

WASMはHTTPで配信します。`file://`では起動しません。

```sh
python3 -m http.server 8080 --bind 127.0.0.1
```

最初は、ローカルゲームを用意せずに実行できるページを開きます。

- [2Dサンプル](http://localhost:8080/emulator/2d-example.html)：入力・図形・画像・音声など
- [3Dサンプル](http://localhost:8080/emulator/3d-example.html)：ワイヤーフレーム飛行ゲーム
- [API契約テスト](http://localhost:8080/emulator/contract.html)：緑画面なら合格

矢印キーは十字キー、`Z`・`Enter`・`Space`はA、`X`はBです。音声は最初にキーまたは
画面ボタンを押してから確認します。サンプルの操作とROM生成は
[サンプル一覧](../examples/README.md)を参照してください。

自作ゲームは`local-games/`に置き、[通常エミュレータ](http://localhost:8080/emulator/)で
選択します。ゲームとアセットの検出方法は[エミュレータの説明](../emulator/README.md)にあります。

## 実機をまとめて確認する

エンジンのビルド後、`ruby tools/build/workflow.rb checks`で統合テストROMを生成できます。
1つのROMから共通API・2D・3D・入力・音声・保存を選べます。
操作と確認結果の読み方は[統合テストROM](compatibility-testing.md#実機の統合テストrom)を参照してください。

## 次に進む

- ブラウザで編集・実行・ZIP出力：[Browser Studio](../studio/README.md)
- RubyファイルからROMを作成：[ゲーム作成](game-development.md)
- 配線・UF2書き込み・SDカード：[実機ガイド](hardware.md)
- エンジンの変更と検証：[開発ガイド](agent-development.md)
