# サンプルゲーム

ここにはエンジンのAPIを確認するサンプルだけを置きます。自作ゲームは`local-games/`や
別リポジトリで管理します。初回の環境準備は[セットアップ](../docs/getting-started.md)を
参照してください。

| サンプル | ソース | ブラウザ |
| --- | --- | --- |
| 2D | [`games/2d/main.rb`](games/2d/main.rb) | [2Dサンプル](http://localhost:8080/emulator/2d-example.html) |
| 3D | [`games/3d/main.rb`](games/3d/main.rb) | [3Dサンプル](http://localhost:8080/emulator/3d-example.html) |

ブラウザ用HTTPサーバーはリポジトリのルートで起動します。

```sh
python3 -m http.server 8080 --bind 127.0.0.1
```

## 2D

10個のボタン状態、矩形・円・線とアルファ描画、FPS・押下ボタン名、document-kun
スプライト、テクスチャ、音声、衝突判定、セーブデータを確認します。
矢印キーで移動、A（`Z`・`Enter`・`Space`）で音声再生、B（`X`）で停止します。

スプライトの元画像は[`games/2d/source-assets/document-kun.png`](games/2d/source-assets/document-kun.png)、
実行時形式は`games/2d/assets/document-kun.raw`です。元画像の変更後は`--prepare-assets`で
変換します（Rubyの`chunky_png`が必要です）。

```sh
ruby tools/build/workflow.rb game examples/games/2d --prepare-assets
```

固定の効果音`games/2d/assets/beep.wav`を再生成する場合は、次を使います。

```sh
ruby tools/fixtures/generate_test_tone.rb
```

## 3D

160×120の内部解像度で自機・敵機・障害物・星・トンネルを描画する飛行ゲームです。
indexed mesh、深度付きwireframe、射撃、接触判定、2D HUDを確認します。

矢印キーで移動、A（`Z`）で射撃（長押しで連射）、B（`X`）でブースト、START（`P`）で
リセット、SELECT（`O`）でHUDを切り替えます。3回接触するとゲームオーバーになります。
発射間隔は約0.5秒で、弾速は`Game::Time.millis`による実時間基準で更新します。

```sh
ruby tools/build/workflow.rb game examples/games/3d
```

生成した`build/games/2d/`または`build/games/3d/`の中身をSDカードへコピーします。
実機の準備は[実機ガイド](../docs/hardware.md)、サンプルを検証に使う手順は
[互換性テスト](../docs/compatibility-testing.md)を参照してください。
