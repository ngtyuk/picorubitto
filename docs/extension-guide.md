# `Game` APIの拡張

新しいゲーム機能は、実機とWASMで同じ契約になるように追加します。

## 手順

### 1. 契約を決める

先に、メソッド名、引数、戻り値、エラー、座標系、データ形式を決めます。実行先固有の
型は公開しません。任意機能なら`Game::Runtime.capability?`で確認できるようにします。

```ruby
if Game::Audio.supported?
  Game::Audio.play('beep.wav')
end
```

### 2. 共通ビルドを更新する

実機とWASMの両方で安全に使うgemは、`tools/runtime_profile.rb`の共通gem一覧へ追加します。
API本体の実装はバックエンドに置きます。

アセットの寿命を管理する共通処理もこの層に置きます。WASMの内部型`Game::AssetLease`は
`picoruby-game-common/src/asset_lease.c`で数値IDを所有し、Ruby GCからJSの解放処理を呼びます。
アセット本体をブリッジの永続参照表へ渡さず、公開するRubyのクラスとAPIは両環境で揃えます。

### 3. RP2350を実装する

ハードウェア処理は`src/`または`mrbgems/picoruby-game/`に追加します。

1. 必要なら`include/game_platform.h`に内部C境界を追加する。
2. `src/game_platform.c`でSPI、DMA、PSRAM、SD、PWMなどを処理する。
3. `mrbgems/picoruby-game/src/mruby/game.c`でRubyメソッドを公開する。このソースはgemとして
   コンパイルされ、名前がpresymへ登録されるため、CMakeに重複追加しない。
4. 引数、サイズ、パスをC側でも検証する。
5. 必要なら能力ビットを追加する。

PWM／I2Sのように同じAPIの物理出力だけを変える場合は、ゲームのメソッドを増やさず
ビルド時の選択とします。I2SはCMake／workflow・PIOプログラム・内部PCM変換で実装し、
WASMのWeb Audioと同じ公開契約を保ちます。両構成のUF2とWASM契約を確認し、物理配線・
追加資源・IRQ期限・実機未確認範囲を文書化します。

### 4. WASMを実装する

`emulator/game_adapter.rb`に同じメソッド名、引数、戻り値を実装します。Canvasやブラウザ
APIとの接続が必要なら`emulator/emulator.js`に閉じ込めます。ブラウザで再現できない機能は、
黙って別動作にせず能力問い合わせで示します。

### 5. 契約テストを追加する

`tests/portable/game_api_contract.rb`に、少なくとも次を追加します。

- メソッドまたは定数の存在
- 基本的な戻り値
- 正常な引数と不正な引数
- 実行先間で共通にできる描画・保存などの動作
- 任意機能の対応状態

このRubyファイルは、WASMページとRP2350用ROMの両方で実行します。

### 6. 文書と生成物を更新する

APIの仕様は[`api-reference.md`](api-reference.md)、実装の分担は
[`runtime-architecture.md`](runtime-architecture.md)、確認方法は
[`compatibility-testing.md`](compatibility-testing.md)へ記録します。

描画APIを追加するときは、RP2350のC拡張とWASMアダプタ／JavaScriptの両方に同じ
引数形式とエラー条件を実装し、契約テストへ最小限の呼び出しを追加します。

例えば`Game::ThreeD`では、`Capabilities::THREE_D`を追加し、実機側に深度バッファと
三角形ラスタライザ、WASM側に同じカメラ座標系・クリッピング・深度比較を実装します。
Rubyからは`set_viewport`、`set_resolution`、`set_camera`、`begin_frame`、
`draw_triangle`、`draw_triangles`、`draw_indexed_mesh`または`draw_wireframe`を呼び、
バックエンドの描画関数は直接公開しません。`draw_wireframe`の`edges`は頂点番号のペア列として
実装し、共有辺を重複して送らないことで、実機とWASMの両方で一括描画の利点を保ちます。
画面や深度バッファのサイズに依存するため、描画後は通常どおり`Game::Display.present`を呼び、
対応確認には3D目視テストも使います。

保持型のAPIは、入力のコピー時点とGC時の所有権を定義します。`ThreeD::Model`では
RP2350の`src/mruby/model.c`をgemの`spec.objs`へ登録し、公開名をpresymへ含めています。
WASMでは数値IDの`AssetLease`とJSONの受け渡しを使います。毎フレーム生成する配列を
上流ブリッジの永続参照表へ残さず、登録データと作業領域の予算を`Assets.resident_bytes`へ
同じ基準で計上します。契約テストに入力変更・不正な一括入力・GCの回収を追加し、
ホストCの変換結果とブラウザの画素も比較します。

描画内部の最適化では、色に加えて深度とdirty領域を旧実装と比較します。3Dの線スキップと
投影再利用は独立したビルド設定で検証し、両方を同時に有効にした場合も比較します。
入力配列・カメラ・解像度・呼び出しの境界で再利用範囲を切り、クリップ生成点と通常頂点を
混同しないようにします。数値方式の変更を併せて行わず、整数演算の範囲外も確認します。
Modelの共有経路は`game_3d_draw_model_geometry`内の同じインスタンスの面→線に限定します。
公開配列APIの呼び出し間をポインタの同一性だけで再利用せず、次のModelでも世代を更新します。
変更時は`model_shared_regression.py`とブラウザの`model-shared.html`で旧版と比較してください。
深度領域の配置変更では`tests/native/depth_regression.py`で、19200画素の境界、大小解像度の
反復、フレーム中の変更拒否、失敗時の設定復元、クリア範囲、PSRAM確保失敗を検証します。
SRAM増分とリンカmapのC用ヒープ枠を記録し、Core 1・音声・SD用の領域を維持します。

```sh
ruby tools/build/workflow.rb engine
```

組み込み文字の揃え方のように、両実行先で完全に同じ計算を使えるAPIは
`picoruby-game-common/mrblib/`へ置き、契約テストで両バックエンドから呼び出します。

## 判断基準

- ゲームコードは`Game` APIだけを使う。
- 実機とWASMで同じエラー条件を返す。
- 共通のビルド設定は`runtime_profile.rb`に集約する。
- CとJavaScriptの詳細をゲームへ漏らさない。
- 契約テストが通らない機能を共通APIとして扱わない。

後方互換性を壊す変更では`Game::API_VERSION`を更新し、既存ゲームへの影響を確認します。

音量ダイヤルのような任意の入力も、GPIOやADC操作をゲームへ公開せず、Cバックエンドで取得・
平滑化し、`Game::Audio.dial_supported?`・`dial_value`・`master_volume`と`AUDIO_DIAL`で
契約を公開します。WASMは同じ値域のUI入力を用意し、無効時の戻り値も契約テストで確認します。

音声APIを拡張する場合は、BGM専用1枠とSE 4枠の占有・置換・停止、固定ゲイン、
同時再生時のサンプルレート制約、BGMの1 MiB上限とループ境界を両バックエンドと契約テストで確認します。
