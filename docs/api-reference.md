# `Game` APIリファレンス

ゲームから使える、RP2350とWASMに共通のAPIです。APIバージョンは`15`です。

```ruby
require 'game'
Game.init

loop do
  Game::Display.clear(0x0000)
  Game::Display.present
  Game.tick(30)
end
```

ゲームROMはPicoRuby 4.0.5の`RITE0400`形式です。以前のROMは同梱版の`mrbc`で
再生成してください（[依存バージョン](dependencies.md)）。API 14の保持モデル・既存の配列描画APIに加え、API 15は音量ダイヤル、API 16はBGMとSEの独立した再生に対応します。

## 共通ルール

| 項目 | 値 |
| --- | --- |
| 画面 | 320×240、原点は左上 `(0, 0)` |
| 色 | RGB565の整数（`0x0000`〜`0xffff`） |
| 画像データ | 1ピクセル2バイト、ビッグエンディアン |
| 透明色 | 既定値は`0xf81f` |

ゲームは`Game` APIだけを使います。実機のGPIOやブラウザのJavaScriptを直接参照しません。

## 乱数

PicoRubyの`rand`を実機とWASMの両方で使えます。正の整数`n`を渡すと、0以上`n`未満の
整数を返します。引数なし、または`0`を渡した場合は0以上の32ビット整数を返します。
一般的なRubyの`rand`と異なり、引数なしの戻り値は小数ではありません。ゲームでは正の整数の
上限を指定してください。

```ruby
x = rand(320) # 0〜319
```

## 初期化とフレーム制御

### `Game.init`

```ruby
Game.init #=> true
```

ランタイムを初期化します。アセットや他のAPIを使う前に1回呼び出します。WASMでは、
HTMLに登録されたアセットの読み込みが完了するまで待ちます。

### `Game.tick(fps)`

```ruby
Game.tick(30) #=> nil
```

指定FPSの次のフレームまで待ち、入力と`Time`のフレーム値を更新します。通常は
`Display.present`の後に1回呼び出します。実機は1〜1000、WASMは1以上のFPSを受け付けます。

## 実行環境

### `Game::API_VERSION`

```ruby
Game::API_VERSION #=> 16
```

### `Game::Capabilities`

機能値はビットマスクです。

| 定数 | 値 | 機能 |
| --- | ---: | --- |
| `DISPLAY` | `1` | 画面描画 |
| `INPUT` | `2` | 入力 |
| `ASSETS` | `4` | アセット読み込み |
| `SPRITES` | `8` | スプライト |
| `AUDIO` | `16` | 音声 |
| `SAVE` | `32` | セーブデータ |
| `COLLISION` | `64` | 矩形衝突判定 |
| `TEXTURES` | `128` | テクスチャ |
| `FONTS` | `256` | カスタムフォント |
| `SHAPES` | `512` | 図形とアルファ合成 |
| `AUDIO_MIXING` | `1024` | 複数音声 |
| `THREE_D` | `2048` | 深度付き簡易3D三角形 |
| `THREE_D_MODELS` | `4096` | 保持モデルと一括インスタンス描画 |

### `Game::Runtime`

```ruby
Game::Runtime.backend       #=> "rp2350" または "wasm"
Game::Runtime.api_version   #=> 16
Game::Runtime.capabilities  #=> Integer
Game::Runtime.capability?(Game::Capabilities::AUDIO) #=> true / false
```

| メソッド | 戻り値 | 説明 |
| --- | --- | --- |
| `backend` | `String` | 実行先 |
| `api_version` | `Integer` | 実行中のAPIバージョン |
| `capabilities` | `Integer` | 対応機能のビットマスク |
| `capability?(mask)` | `true` / `false` | 指定した全ビットに対応しているか |

`capability?`に`0`や未対応ビットを渡すと`false`です。

## 入力

```ruby
Game::Input::UP     #=> 1
Game::Input::DOWN   #=> 2
Game::Input::LEFT   #=> 4
Game::Input::RIGHT  #=> 8
Game::Input::A      #=> 16
Game::Input::B      #=> 32
Game::Input::L      #=> 64
Game::Input::R      #=> 128
Game::Input::START  #=> 256
Game::Input::SELECT #=> 512
```

