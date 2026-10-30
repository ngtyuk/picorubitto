# Webフロントエンド

Web画面は[Funicular](https://github.com/picoruby/funicular)の`Component`とVirtual DOMで
描画します。Studio、ドット絵ツール、通常エミュレータ、2D・3Dサンプル、APIテスト、
Studio内プレビューが対象です。HTMLはCSS・スクリプトの読み込みとマウント先だけを持ちます。

## 配置と責務

| 場所 | 責務 |
| --- | --- |
| `web/components.rb` | 共通コンポーネント、ゲーム操作ボタン |
| `web/start.rb` | ページに応じたFunicularの起動 |
| `web/boot.js` | PicoRuby WASMの初期化、Rubyソースの読み込み順、協調スケジューラ |
| `studio/studio.rb` | 画面、ビルド・構文チェックの制御、診断・素材一覧と操作 |
| `studio/pixel-editor.rb` | 描画ツール・パレット・コマ一覧・使用例の表示と操作 |
| `studio/runner.rb` | プレビューの待機・実行表示、ゲーム操作ボタン |
| `emulator/shell.rb` | ゲーム選択、参照素材の探索方針、サンプル・テスト画面 |
| `studio/studio-bridge.js` | Workerとのコンパイル通信、IndexedDB、PNG変換、ZIP、iframeとの通信 |
| `studio/pixel-editor.js`, `studio/pixel-art-model.js` | Pointer Capture、Canvas、アニメーション、TypedArrayの画素・履歴・RAW/PNG変換 |
| `studio/runner-bridge.js` | 親画面との通信、MRBと素材の受け渡し |
| `emulator/browser-bridge.js` | HTTPのディレクトリ一覧・ソース取得、ゲームの起動 |
| `emulator/emulator.js` | ゲーム実行用のCanvas・音声・入力・保存バックエンド |

Funicular 0.5.2の`tag(:main, ...)`などで要素を作り、状態は`state[:key]`で参照します。
状態の表示にはFunicularの`patch`を使います。ブラウザ側からの通知は`GamingWeb.emit`の
JSONイベントを通し、Ruby側で表示状態へ反映します。Studioのビルド・構文チェック・ZIP
作成の順序はRuby側で管理し、MRBや素材のバイナリはJavaScript側に保持します。

コード編集用textarea、Prismのpre、ゲーム画面・ドット絵用Canvasは安定したDOM要素として
マウントし、文字の選択・スクロール・描画は各ブラウザブリッジが扱います。画面更新で
これらの要素を作り直さないでください。コマ一覧のCanvasはFunicularが生成し、
`component_updated`後にブリッジが画素を描き直します。

## WASMと起動順

ゲーム用WASMのビルド設定には既に`picoruby-funicular`を含めています。同じ
`emulator/picoruby/picoruby.js`と`picoruby.wasm`をUIにも使い、別のフロントエンド用WASMや
CDN依存は追加しません。静的HTTPサーバーで動作します。

`web/boot.js`が生成済みの`emulator/picoruby/version.json`を読み、ブラウザブリッジを順番に
読み込んでからRubyファイルをHTMLの記載順にまとめ、1つのタスクとして起動します。
JavaScript・WASM・コンパイラWorkerのURLへ同じランタイムrevisionを付け、Rubyソースは
キャッシュを使わず取得します。ランタイム更新時の旧版との混在を防ぎます。
Funicularのマウントが終わってからブリッジを初期化し、ゲームバックエンド・アダプタ・
ゲームを読み込みます。通常エミュレータはUIとゲームで1つのVMを共有し、Studioの
プレビューはiframe内のVMでゲームを動かします。UIだけのVMは短い実行枠でブラウザへ
制御を戻し、ゲームのVMは従来の16ms枠を使います。`mrb_run_step_status`でタスクの
待機を検出したら、そのフレームの実行を終えます。

ブリッジの公開メソッドは、`open`・`select`・`fail`などRubyの既存メソッドと衝突する名前を
避けます。イベントの配列や状態はJSON文字列で渡します。複数行のゲームソースは取得した
JavaScript側で保持し、Rubyから送り返しません。要素はFunicular標準の`tag`で生成します。

## 検証

[Studioの起動手順](../studio/README.md)でWASMを生成し、HTTP経由で画面を開きます。
Rubyの制御テストは、WASMビルドと同じPicoRuby checkoutのFunicularクラスを読み込みます。

```sh
ruby tests/web/components_test.rb
node --test tests/web/pixel-art-model.test.js
```

ブラウザでは以下を確認します。

- Studioの構文チェック、エラー位置への移動、実行・再起動、素材追加、SD用ZIP、自動保存の復元
- ドット絵の描画、Undo/Redo、コマ追加・複製・並べ替え・再生、PNG入出力、素材更新
- 通常ゲームの切り替えと隣接ゲームの共有素材、2D・3Dサンプル、契約テストの緑画面
- キーボード・画面ボタン、シンタックスハイライト、スマートフォン幅の表示

Web UIだけの変更ではUF2の再生成は不要です。`Game` APIやランタイム契約も変える場合は
[開発ガイド](agent-development.md)に従い、UF2とWASMの両方をビルドします。
