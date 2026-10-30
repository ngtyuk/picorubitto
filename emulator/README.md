# ブラウザエミュレータ

ブラウザエミュレータは、RP2350向けのRubyゲームをPicoRuby WASMで実行します。
画面とゲーム選択はFunicularのRubyコンポーネントで管理します。
TFTをCanvas、ボタンをキーボードと画面ボタン、SDカードをHTTP配信のアセットに置き換えます。
画像・フォント・音声のハンドルはRuby GCと連動して回収します。元のアセットはブラウザの仮想SD
として事前ロードするため、`Game::Assets.resident_bytes`はブラウザ全体のメモリ使用量とは異なります。

## セットアップ

プロジェクトルートで次を実行します。

```sh
source /path/to/emsdk/emsdk_env.sh
export EM_NODE_JS="$EMSDK_NODE"
ruby tools/build/workflow.rb engine --wasm-only
```

生成されたWASMランタイムは`emulator/picoruby/`へコピーされます。

## 起動

プロジェクトルートでHTTPサーバーを起動します。

```sh
python3 -m http.server 8080 --bind 127.0.0.1
```

ブラウザで次を開きます。サンプルと契約テストは`local-games/`がなくても実行できます。

| ページ | 用途 |
| --- | --- |
| [2Dサンプル](http://localhost:8080/emulator/2d-example.html) | 入力・図形・画像・音声など |
| [3Dサンプル](http://localhost:8080/emulator/3d-example.html) | ワイヤーフレーム飛行ゲーム |
| [API契約テスト](http://localhost:8080/emulator/contract.html) | 実機とWASMの共通API確認 |
| [通常エミュレータ](http://localhost:8080/emulator/) | `local-games/`から選ぶ自作ゲーム |
| [Browser Studio](http://localhost:8080/studio/) | ブラウザ内で編集・実行（[別途準備](../studio/README.md)） |

`file://`では起動しません。サンプルの操作・内容・ROM生成は
[サンプル一覧](../examples/README.md)を参照してください。

## local-gamesの自動読み込み

通常ゲーム画面（`index.html`）は、HTTPで`../local-games/`のディレクトリ一覧を取得し、
`main.rb`を持つディレクトリをゲームとして検出します。ゲーム選択後は、次のURLで起動します。

```text
http://localhost:8080/emulator/?game=ゲームディレクトリ名
```

ゲームごとの`assets/`にある`.raw`、`.pbf`、`.wav`を自動的に読み込みます。ゲームに
アセットディレクトリがない場合は、ソースの参照ファイル名を調べ、必要なファイルを持つ
隣のゲームの`assets/`を探します。アセットを共有するローカルゲームも追加設定なしで起動できます。

この検出はHTTPサーバーのディレクトリ一覧に依存するため、上記のPythonサーバーを
使ってください。新しいローカルゲームは、ディレクトリに`main.rb`を置くだけで一覧へ
追加されます。作成方法は[ゲーム作成](../docs/game-development.md)を参照してください。

セーブ・衝突・時間・性能などのローカル検証ゲームも同じセレクタか`?game=名前`で
起動します。個別のHTMLや固定アセット一覧は不要です。

## 操作

- 矢印キー／画面の十字キー：方向入力
- `Z`、`Enter`、`Space`／Aボタン：A入力
- `X`／Bボタン：B入力
- `Q` / `E`／L・Rボタン：L / R入力
- `P` / `O`／START・SELECTボタン：START / SELECT入力

ブラウザの音声はユーザー操作後に有効になります。
実行・読込エラーは画面に短く表示し、詳細はブラウザコンソールへ出力します。
「もう一度」でページを再読み込みできます。

## API契約テスト

`contract.html`は`tests/portable/game_api_contract.rb`を実行します。緑画面が合格、赤画面が
失敗です。WASMランタイムや共通APIを変更したときは、ページを再読み込みして確認します。

実機側の同じテストは[`../docs/compatibility-testing.md`](../docs/compatibility-testing.md)を
参照してください。

## 実機との違い

- 描画先はCanvasです。TFTのSPI転送、DMA、PSRAMの帯域は再現しません。
- `Game::Assets.read`はブラウザ側のアセット値を返します。
- セーブデータはブラウザの永続ストレージに保存します。
- 音声はWeb Audio APIを使います。
- フレームレートはブラウザのスケジューラに左右されます。

APIの共通仕様は[`../docs/api-reference.md`](../docs/api-reference.md)、実装の分担は
[`../docs/runtime-architecture.md`](../docs/runtime-architecture.md)を参照してください。

画面とブラウザ連携の配置は[Webフロントエンド](../docs/web-frontend.md)を参照してください。

音量は操作パネル下の「音量ダイヤル」で調節できます。ADC相当値0〜4095と全体音量0〜255を
`Game::Audio.dial_value`と`master_volume`で読み取れます。検証ROMの`AUDIO / DIAL`は
このスライダーを実機のポテンショメータと同じAPIで確認します。