| メソッド | 説明 |
| --- | --- |
| `Game::Input.state` | 現在押されているボタンのビットマスク |
| `Game::Input.pressed?(button)` | ボタンが押されているか |
| `Game::Input.just_pressed?(button)` | 前フレームから今フレームまでに押されたか |
| `Game::Input.just_released?(button)` | 前フレームから今フレームまでに離されたか |

```ruby
if Game::Input.just_pressed?(Game::Input::A)
  # Aが押された瞬間の処理
end

if (Game::Input.state & Game::Input::LEFT) != 0
  # 左が押されている間の処理
end
```

実機の入力ピンは、十字キーがGP2〜GP5、A/BがGP6/GP7、L/R/START/SELECTが
GP9/GP12/GP14/GP16です。すべて内部プルアップで、GNDへ接続すると押下になります。
WASMは矢印キーと画面ボタンを使います。キーボードでは`Z`・`Enter`・`Space`がA、
`X`がB、`Q`/`E`がL/R、`P`/`O`がSTART/SELECTです。各問い合わせはフレーム内で同じ値を
返します。不正なボタン値は`ArgumentError`です。

## ディスプレイ

代表的な色は、赤`0xf800`、緑`0x07e0`、青`0x001f`、白`0xffff`、黒`0x0000`です。

### サイズと画面更新

```ruby
Game::Display.width  #=> 320
Game::Display.height #=> 240
Game::Display.clear(0x0010)
Game::Display.present
```

`clear(color)`はフレームバッファ全体を塗ります。`present`で画面に反映します。
`present`は通常1フレームに1回、描画の最後に呼び出します。
3Dフレームもここで終了します。次の`ThreeD.begin_frame`より前に解像度・ビューポートを
変更でき、同じROM内でシーンやテストを切り替えられます。

### 背景

```ruby
background = Game::Assets.read('background.raw')
Game::Display.set_background(background)

loop do
  Game::Display.begin_frame
  # スプライトや動く図形を描く
  Game::Display.present
  Game.tick(30)
end
```

`set_background(data)`は320×240、153600バイトのRGB565画像を登録します。
`begin_frame`は登録した背景をフレームの開始時に復元します。背景データはゲーム中保持し、
内容を変更しません。背景を使わない場合は`clear`を使います。

### 矩形と画像

```ruby
Game::Display.fill_rect(10, 20, 32, 24, 0xf800)
Game::Display.fill_rect(20, 20, 32, 24, 0xf800, 128)

image = Game::Assets.read('player.raw')
Game::Display.blit_rgb565(100, 80, 32, 32, image)

sheet = Game::Assets.read('tiles.raw')
Game::Display.blit_rgb565_region(0, 0, 32, 32, sheet, 128, 64, 0)
```

| メソッド | 説明 |
| --- | --- |
| `fill_rect(x, y, width, height, color, alpha = 255)` | 矩形を塗る。画面外はクリップ |
| `blit_rgb565(x, y, width, height, data)` | RGB565画像を原寸でコピー |
| `blit_rgb565_region(x, y, width, height, data, source_width, source_x, source_y)` | 画像の一部をコピー |

`fill_rect`の幅・高さは正数、`alpha`は0〜255です。`alpha`が0なら描画しません。
`blit_rgb565`のデータサイズは`width * height * 2`バイトです。これらの画像コピーは
透明色を処理せず、全ピクセルを上書きします。

### テキストと図形

```ruby
Game::Display.draw_text(8, 8, 'SCORE: 10', 0xffff)
Game::Display.text(8, 24, 'READY') # draw_textと同じ
Game::Display.text_aligned(160, 48, 'ゲーム開始', :center)
Game::Display.text_aligned(320, 72, '得点 100', :right, 0xffe0)
Game::Display.draw_circle(160, 120, 32, 0x07e0, 160)
Game::Display.draw_line(10, 10, 200, 100, 2, 0xffff)
```

