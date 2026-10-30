# PicoRubitto

[English](README.md) | [日本語](README.ja.md)

[PicoRuby](https://github.com/picoruby/picoruby)で書いたゲームを、RP2350実機と
ブラウザのWebAssemblyで動かすゲームプラットフォームです。同じRubyコードから
共通の`Game` APIを使います。

ゲームエンジン、ファームウェア、ブラウザエミュレータに加え、Ruby・ドット絵の編集と
microSD用ゲーム出力ができるBrowser Studioを含みます。PicoRuby本体は別の依存checkout
からビルドします。

## PicoRubitto Studioを試す

[PicoRubitto Studioを開く](https://picorubitto.ngt-y.uk/)と、ブラウザでゲームを作って実行できます。

- 320×240・RGB565の画面、スプライト、テクスチャ、ビットマップフォント
- 十字キー、A/B、L/R、START/SELECT
- 深度付き低ポリゴン3D、WAV再生（最大4音）、セーブ、矩形衝突判定
- 対応ボード：Pimoroni Pico Plus 2、WeAct RP2350B Core Board（8MB PSRAM）

## イチからセットアップする

以下はmacOS／Linuxのシェル向けの手順です。プロジェクトのコマンドはリポジトリの
ルートで実行します。ツールや依存関係の取得にはインターネット接続が必要です。
ブラウザで試すだけなら、ボード・配線・microSDカードは不要です。

### 1. PC側のツールを入れる

Git、**Ruby 3.1以上**、Rake、PC用のC/C++コンパイラ、Make、CMake、Python 3が必要です。
Pythonはブラウザ用HTTPサーバー、Rubyはビルドツールの実行に使います。

macOSではXcode Command Line Toolsを入れ、[Homebrew](https://brew.sh/)を使います。
Homebrewがなければ先に導入してください。

```sh
xcode-select --install
```

インストール完了後に次を実行します。Command Line Tools導入済みなら上の操作は不要です。

```sh
brew install git ruby@3.4 cmake python
export PATH="$(brew --prefix ruby@3.4)/bin:$PATH"
gem install rake
```

Ubuntu 24.04では次を導入の出発点にできます。

```sh
sudo apt-get update
sudo apt-get install git ruby-full rake build-essential cmake python3
```

続ける前に、使用するツールを確認します。

```sh
ruby --version
ruby -S rake --version
cc --version
c++ --version
make --version
cmake --version
python3 --version
```

Rubyが3.1未満なら、バージョン管理ツールで切り替えるか、
[Rubyのインストールガイド](https://www.ruby-lang.org/en/documentation/installation/)を
参照してください。macOS標準のRubyは古い場合があります。Homebrew用の上のPATH設定は
現在のターミナルだけに有効です。継続利用する場合はシェルの設定にも追加します。

### 2. リポジトリと固定した依存関係を取得する

```sh
git clone https://github.com/ngtyuk/picorubitto.git
cd picorubitto
ruby tools/build/workflow.rb setup
ruby tools/build/workflow.rb doctor
```

`setup`はPicoRuby **4.0.5**、そのsubmoduleのFunicular **0.5.2**、Pico SDK **2.3.1**、
固定したFatFs SDポートを`deps/`へ取得します。必要なsubmoduleも初期化するため、
PicoRubyを別途cloneする必要はありません。

`doctor`は依存ソースのコミットを表示します。PicoRuby・Pico SDK・FatFsが`locked`に
なっていることを確認してください。コンパイラやPC側ツール全体の検査ではありません。
版とコミットは[`tools/build/dependencies.lock.json`](tools/build/dependencies.lock.json)に
記録しています。

ビルドは既定で`deps/picoruby`を使います。シェルに`PICORUBY_ROOT`が設定済みの場合は、
そちらが優先されます。既存checkoutを意図的に使う場合は
`export PICORUBY_ROOT=/absolute/path/to/picoruby`で指定できます。
`setup`はその外部checkoutを更新しません。詳細は[依存バージョン](docs/dependencies.md)を
参照してください。

### 3. ブラウザ用のEmscriptenを入れる

[Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)はリポジトリ外に
置きます。以下では現在の開発環境にある**6.0.8**を指定します。emsdkは依存lockの管理対象外です。
導入済みの場合は再cloneせず、既存のemsdkディレクトリを使ってください。

```sh
git clone https://github.com/emscripten-core/emsdk.git "$HOME/emsdk"
"$HOME/emsdk/emsdk" install 6.0.8
"$HOME/emsdk/emsdk" activate 6.0.8
source "$HOME/emsdk/emsdk_env.sh"
export EM_NODE_JS="$EMSDK_NODE"
emcc --version
```

WASMをビルドする新しいターミナルでは、`source`と`EM_NODE_JS`の設定を再実行します。
以降も同じターミナルで作業してください。

### 4. エンジンをビルドする

用途に合わせて、次のどちらかを選びます。

**ブラウザだけで試す場合：** Studioをビルドすると、エミュレータ用ランタイムも一緒に
生成します。この経路にはArmクロスコンパイラは不要です。

```sh
ruby tools/build/workflow.rb studio
```

`emulator/picoruby/`と、ブラウザ内コンパイラの`studio/runtime/`を生成します。
エミュレータだけなら`ruby tools/build/workflow.rb engine --wasm-only`を使えます。
そのコマンドではStudio用コンパイラと既定のPC用ROMコンパイラは生成しません。

**実機とブラウザを使う場合：**
[Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)から、
PCのOS・CPUに合う、ターゲットが**`arm-none-eabi`**（AArch32 bare-metal）のパッケージを
導入します。現在の開発環境は**15.2.Rel1**です。`ARM_TOOLCHAIN`はコンパイラ本体ではなく
`bin`ディレクトリを指定します。以下のパスを導入先に置き換えてください。

```sh
export ARM_TOOLCHAIN=/absolute/path/to/arm-none-eabi/bin
"$ARM_TOOLCHAIN/arm-none-eabi-gcc" --version
ruby tools/build/workflow.rb engine
```

既定ボードはPimoroni Pico Plus 2です。次を生成します。

| 成果物 | 場所 |
| --- | --- |
| ファームウェア | `build/firmware-release/pico_ruby_gaming.uf2` |
| ELF / map | `build/firmware-release/pico_ruby_gaming.elf` / `.map` |
| PC用ROMコンパイラ | `build/mruby/mrbc/default/bin/mrbc` |
| ブラウザ用ランタイム | `emulator/picoruby/` |

WeAct RP2350B Core Board（8MB PSRAM）では次を使います。

```sh
ruby tools/build/workflow.rb engine --board weact_studio_rp2350b_core
```

UF2は`build/firmware-release-weact-studio-rp2350b-core/pico_ruby_gaming.uf2`です。
どちらの実機ビルドでも、Studioを使う場合は別途
`ruby tools/build/workflow.rb studio`を実行してください。

### 5. ブラウザでサンプルを動かす

リポジトリのルートでサーバーを起動し、そのまま動かしておきます。

```sh
python3 -m http.server 8080 --bind 127.0.0.1
```

| ブラウザで開くページ | 確認すること |
| --- | --- |
| [2Dサンプル](http://localhost:8080/emulator/2d-example.html) | 図形・入力・スプライト・音声 |
| [3Dサンプル](http://localhost:8080/emulator/3d-example.html) | ワイヤーフレーム飛行ゲーム |
| [API契約テスト](http://localhost:8080/emulator/contract.html) | 緑画面なら合格、赤画面なら失敗 |
| [Browser Studio](http://localhost:8080/studio/) | Ruby・ドット絵エディタ。Studioのビルドが必要 |

サンプルは`local-games/`や別のゲームリポジトリなしで動きます。必ずHTTPで開き、
`file://`は使わないでください。現行のChrome・Edge・Firefoxを使います。
矢印キーで移動、`Z`がA、`X`がB、`P`がSTART、`O`がSELECTです。
音声はキーまたは画面ボタンを押してから有効になります。
ゲームごとの操作は[サンプル一覧](examples/README.md)を参照してください。

### 6. 実機でサンプルを動かす

対応ボード、配線済みのST7789Vディスプレイ、microSDインターフェース、ボタン、
音声を使う場合は音声回路が必要です。ボード単体にはこれらの周辺機器が揃っていないため、
先に[実機ガイド](docs/hardware.md)に従って配線します。

実機ビルド後、対応するコンパイラでサンプルROMを生成します。

```sh
ruby tools/build/workflow.rb game examples/games/2d
```

1. ボードをBOOTSELモードでUSB接続し、ボードに合うUF2を表示されたドライブへコピーします。
2. `build/games/2d/`の**中身**を、FAT32のmicroSDカードのルートへコピーします。
3. カードを挿入してボードを再起動します。

カードのルート直下に`game.mrb`と`assets/`を置きます。3D版は
`ruby tools/build/workflow.rb game examples/games/3d`で生成し、代わりに
`build/games/3d/`の中身をコピーします。Studioからコンパイル済みゲームと素材を含む
SD用ZIPを出力する方法もあります。UF2は初回に書き込み、ゲームの入れ替えではROMと素材を
更新します。

## 困ったとき

| 症状 | 確認すること |
| --- | --- |
| Rubyの版エラー／Rakeがない | `ruby --version`と`ruby -S rake --version`を確認し、選択したRubyでRakeを導入します。 |
| `doctor`が`not installed`／固定版と不一致 | `setup`を実行します。依存checkoutに変更があると停止するので、変更を保持したまま別途整理してください。 |
| Emscriptenが見つからない | ビルドするターミナルで`emsdk_env.sh`を読み込み、`EMSDK`・`EMSDK_NODE`・`EM_NODE_JS`を確認します。 |
| Arm toolchainが見つからない | `arm-none-eabi-gcc`・`arm-none-eabi-ld`・`arm-none-eabi-ar`があるディレクトリを`ARM_TOOLCHAIN`に指定します。 |
| ブラウザのランタイム／Studioコンパイラが404 | 対応するランタイムをビルドし、リポジトリのルートからHTTPサーバーを起動します。 |
| エミュレータのゲーム一覧が空 | まず同梱サンプルを開いてください。通常の一覧は手元の`local-games/`にあるゲームを表示します。 |
| 実機が起動しない | ボードに合うUF2、配線、FAT32、カード直下のROM・素材、USBシリアルの起動ログを確認します。 |

## ドキュメントと開発

以下の詳細ガイドは現在、日本語で提供しています。

| やりたいこと | 文書 |
| --- | --- |
| セットアップの詳細 | [セットアップ](docs/getting-started.md) |
| ゲームを書いてROMにする | [ゲーム作成](docs/game-development.md) |
| ブラウザで編集する | [Browser Studio](studio/README.md) |
| 配線・UF2書き込み | [実機ガイド](docs/hardware.md) |
| 実機の動作確認 | [統合テストROM](docs/compatibility-testing.md#実機の統合テストrom) |
| APIを調べる | [APIリファレンス](docs/api-reference.md) |
| エンジンを変更する | [開発ガイド](docs/agent-development.md) |
| 構成・互換性・性能 | [文書一覧](docs/README.md) |

`src/`・`include/`・`mrbgems/`が実機バックエンドとgem、`emulator/`がWASMアダプタと
エミュレータ、`studio/`・`web/`がブラウザ用ツールです。テストは`tests/`、ビルドツールは
`tools/`に置きます。

エンジン・共通APIの変更では**UF2とWASMの両方**をビルドし、共通契約テストと対応する
文書更新も行います。開発手順の詳細は[AGENTS.md](AGENTS.md)を参照してください。

`build/`・`deps/`・`emulator/picoruby/`・`studio/runtime/`の生成物・依存物はコミットしません。
自作ゲームはGit管理対象外の`local-games/`か別リポジトリで管理します。
`examples/`は共有するAPIサンプルです。

組み込みの英数字・かな・漢字にはマルミーニャ1.500を使用しています。詳細は[ライセンス](tools/fonts/MARUMINYA-OFL.txt)を参照してください。

## ライセンス

このプロジェクトは[MITライセンス](LICENSE)で公開します。同梱する第三者製コンポーネントには、
[マルミーニャ](tools/fonts/MARUMINYA-OFL.txt)や[Prism](studio/vendor/prism/LICENSE)など、
それぞれのライセンスが適用されます。
