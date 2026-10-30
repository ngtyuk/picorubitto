# PicoRubitto Browser Studio

ブラウザだけで`main.rb`を書き、PicoRubyの`game.mrb`へコンパイルして、同じWASMエミュレータで実行できます。画面はFunicular（PicoRuby）のRubyコンポーネントで描画し、コードのコンパイルはブラウザ内のWeb Workerで処理します。

公開版は[PicoRubitto Studio](https://picorubitto.ngt-y.uk/)から利用できます。
画面右上の「English」／「日本語」で表示言語を切り替えられます。初回はブラウザの
言語設定に応じて選択し、切り替えた設定はブラウザに保存します。
画面上部の「ドキュメント」から、目次付きのAPIリファレンスを別のタブで開けます。
APIリファレンスの本文は日本語です。

## 起動

[依存関係を準備](../docs/getting-started.md)してから、エンジンとStudio用コンパイラを生成します。

```sh
cd /path/to/picorubitto
source /path/to/emsdk/emsdk_env.sh
export EM_NODE_JS="$EMSDK_NODE"
ruby tools/build/workflow.rb studio
```

次にリポジトリのルートでHTTPサーバーを起動し、<http://localhost:8080/studio/>を開きます。WASMを使用するため、`file://`で直接開かないでください。

```sh
python3 -m http.server 8080 --bind 127.0.0.1
```

`workflow.rb studio`は通常のブラウザ用WASMランタイムと、Studio内で使う`mrbc-wasm-browser`をまとめて生成します。生成物は`emulator/picoruby/`と`studio/runtime/`に置かれますが、どちらもGit管理対象外です。

Cloudflareへの公開用ファイルはビルド後に生成します。

```sh
git clone https://github.com/ngtyuk/picocraft.git /path/to/picocraft
# Sunny Trail Adventureのゲームディレクトリも別途用意する。
ruby tools/build/package_studio.rb \
  --sample 'picocraft,Picocraft,/path/to/picocraft' \
  --sample 'sunny-trail,Sunny Trail Adventure,/path/to/sunny-trail'
npx wrangler deploy
```

配信対象は`build/studio-site/`内の必要なファイルに限られます。`wrangler.jsonc`は
`picorubitto.ngt-y.uk`と従来の`game-studio.ngt-y.uk`を指定します。公開にはCloudflareへの
ログインとドメインの設定が必要です。ブラウザの保存データはドメインごとに独立するため、
旧ドメインのプロジェクトは新ドメインに自動では移りません。
`--sample`を省くとサンプルは配信されません。ゲームのソースと素材はエンジンとは別に
管理します。Picocraftは[専用リポジトリ](https://github.com/ngtyuk/picocraft)で公開しています。

PicoRuby 4.0.5の`RITE0400`形式でROMを生成します。以前の版で作ったROMは、コードから再ビルドしてください。

## 使い方

1. 左側の`main.rb`にRubyコードを書きます。キーワード・文字列・数値・コメントを色分けして表示します。
   初回は`Hello, world!`を表示するサンプルが入っています。
   公開版では「サンプルゲーム」から「Picocraft」や「Sunny Trail Adventure」を選ぶと、確認後にコードと素材を読み込めます。現在のプロジェクトは置き換わります。
2. 編集を止めると構文チェックが自動実行されます。エラーの行・列をクリックすると該当位置へ移動できます。
3. 「ゲーム」欄の「実行」を押すと、生成された`game.mrb`を320×240エミュレータで実行します。エミュレータへ渡すのはRubyソースではなく、実機用と同じMRBです。
4. 必要なら「書き出す」を押して、ゲームと素材をSDカード用ZIPとして保存します。

プロジェクト名、コード、素材はIndexedDBへ自動保存されます。初版のエントリポイントは`main.rb`のみです。

## 素材

追加できるファイルは次の4種類です。

| 入力 | ZIPに入る形式 | 備考 |
| --- | --- | --- |
| `.png` | `.raw` | ブラウザ内でビッグエンディアンRGB565へ変換。アルファ128未満は透明キー色`0xf81f` |
| `.raw` | `.raw` | 既存のRGB565データをそのまま使用 |
| `.wav` | `.wav` | 既存のWAVをそのまま使用。実行時にエンジン側の形式検証を行う |
| `.pbf` | `.pbf` | ビットマップフォントをそのまま使用 |

TTF/OTFからPBFへの変換はStudio初版では対応していません。ファイル名は英数字、`.`、`_`、`-`のみ使用でき、同名素材は追加できません。

Rubyコードからは、通常のゲームと同じく`Game::Assets.read('image.raw')`などでアセットを読み込みます。APIの詳細は[`../docs/api-reference.md`](../docs/api-reference.md)を参照してください。

## ドット絵とアニメーション

「素材」の「ドット絵」から、透明背景の画像を描けます。

1. 名前と1コマの幅・高さ（各1〜64px）を指定します。
2. 鉛筆、消しゴム、塗りつぶし、色取りで描きます。「戻す」「やり直す」で編集を戻せます。
3. コマを追加・複製・並べ替え、1〜24fpsで再生します。最大32コマで、「前のコマ」を重ねて確認できます。
4. 「素材に追加」でゲーム用の`.raw`へ変換します。追加した素材の「編集」から開き直し、「素材を更新」で反映します。

下書きと素材の編集データはプロジェクトと一緒に自動保存されます。「PNG保存」はコマを
横に並べた透明PNGを出力します。「PNGを開く」では、現在の幅・高さに合う横並び画像を
コマに分割します。読み込み前に1コマのサイズを指定してください。

「Ruby」を開くと、画像のサイズ・コマ数・速度を反映した実行例を表示します。
`Game::Sprite.load('sprite.raw', 幅, 高さ, コマ数)`で読み込み、`sprite.draw(x, y, コマ番号)`で
表示できます。SD用ZIPにも同じ素材が入ります。

## 実機へコピー

UF2はゲームごとには作りません。最初にエンジンのUF2をPicoへ一度書き込み、Studioが作ったZIPを展開して、中身をFAT32のmicroSDカード直下へコピーします。

```text
game.mrb
assets/
  image.raw
  sound.wav
  font.pbf
```

UF2の生成は[セットアップ](../docs/getting-started.md)、初回書き込みと配線は
[実機ガイド](../docs/hardware.md)を参照してください。

## 制約

- 静的サイトとして動作し、サーバー側コンパイル、ログイン、クラウド保存は行いません。
- Chrome、Edge、Firefox系の現行ブラウザを対象にしています。
- ブラウザのIndexedDBを消去すると自動保存したプロジェクトも消去されます。
- 実機で動かす前に、Studioのエミュレータと通常のWASM契約テストで確認してください。

画面の構成や変更手順は[Webフロントエンド](../docs/web-frontend.md)を参照してください。