`draw_text`と`text`は組み込みフォントで描画します。色を省略すると白です。
英字・数字・記号などの印字可能なASCII文字は6×12ドットで表示し、大文字・小文字を区別します。
各文字は12ピクセル幅の枠内で中央に置きます。
ひらがな・カタカナ（JIS X 0208に含まれる文字）、JIS第1・第2水準の漢字6,355字、
全角空白、`、。 「」・ー！？`は
12×12ドットで表示します。すべての文字の送り幅は12ピクセルです。対応外のUnicode文字は
1文字につき`?`を1つ表示します。改行は対応外です。

`text_aligned(x, y, text, alignment = :left, color = 0xffff)`は、`x`を文字列の
`:left`（左端）、`:center`（中央）、`:right`（右端）の基準として描画し、`nil`を返します。
幅は文字数×12ピクセルで、画面外はクリップします。`x`と`y`は32ビット符号付き整数、
色は0〜65535の整数です。指定外の揃え方、範囲外の座標や色、NULを含む文字列は
`ArgumentError`、座標・色や文字列の型が不正な場合は`TypeError`になります。

英数字・かな・漢字の字形にはマルミーニャ1.500の12ドットゴシックを使用しています。出典とライセンスは
[マルミーニャのライセンス](../tools/fonts/MARUMINYA-OFL.txt)を参照してください。
`Game::Font`で読み込むPBFは別の機能で、引き続き非ASCII文字を扱いません。

`draw_circle(x, y, radius, color, alpha = 255)`は塗りつぶし円、
`draw_line(x1, y1, x2, y2, thickness, color, alpha = 255)`は太さ付きの線を描きます。
円の半径は0〜320、線の太さは1〜320、アルファ値は0〜255です。画面外はクリップされます。

## 簡易3D

`Game::ThreeD`は、ワールド座標のRGB565三角形をソフトウェア描画します。既定の出力先は
320×240画面全体ですが、内部解像度と出力ビューポートを変更できます。実機とWASMの両方で
深度バッファを使うため、三角形を奥行き順に並べる必要はありません。テクスチャ、ライティング、
三角形以外のモデル形式は対象外です。

```ruby
Game::Display.begin_frame
Game::Display.clear(0x0010)
Game::ThreeD.set_viewport(0, 0, 320, 240)
Game::ThreeD.set_resolution(160, 120)
Game::ThreeD.set_camera(0, 0, 0, 0, 0, 70, 0.1, 100)
Game::ThreeD.begin_frame
Game::ThreeD.draw_triangle(
  -1, -1, 3,
   1, -1, 3,
   0,  1, 3,
  0xf800
)
Game::Display.present
```

### `Game::ThreeD.set_camera`

```ruby
Game::ThreeD.set_camera(
  x, y, z, yaw, pitch,
  fov_degrees = 70, near_clip = 0.1, far_clip = 100
)
```

カメラの位置はワールド座標、`yaw`と`pitch`はラジアン、`fov_degrees`は度です。初期状態は
原点 `(0, 0, 0)` から`+Z`方向を見るカメラです。`+X`が右、`+Y`が上です。視野角は1より大きく
179未満、near/farは`0 < near_clip < far_clip`にします。座標と設定値は整数または浮動小数点数を
使えます。

### `Game::ThreeD.begin_frame`

```ruby
Game::ThreeD.begin_frame #=> nil
```

深度バッファをクリアします。`Display.begin_frame`または`Display.clear`で色を準備したあと、
そのフレームの最初に1回呼び出します。3D描画APIはこの呼び出しのあとに使います。

### `Game::ThreeD.set_viewport`

```ruby
Game::ThreeD.set_viewport(x, y, width, height) #=> nil
```

3Dの描画結果を配置する画面上の矩形を設定します。座標は320×240画面のピクセル単位で、
矩形全体が画面内に収まる必要があります。初期値は`(0, 0, 320, 240)`です。フレームの描画中
には変更できないため、通常は`ThreeD.begin_frame`より前に1回設定します。内部解像度が
ビューポートと異なる場合は最近傍補間で拡大・縮小します。

### `Game::ThreeD.set_resolution`

```ruby
Game::ThreeD.set_resolution(width, height) #=> nil
```

