# ゲームの作成とROM生成

ゲームからは`Game` APIだけを使います。同じRubyコードをRP2350とWASMで実行できます。
APIの引数・戻り値・形式は[APIリファレンス](api-reference.md)を参照してください。

シーンを切り替えるときは、使い終わった`Texture`、`Sprite`、`Font`、`Audio::Sound`への参照を
外します。RubyのGCが実データも回収します。`Game::Assets.collect`でまとめて回収できるため、
同時に保持する量を抑えれば、SD上の総アセット量がPSRAM容量を超えるゲームも作れます。
背景を登録している場合は`Game::Display.set_background(nil)`で解除します。

## ゲームディレクトリ

自作ゲームはGit管理対象外の`local-games/`に置きます。公開・共有するゲームは別の
リポジトリで管理し、このエンジンには追加しません。

```text
local-games/my-game/
├── main.rb
├── assets/             # 実行時に読み込む素材（必要な場合）
├── source-assets/      # 変換前の元データ（必要な場合）
└── prepare_assets.rb   # ゲーム固有の素材生成（必要な場合）
```

最小の`main.rb`は次のとおりです。追加のアセットは必要ありません。

```ruby
require 'game'

Game.init
loop do
  Game::Display.clear(0x0000)
  Game::Display.draw_text(8, 8, 'HELLO', 0xffff)
  Game::Display.present
  Game.tick(30)
end
```

[セットアップ](getting-started.md)後、HTTPサーバー経由で
[通常エミュレータ](http://localhost:8080/emulator/)を開くと、`main.rb`を持つゲームが
一覧に表示されます。ブラウザはソースとアセットを直接読み込みます。

ブラウザだけで編集したい場合は[Browser Studio](../studio/README.md)を使えます。
動作例は[2D・3Dサンプル](../examples/README.md)を参照してください。

## ROMを生成する

ゲームディレクトリに`main.rb`がある場合は、次のコマンドで`game.mrb`を作れます。

```sh
cd /path/to/picorubitto
ruby tools/build/workflow.rb game path/to/game
```

ホストコンパイラは`workflow.rb engine`で生成する`build/mruby/mrbc/default/bin/mrbc`です。
PicoRuby 4.0.5は`RITE0400`形式を使います。以前の版の`game.mrb`も元コードから再生成し、
対応するUF2と一緒に更新します。コンパイラを指定する場合は`--mrbc PATH`を使います。

出力先は`build/games/<ゲームディレクトリ名>/`です。出力ディレクトリの内容をSDカード
のルートへコピーします。

```text
game.mrb
assets/       # ゲームにアセットがある場合
```

PNGやTTF/OTFを使う場合は、実機用形式へ変換してからROMを作ります。

```sh
ruby tools/build/workflow.rb game path/to/game --convert-assets
```

この変換にはRubyの`chunky_png`とFreeType開発ツールが必要です。PNGはビッグエンディアン
RGB565の`.raw`、TTF/OTFはビットマップフォントの`.pbf`になります。`--font-sizes`
でフォントサイズを指定できます。

ゲームに`prepare_assets.rb`がある場合は、アセット準備も実行できます。2D exampleの
元画像を更新した場合は、次のように実行します。

```sh
ruby tools/build/workflow.rb game examples/games/2d --prepare-assets
```

ゲームのRubyコードやアセットだけを変更した場合、エンジンの再ビルドは不要です。

UF2はゲームごとには作りません。初回のファームウェア書き込みとSDカードからの起動は
[実機ガイド](hardware.md)を参照してください。
