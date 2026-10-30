# 依存バージョンと更新

`tools/build/dependencies.lock.json`でバージョンとコミットを固定します。

| 依存 | 固定版 | コミット |
| --- | --- | --- |
| [PicoRuby](https://github.com/picoruby/picoruby/releases/tag/4.0.5) | 4.0.5 | `604666ec366bd9c597756e3d671bfa4c378f165d` |
| [Funicular](https://github.com/picoruby/funicular/tree/fe7f88700a201e80269a3e54221770b9ca0025f4) | 0.5.2（PicoRubyのsubmodule） | `fe7f88700a201e80269a3e54221770b9ca0025f4` |
| [Pico SDK](https://github.com/raspberrypi/pico-sdk/releases/tag/2.3.1) | 2.3.1 | `079c6f39023649b154152db30f1d781e884879bc` |
| [SD用FatFsポート](https://github.com/picoruby/picoruby/tree/2eb44257faa86980ee12f40e77573779310433b9/mrbgems/picoruby-filesystem-fat) | R0.14b、削除前の最終版 | `2eb44257faa86980ee12f40e77573779310433b9` |
| [Prism](https://github.com/PrismJS/prism/releases/tag/v1.30.0) | 1.30.0、MITライセンスを含め同梱 | `studio/vendor/prism/` |

PicoRuby 4.xでは従来のFatFs gemが削除されています。既存SDバックエンドのCソースだけを
`deps/picoruby-fatfs`へ独立して取得し、現行PicoRubyと組み合わせます。

## 準備と確認

```sh
ruby tools/build/workflow.rb setup
ruby tools/build/workflow.rb doctor
```

`setup`は固定コミットと必要なsubmoduleを`deps/`へ取得します。既存checkoutのoriginや
未コミット変更を確認し、異なる取得元やローカル変更があれば停止します。外部の
`PICORUBY_ROOT`はビルド入力としてだけ使い、準備コマンドでは変更しません。
`doctor`はインストール済みコミットと固定版を表示します。

## 3.4系からの移行

4.0.5のコンパイラ名は`mrbc`、ROM形式は`RITE0400`です。従来のROMは元のRubyから再生成し、
新しいUF2とセットでSDカードへコピーしてください。ゲームの`Game` APIは共通のままです。

```sh
ruby tools/build/workflow.rb engine
ruby tools/build/workflow.rb studio
ruby tools/build/workflow.rb game path/to/game
```

ホストコンパイラは`build/mruby/mrbc/default/bin/mrbc`、Studio用は
`studio/runtime/mrbc.js`と`mrbc.wasm`です。ROM生成では`--mrbc PATH`で指定できます。
従来の`--picorbc`はコマンド引数の別名として受け付けます。

WASM生成時には`emulator/picoruby/version.json`へPicoRuby・Funicularの版、コミット、
ROM形式を記録します。Studio用コンパイラの生成時にも同じ情報をコピーします。

## 次回の更新

1. 公式リリースとPicoRuby内のsubmoduleを確認し、lockの版・コミット・取得対象を更新する。
2. `setup`でプロジェクト管理の依存checkoutを更新し、`doctor`で確認する。
3. 実機・WASM・Studioの設定を揃え、`engine`と`studio`をビルドする。
4. 契約ROMを再生成し、WASM契約テスト、Studioでのコンパイル・実行・ZIP出力、2D・3Dと
   通常エミュレータを確認する。可能なら両ボードでも起動する。
5. 依存取得ツールと共通UIのテスト、Cバックエンドの回帰テストを実行する。

```sh
ruby tests/native/dependencies_test.rb
ruby tests/web/components_test.rb
node --test tests/web/pixel-art-model.test.js
python3 tests/native/audio_regression.py
python3 tests/native/platform_regression.py
python3 tests/native/texture_regression.py
```

契約ROMとブラウザの確認手順は[互換性テスト](compatibility-testing.md)を参照してください。
依存checkoutと生成物はGitへ追加せず、lock、設定、ソース、仕様文書を更新します。