3Dの投影、ラスタライズ、深度判定に使う内部解像度を設定します。幅は1〜320、高さは1〜240で、
初期値は`320×240`です。設定は画面全体の解像度を変更せず、結果は`set_viewport`で指定した矩形へ
出力されます。`160×120`は画素数を4分の1にして3Dの走査負荷を軽くする代表的な設定です。
フレームの描画中には変更できません。

標準RP2350構成は19200画素以下の深度をSRAMへ、それ以上をPSRAMへ配置します。
これは内部の配置で、解像度の許容範囲・深度比較・戻り値は実機とWASMで同じです。

### 三角形の描画

```ruby
Game::ThreeD.draw_triangle(
  x1, y1, z1, x2, y2, z2, x3, y3, z3, color
)

Game::ThreeD.draw_triangles([
  x1, y1, z1, x2, y2, z2, x3, y3, z3, color,
  x1, y1, z1, x2, y2, z2, x3, y3, z3, color
])
```

`draw_triangles`は`[x1, y1, z1, x2, y2, z2, x3, y3, z3, color, ...]`形式の10値レコードを
受け取ります。`color`はRGB565整数です。三角形はnear/far平面でクリップされ、画面外はクリップ
されます。画面内では深度が近いピクセルが優先されます。現在の画面座標系で面積が正になる頂点
順を表面として扱い、逆向きの三角形は裏面カリングで描画しません。毎フレームの描画順は問いませんが、
`ThreeD.begin_frame`から`Display.present`までに呼び出してください。3D描画のあとに2DのHUDを
通常の`Display` APIで重ねられます。

共有頂点を持つモデルは、次のindexed mesh形式で描画できます。

```ruby
Game::ThreeD.draw_indexed_mesh(
  [x0, y0, z0, x1, y1, z1, x2, y2, z2],
  [0, 1, 2],
  [0xf800]
)

Game::ThreeD.draw_wireframe(
  [x0, y0, z0, x1, y1, z1, x2, y2, z2],
  [0, 1, 1, 2, 2, 0],
  0xffff
)
```

`vertices`はXYZのフラット配列、`indices`は三角形ごとの頂点番号のフラット配列、`colors`は
三角形ごとのRGB565配列です。頂点は1フレーム内で一度だけ変換されます。頂点数は最大256で、
`indices.length`は3の倍数、`colors.length`は三角形数と一致する必要があります。

### `Game::ThreeD.draw_wireframe`

```ruby
Game::ThreeD.draw_wireframe(vertices, edges, color, depth_test = true)
```

`vertices`はXYZのフラット配列、`edges`は線分の両端の頂点番号を並べたフラット配列です。
例えば`[0, 1, 1, 2, 2, 0]`は3本の辺を表します。`edges.length`は2の倍数で、頂点数は
最大256です。`color`はRGB565整数で、線幅は内部解像度上の1ピクセルです。頂点変換と線分描画は
バックエンド側で一括処理されるため、モデルの共有辺は一度だけ指定してください。

`depth_test`が`true`なら、線分の奥行きを深度バッファと比較し、手前の線だけを描きます。
`false`なら深度バッファを更新・参照せず、指定した線を現在の3D画像へ重ねます。線分はnear/far
平面でクリップされ、画面外の部分は描画されません。`ThreeD.begin_frame`から
`Display.present`までに呼び出してください。

3D機能が使えるかは次で確認できます。

```ruby
if Game::Runtime.capability?(Game::Capabilities::THREE_D)
  # Game::ThreeD を使う
end
```

これは軽量なソフトウェア描画機能です。小さなモデルや低ポリゴンのゲームを想定し、画面を
大きく覆う三角形の数を増やしすぎないでください。

同じ頂点を使うindexed meshとwireframeでは、通常の固定小数点描画経路で投影結果を
1回の呼び出し中だけ再利用します。画面外の線走査は元の画素位置と深度を保って省略します。
既存の描画引数・上限・描画順は同じです。

### `Game::ThreeD::Model`

モデルの形状を一度登録し、位置・回転・拡大率だけを毎フレーム渡します。
`Game::Runtime.capability?(Game::Capabilities::THREE_D_MODELS)`で対応を確認できます。
同じインスタンスの面と線は内部の準備結果を共有します。別インスタンスや、後の描画呼び出しへ
投影値を持ち越さず、面→線の描画順・深度判定・保持量の契約は同じです。

```ruby
vertices = [-1, -1, 0, 1, -1, 0, 0, 1, 0]
model = Game::ThreeD::Model.new(vertices, [0, 2, 1], [0x07e0],
  [0, 1, 1, 2, 2, 0], 0xffff, true)
# 三角形のみなら最初の3引数、線のみならindicesとcolorsを[]にする。
Game::ThreeD.begin_frame
model.draw_instances([
  # x, y, z, yaw, pitch, roll, scale_x, scale_y, scale_z
  -1, 0, 5, 0.2, 0, 0, 1, 1, 1,
   1, 0, 7, 0, 0, 0.3, 0.5, 0.5, 0.5
]) #=> nil
Game::Display.present
```

`Model.new(vertices, indices, colors, edges = [], color = 0xffff, depth_test = true)`は各配列を
検証してコピーします。登録後の元配列の変更は反映されません。`vertices`はローカル座標XYZの
平坦な配列、`indices`は三角形の頂点番号3個ずつ、`colors`は三角形ごとのRGB565整数です。
`edges`は線の頂点番号2個ずつ、`color`と`depth_test`は線に適用します。

| 定数／上限 | 値 |
| --- | ---: |
| `Model::MAX_VERTICES`（1モデル） | 256（最小1） |
| `Model::MAX_TRIANGLES`（1モデル） | 1024 |
| `Model::MAX_EDGES`（1モデル） | 2048 |
| `Model::MAX_INSTANCES`（1呼び出し） | 128 |

`draw_instances(transforms)`は1体につき9値の平坦な配列です。各体のローカル座標を
拡大→yaw→pitch→roll→平行移動の順で変換し、現在のカメラ・解像度で三角形、線の順に描きます。
体の順序は配列順です。角度はラジアンで、yawは`x'=x*cos(yaw)-z*sin(yaw)`、
pitchは`y'=y*cos(pitch)-z*sin(pitch)`、rollは`x'=x*cos(roll)-y*sin(roll)`です。
各式の右辺はその回転前の座標です。負の拡大率は面の向きも反転し、既存の裏面カリングに従います。
0の拡大率も受け付けます。入力と変換結果はバックエンドの単精度浮動小数点を使い、
Rubyで計算してから従来APIへ渡す場合とは丸め境界の画素が異なる場合があります。

頂点座標と変換9値は`Integer`または`Float`で、有限かつ絶対値32768以下です。
配列・数値・整数・真偽値の型違いは`TypeError`、配列長・上限・添字・色・数値範囲の違反は
`ArgumentError`です。変換配列全体を検証してから描きます。空の変換配列は`nil`を返し、
非空の描画は`ThreeD.begin_frame`から`Display.present`までに呼びます。
フレーム外の非空描画は`ArgumentError`です。

モデルは複数の変数・複数インスタンスで共有でき、参照がなくなるとGCが登録データと作業領域を
回収します。`dup`・`clone`は`TypeError`です。明示的な解放操作は設けていません。
`Assets.resident_bytes`にはモデルの生データと再利用するワールド頂点領域も含みます。
頂点数V、三角形数T、辺数Eに対して`24*V + 8*T + 4*E`バイトです。
管理用の構造体、Ruby/JSオブジェクト、変換配列、レンダラの共通作業領域は別です。

## アセット

```ruby
asset = Game::Assets.read('player.raw')
```

`assets/`から読み込みます。パスは`assets/`からの相対パスで、`..`、絶対パス、ドライブ
指定は使えません。戻り値は実機では`String`、WASMではランタイム管理のアセット値です。
`Display`、`Sprite`、`Texture`、`Font`、`Audio`へそのまま渡します。

### アセットの回収

`Sprite`、`Texture`、`Font`、`Audio::Sound`はRubyのGCと連動します。参照がなくなると
実データも回収し、次の読み込みに再利用します。実機では空き不足時にGC後、一度再試行します。
シーン切り替え時には明示的に回収できます。

```ruby
texture = nil
Game::Assets.collect        #=> nil（Ruby GCと不要な音声キャッシュの回収）
Game::Assets.resident_bytes  #=> 管理中アセットの生データ合計バイト数
```

`resident_bytes`は上記4種類と`Audio.play`のキャッシュ、`ThreeD::Model`のデータと
ワールド頂点作業領域を対象にします。VMヒープ全体、
管理用の領域、`Assets.read`の戻り値は含めません。実機ではPSRAMとSRAMキャッシュを合算します。
WASMでは管理ハンドルのデータ量を数え、仮想SDとして保持する読み込み元や
Canvas・Web Audioの実メモリ量は含めません。

再生中に`Sound`への参照がなくなっても、再生に必要なデータを保持します。実機では音声IRQが
読み終わったデータをCore 0で回収し、WASMでは再生終了・停止後に回収します。
`Audio.play`のパス別キャッシュは最大8件で、未使用のキャッシュを回収して再利用します。
各ファイルは読み込み時に全体がメモリへ収まる必要があります。

`Display.set_background(data)`は登録中のデータへの参照を保持します。登録中はデータを変更せず、
背景を使い終わったら`Display.set_background(nil)`で参照を解除します（戻り値は`nil`）。

`Texture#checksum`は読み込んだRGB565バイト列のFNV-1a 32-bitチェックサムを、
`0..4294967295`の整数で返します。大容量アセットの内容照合にも使えます。

## スプライト

```ruby
player = Game::Sprite.load('player.raw', 32, 32, 4)
player.draw(100, 80, 0)
player.draw_batch([100, 80, 0, 140, 80, 1])
player.width   #=> 32
player.height  #=> 32
player.frames  #=> 4
```

`Sprite.load(path, frame_width, frame_height, frame_count, key_color = 0xf81f)`は、
横一列のRGB565スプライトシートを読み込みます。ファイルサイズは
`frame_width * frame_height * frame_count * 2`バイト、フレーム番号は`0...frame_count`
です。キー色のピクセルは透明になり、画面外はクリップされます。無効なサイズやフレーム
番号は`ArgumentError`です。

`draw_batch(placements)`は、`[x, y, frame, ...]`形式の整数配列に含まれるスプライトを
まとめて描画します。配列の長さは3の倍数でなければなりません。毎フレーム多数の同じ
スプライトを描く場合に、Ruby/CまたはRuby/JavaScript境界の呼び出し回数を減らせます。

## テクスチャ

```ruby
image = Game::Texture.load('scene.raw', 320, 240)
image.draw(0, 0)
image.draw(80, 40, 0, 0, 64, 64, 128, 128)
```

`Texture.load(path, width, height, key_color = 0xf81f)`は、1枚のRGB565画像を読み込みます。
ファイルサイズは`width * height * 2`バイトです。

`texture.draw(x, y, source_x = 0, source_y = 0, source_width = texture.width,
source_height = texture.height, destination_width = source_width,
destination_height = source_height)`で、画像の一部を最近傍法で描きます。キー色は透明です。
回転、半透明、バイリニア補間はありません。`width`と`height`で元画像の寸法を取得できます。

実機ではPNGやJPEGを読みません。ホスト側で`tools/assets/convert_assets.rb`を使い、`.raw`を作ります。

## フォント

```ruby
font = Game::Font.load('ui.pbf')
font.draw('SCORE: 120', 8, 8, 16, 1, 0xffff)
width = font.measure('SCORE: 120', 16, 1)
```

`Font.load(path)`は、ホスト側でTTF/OTFから作った`.pbf`を読み込みます。
`font.measure(text, pixel_size, spacing = 0)`は文字列の幅、
`font.draw(text, x, y, pixel_size, spacing = 0, color = 0xffff)`は描画を行います。
改行に対応し、非ASCII文字は`?`になります。指定したサイズがPBFに含まれていない場合は
`ArgumentError`です。
`measure`は各行の末尾の文字間隔を除いた幅の最大値を返し、空文字は0です。
PBFの全体検証はロード時に行い、描画時には画面内の範囲を処理します。

変換例：

```sh
ruby tools/build/workflow.rb game path/to/game --convert-assets --font-sizes 12,16,20,24
```

## 音声

```ruby
if Game::Audio.supported?
  Game::Audio.play_bgm('bgm.wav', 255, true) # 自動ループ
  Game::Audio.play_se('beep.wav', 220)
end

beep = Game::Audio::Sound.load('beep.wav')
beep.play_se
beep.stop
Game::Audio.stop_bgm
Game::Audio.stop
Game::Audio.busy? #=> true / false
```

| メソッド | 説明 |
| --- | --- |
| `Game::Audio.supported?` | 音声出力を利用できるか |
| `Game::Audio.play_se(path, volume = 255)` | SEを再生（最大4音） |
| `Game::Audio.play_bgm(path, volume = 255, loop = false)` | BGMを再生（1音、新しい要求で置換） |
| `Game::Audio.stop_bgm` | BGMだけを停止 |
| `Game::Audio.play(path, volume = 255)` | `play_se`と同じ |
| `Game::Audio.stop` | すべての音声を停止 |
| `Game::Audio.busy?` | 再生中の音声があるか |
| `Game::Audio.dial_supported?` | 音量ダイヤルの読み取りを利用できるか |
| `Game::Audio.dial_value` | 平均化したADC値（`Integer`、0〜4095）。ダイヤル無効時は`nil` |
| `Game::Audio.master_volume` | 全体音量（`Integer`、0〜255）。ダイヤル無効時は255 |
| `Game::Audio::Sound.load(path)` | WAVを再利用できるオブジェクトとして読み込む |
| `sound.play_se(volume = 255)` | その音源をSEとして再生 |
| `sound.play_bgm(volume = 255, loop = false)` | その音源をBGMとして再生 |
| `sound.play(volume = 255)` | `sound.play_se`と同じ |
| `sound.stop` | その音源から始めた再生を停止 |

`Audio.play`は同じパスの音声データを再利用します。実機では初回成功時にキャッシュし、
WASMでは事前読み込み済みのデータを使います。アセット変更後は再起動してください。
`Sound.load`で作ったオブジェクトはそれぞれ独立した停止対象です。実機のメモリを節約するには、
同じ音の`Sound.load`を繰り返さず、取得したオブジェクトを保持して再生します。

共通形式は、PCM、モノラル、8-bit unsigned、サンプルレート96kHz以下です。WAV全体の上限はBGMが1 MiB、SEが128 KiBです。
`Sound.load`は最大1 MiBを読み込めますが、128 KiBを超える音はSEとして再生できません。
BGMは1音で、新しいBGMは現在のBGMを置き換えます。`loop`をtrueにすると終端から先頭へ繰り返します。
ループ時は音源の先頭と末尾のフェードを行いません。波形の端が不連続な素材ではクリック音が生じます。
WAV全体をメモリへ読み込むため、PSRAMの空き不足時は読み込みに失敗します。
SEは最大4音で、満杯時の新しいSEは既存音を維持したまま破棄します。`volume`は0〜255です。
実機で同時再生する音源のサンプルレートはそろえてください。異なるレートの要求はエラーとなり、
再生中の音は維持されます。WASMでは最初のユーザー操作後に音声を再生します。

合成時の固定ゲインはBGMが1/2、SEは各1/8です。SEの数が変わってもBGMのゲインは変わりません。
最大音量のBGMと4音のSEを足してもデジタル出力の範囲に収まる配分です。実機では整数の丸め、
ブラウザではWeb Audioの演算による差があります。API 16では既存の`play`をSEとして扱うため、
旧版より単音の出力が小さくなります。実機のDMAバッファにある音は停止・置換後もしばらく残ります。

実機の出力はビルド時にPWM（既定）またはMAX98357A用I2Sを選べます。
I2Sも同じAPI・入力WAV・BGMとSEの合成規則を使い、48kHz・signed 16-bitの左右同一PCMへ変換します。
実機の`--audio-max-gain`はミックス・ダイヤル適用後の出力振幅を0〜100%に制限します。
省略時は100%で、PWMとI2Sの両方に適用します。`master_volume`の値に上限は含みません。
WASMにはこの物理出力の上限を適用しません。
8-bit WAVを16-bit WAVとして読み込む変更ではありません。再生速度は元のサンプルレートに従います。
配線と設定は[実機ガイド](hardware.md#max98357aのi2s出力)を参照してください。

音量ダイヤルは`Game::Capabilities::AUDIO_DIAL`（`1 << 13`）でも照会できます。
実機は`engine --volume-knob`でADC2/GP42を有効にすると、再生中・以後の全音源へ全体音量を
自動適用します。`dial_supported?`はビルド・初期化の対応状態で、つまみの接続検出ではありません。
ADCは50Hz、4回平均、音量は1/4の平滑化を通します。平滑化後128以下は0、3800以上は255、
間は`(値 - 128) * 255 / 3672`の整数値です。`dial_value`は平滑化前の4回平均です。
各音源の`volume`に全体音量を乗算します。`Audio.stop`や`Game.init`でつまみ位置は変わりません。
WASMは画面の「音量ダイヤル」を仮想ADC値とし、同じ範囲・両端判定で全体音量を変更します。
報告値は即時に変わり、実際のゲインはWeb Audioで滑らかに追従します。
配線は[ダイヤル音量](hardware.md#ダイヤル音量adc)を参照してください。

## 時間

```ruby
started = Game::Time.millis
elapsed = Game::Time.millis - started
delta_ms = Game::Time.delta_ms
x += speed * delta_ms / 1000
```

`millis`はランタイム起動後の経過ミリ秒、`delta_ms`は前フレーム境界からの経過ミリ秒です。
`delta_ms`は`Game.init`後の最初のフレームでは0で、フレーム内では変わりません。

## セーブデータ

```ruby
if Game::Save.supported?
  Game::Save.write('player', 'level=2')
  data = Game::Save.read('player')
  Game::Save.exists?('player')
  Game::Save.delete('player')
end
```

実機ではSDカードの`saves/`、WASMではブラウザの永続ストレージへ保存します。スロット名は
ASCII英数字、`_`、`-`だけで1〜24バイト、データは0〜32768バイトです。`read`は存在しない
スロットで`nil`を返し、`write`と`delete`は成功すると`true`を返します。保存は同期処理なので、
毎フレームではなくイベント時に呼び出します。

## 矩形衝突判定

```ruby
hit = Game::Collision.rects_overlap?(
  player_x, player_y, player_width, player_height,
  enemy_x, enemy_y, enemy_width, enemy_height
)
```

2つの軸平行矩形が重なれば`true`です。辺が接するだけなら`false`です。幅と高さは正数に
します。条件外の値は`ArgumentError`です。

## エラー

主なエラーは次のとおりです。

| 条件 | エラー |
| --- | --- |
| 色、座標、サイズ、アルファ値、フレーム番号が不正 | `ArgumentError` |
| 画像や保存データの型が不正 | `TypeError` |
| アセット、画像、フォント、WAV、SDの読み込みに失敗 | `RuntimeError` |
| セーブデータが32768バイトを超える | `RangeError` |
| TFT転送に失敗 | `RuntimeError` |

ターゲットによって例外メッセージは異なる場合があります。ゲームでは有効な入力範囲を
守り、任意機能は`Game::Runtime.capability?`で確認します。

## 実装場所

| 内容 | ファイル |
| --- | --- |
| RP2350のRuby API | `mrbgems/picoruby-game/src/mruby/game.c` |
| RP2350のハードウェア処理 | `src/game_platform.c`, `include/game_platform.h` |
| WASM API | `emulator/game_adapter.rb` |
| Canvas、入力、仮想SD、保存 | `emulator/emulator.js`と各HTML |
| 共通契約テスト | `tests/portable/game_api_contract.rb` |

APIを変更する場合は[`extension-guide.md`](extension-guide.md)と
[`compatibility-testing.md`](compatibility-testing.md)も更新します。
