# 実機とWASMの互換性テスト

リポジトリを[セットアップ](getting-started.md)した後、ルートから以下を実行します。
同じ`Game` APIの契約を実機とWASMで確認し、対象に応じてホスト回帰テストも使います。
ホストの模擬ハードウェアやブラウザの結果だけで、実機の表示・音声・性能を判定しません。

## 変更ごとの確認

| 変更 | 確認 |
| --- | --- |
| エンジン、共通API、C、共通ビルド設定 | UF2とWASMの両ビルド、WASM契約、実機用統合ROM生成、可能なら実機起動 |
| WASMアダプタ／JavaScript | WASM契約、通常エミュレータ、ブラウザコンソール。API変更は実機も更新 |
| Studio・Web画面 | Studioのビルド、構文チェック・実行・素材・保存・SD用ZIP |
| ゲームRuby・ゲーム素材 | 対象ROM生成と通常エミュレータ |

エンジン変更では`engine --firmware-only`や`engine --wasm-only`だけで完了にしません。
必要な環境変数は[開発ガイド](agent-development.md)を参照してください。

```sh
ruby tools/build/workflow.rb engine
ruby tools/build/workflow.rb checks
```

## WASM契約テスト

[`tests/portable/game_api_contract.rb`](../tests/portable/game_api_contract.rb)は、API版、
機能問い合わせ、`rand`の戻り値と範囲、入力、2D・3D描画、組み込みかな・漢字と文字揃えの呼び出し、アセットの寿命、音声、保存、衝突、時間を検査します。
組み込み英数字・かな・漢字の実機CとブラウザJavaScriptのデータおよびピクセル一致は
`python3 tests/native/kana_text_regression.py`で検査します。組み込み字形の再生成には
[マルミーニャの配布元](https://github.com/hicchicc/x12y12pxMaruMinya)の1.500版TTFと
Pillow 12.3.0を用い、`python3 tools/fonts/generate_builtin_font.py PATH_TO_TTF`を実行します。
通常のビルドではTTFやPillowは不要です。文字揃えの基準座標と入力の検査は
`ruby tests/native/text_alignment_test.rb`でも確認できます。

```sh
python3 -m http.server 8080 --bind 127.0.0.1
```

<http://localhost:8080/emulator/contract.html>を開き、緑画面とコンソールに例外がないことを
確認します。赤画面は失敗です。続けて[2Dサンプル](http://localhost:8080/emulator/2d-example.html)、
[3Dサンプル](http://localhost:8080/emulator/3d-example.html)、
[通常エミュレータ](http://localhost:8080/emulator/)で、表示・入力・音声を確認します。
通常エミュレータのゲーム一覧には`local-games/`のゲームが必要です。

契約テスト用の固定フォントだけを再生成する場合は次を使います。

```sh
ruby tools/fixtures/generate_test_font.rb
```

## 実機の統合テストROM

`workflow.rb checks`は、現在のソースとエンジンビルドで生成したホストコンパイラを使い、
`build/tests/hardware-suite/`へ`game.mrb`・素材・確認用`main.rb`を生成します。
検査用の画像32枚、計16MiBも生成するため、`assets/`全体をコピーしてください。

1. [実機ガイド](hardware.md)に従って配線し、ボードに合う最新UF2を書き込む。
2. `build/tests/hardware-suite/`の`game.mrb`と`assets/`をFAT32のSDカード直下へコピーする。
3. カードを挿入して再起動し、USB CDCシリアルの起動ログを確認する。
4. メニューから個別項目、またはRUN ALLを実行する。

WeAct用は`engine --board weact_studio_rp2350b_core`でビルドします。
起動ログの`PSRAM: available=1 size=8388608`と`PSRAM: pattern test OK`も確認します。
UF2、ROM、素材は同じソースから生成した組み合わせを使い、生成物はコミットしません。

### 操作と判定

- 上下で項目を選び、Aで実行します。
- 自動テストの結果画面はAで次へ、Bでメニューへ戻ります。
- 表示・聴取の確認画面はAでOK、BでFAILを記録します。
- ボタンテストは10個すべてを押し、STARTを最後に押します。
- 性能テストはBで中断できます。

自動判定はPASS、目視・聴取はOK、性能計測の完了はMEAS、中断はSTOPです。
計測完了は性能目標の合格を意味しません。FAIL後も次の項目へ進めます。

| 項目 | 確認する内容 |
| --- | --- |
| RUBY / VM | 整数・Float・Math・コレクション・ブロック・例外・GC |
| COMMON API | 共通契約、保存の往復、描画とアセット |
| 2D / TEXTURE | スプライト、PBF、等倍・拡大縮小・非整数倍率・クリップ |
| 3D / RESOLUTION | 面・線・HUD、160×120→320×240→160×120の切替 |
| BUTTONS | 十字キー、A/B、L/R、START/SELECTの10ボタン |
| AUDIO | 単音、効果音、BGM、BGMと最大4 SE、停止 |
| SAVE | 保存・読出し・削除。元の内容があれば復元 |
| ASSET / GC | 大容量画像の入替え、使用中アセットの保持、解放後の保持量 |
| FONT / CLIP | 幅、改行、代替文字、破損PBF拒否、四辺クリップと見本、組み込みかな・漢字 |
| PERF / 2D | 32・128・256体、背景差分復元 |
| PERF / 3D | 保持モデルの飛行シーン、内部160×120・320×240 |
| PERF / RENDER | 共有頂点の球面と長い線 |
| PERF / VM | 整数、配列・Hash、文字列の固定処理と結果確認 |
| PERF / TRI | 小さい面、細長い面、大面積の三角形 |
| AUDIO / MIX | 単音、実4音、事前合成音の聴き比べ（実4音は単音の4倍の固定ゲイン） |
| AUDIO / DIAL | ADC値・全体音量・再生中の追従。機能無効時はSKIP |
| AUDIO / RATE | 48kHz・22.05kHzの同じ単音と、48kHz無音の聴き比べ |

`ASSET / GC`の各画像終了後は、ログの`resident`が`baseline`へ戻ることを確認します。
`FONT / CLIP`の`font-invalid.pbf`拒否は想定内です。見本との表示比較も行います。

### 音声の確認

AUDIOでは上下左右が単音、A/B/Lが効果音、SELECTがBGM切替、Rが4音再生、STARTが終了です。
BGMと効果音を重ね、音切れ・ノイズ・停止と再開を確認します。ログは再生要求であり、
聴取の自動判定ではありません。
出力上限付きUF2は起動ログの`AUDIO: max_gain=...%`を確認し、ダイヤルの最小・最大、
単音と重ね再生を聴き比べます。物理アンプとスピーカーの音量は自動判定できません。

BGMと効果音だけを確認する専用ROMは、次で生成できます。

```sh
ruby tools/build/workflow.rb checks --audio-only
```

`build/tests/audio-suite/`の`game.mrb`と`assets/`をSDへコピーします。
起動直後にAUDIO PADが開きます。SELECTでBGMを開始し、A/B/Lで効果音を重ねます。
RはBGMと効果音3つをまとめて開始します。BGM用に1音分を確保し、効果音を連打しても
BGMを維持します。BGMは自動ループし、境目も聴取対象です。
STARTで全音停止、停止画面のAでパッドを開き直します。画面は毎フレーム更新します。
ダイヤル付きUF2では、再生中の全体音量も調節できます。

AUDIO / MIXでは左右で音量を選び、Aで単音、Bで実4音、Lで事前合成音、Rで停止します。
音量32から始めて定常部分を比較します。実機とWASMはBGM 1/2、各SE 1/8の固定ゲインで加算します。
実機の整数丸めとブラウザの演算には差があります。PCM検査だけでアンプや電源の影響は確認できません。
SELECTでLIVE（毎フレーム描画・転送）→DRAW（毎フレーム描画、転送は操作時だけ）→
STATIC（描画・転送とも操作時だけ）に切り替えられます。
切替では再生を止めず、音源・音量・再生位置も変えません。同じ再生の途中で、表示更新の有無を比較します。

AUDIO / DIALではAでBeep、Bで4音、SELECTでBGM切替、Rで停止、STARTで終了します。
両端に0.5秒以上止め、全体音量0／255、無音／最大音量、再生中の追従を確認します。
[ダイヤル配線](hardware.md#ダイヤル音量adc)とI2S併用時のDIN変更も確認してください。
Lで画面更新をLIVE→DRAW→STATICに切り替えます。STATICでもADC取得・音量追従・ログは継続し、
ボタン操作時は表示を更新します。DIAL画面の4音・BGMは4秒ごとに再生し直します。

AUDIO / RATEではAで48kHz、Bで22.05kHzの1kHz単音、Lで48kHzの無音を2.5秒再生し、
Rで停止、STARTで終了します。I2S版の48kHz音源はサンプルレート変換による補間が不要です。
各音源は一度だけ再生し、選択が変わると前の音を止めます。
初期表示はSTATICです。選択した音源の表示を転送してから200ms待ち、再生開始時には
画面を更新しません。非同期TFT転送が落ち着いた状態から聴くための待ち時間であり、
転送完了を取得するAPIではありません。SELECTでSTATIC→LIVE→DRAW→STATICと切り替え、
同じ単音・無音で画面転送との関係を比較できます。LIVEでは下のマーカーも動かします。
切替そのものは再生を止めませんが、ラベル表示のため1回転送します。切替後もノイズが続く場合は
STATICでR→A/B/Lと操作し、200ms待ってから開始する再生と比較します。
単音の振幅は8-bit PCMの中心128に対して±32、再生音量は255です。ダイヤル併用版では
最大端に置き、ログの`master=255`を確認するとADCの揺れの影響を減らせます。
開始直後と終了直前を避け、0.5〜2秒を比較します。`[AUDIO RATE]`と`AUDIO: I2S stats`を
記録してください。無音ではI2S転送が続き、停止では転送も止まるため、この差も確認できます。
この検査だけで実配線上のクロックやアンプの正常動作を保証するものではありません。

PCM診断付きI2S版では、ダイヤル最大端で`master=255`に固定し、各2.5秒の再生後にRを押します。
statsの`pcm_frames=4096`と次の値を照合できます。音源・再生音量・全体音量が異なる場合は比較できません。

| AUDIO / RATE | pcm_fnv1a | pcm_max_step |
| --- | --- | --- |
| A：48kHz単音 | `3d11ac59` | 256 |
| B：22.05kHz単音 | `39750a6f` | 236 |
| L：48kHz無音 | `bcc31dc5` | 0 |

`python3 tests/native/i2s_regression.py`は32-bit／64-bit両演算を、独立した絶対位置からの
PCM参照計算と照合します。符号・位相の端と乱数による30万条件、1〜96000Hzの時間軸、
バッファ境界、4096到達後の診断停止と初期化、実際のPIO命令も検査します。
`I2S_PROBE_OUTPUT=build/pcm-expected.json`を付けると期待値をJSONへ保存できます。
実機上の生成PCMが一致しても、DMA後の送信波形の正常性を保証するものではありません。

音声のホスト回帰検査は、`src/game_platform.c`からPCM中心値などの定義を型も含めて
取り込みます。unsigned定数`128u`をホストだけ`128`と再定義すると、負の半波がunsigned演算で
壊れる不具合を隠すためです。全256入力値×7音量、負値のfade、2音／4音での正負相殺を検査します。
`python3 tests/native/audio_pcm_regression.py`ではPWMの実ミキサーで単音・4音・事前合成音を
出力し、波形の飽和や全体音量の中心値も確認します。

### 性能比較

標準ROMは保持モデルの3Dを使います。従来のRuby頂点変換と比較する場合だけ、次を生成します。

```sh
ruby tools/build/workflow.rb checks --compare-3d
```

出力は`build/tests/hardware-suite-comparison/`、メニューはHARDWARE COMPAREです。
標準と比較版の識別はログの`profile=standard/comparison`で行います。
同じボード、ROM、素材、描画・音声設定で、一度に1つの設定だけを変えて比較します。
フレーム時間の読み方とビルド設定は[性能メモ](performance-notes.md)を参照してください。

## ホスト回帰テスト

Ruby、Python 3とC/C++コンパイラが必要です。Cの回帰テストには
AddressSanitizer／UndefinedBehaviorSanitizerに対応したコンパイラを使います。
以下はルートから実行する入口です。変更した領域に応じて選びます。

| 対象 | コマンド |
| --- | --- |
| 依存取得・更新 | `ruby tests/native/dependencies_test.rb` |
| VMビルド構成 | `ruby tests/native/vm_profile_test.rb` |
| 統合ROM生成 | `ruby tests/native/checks_test.rb` |
| 音声テストの操作 | `ruby tests/native/hardware_audio_test.rb` |
| 起動・エラー画面と内蔵音源 | `python3 tests/native/boot_screen_test.py`、`node --test tests/web/system-screen.test.js` |
| 標準ビルドの選択肢 | `ruby tests/native/build_options_test.rb` |
| 性能測定の制御 | `ruby tests/native/performance_test.rb` |
| 最小言語構成でのROM実行 | `python3 tests/native/minimum_profile_regression.py` |
| 音声の状態・PCM・I2S・ダイヤル | `python3 tests/native/audio_regression.py`、`audio_pcm_regression.py`、`i2s_regression.py`、`i2s_dma_regression.py`、`audio_dial_regression.py`（同じディレクトリ） |
| 入力・アセットの寿命と回収 | `python3 tests/native/platform_regression.py` |
| フォント・テクスチャ | `python3 tests/native/font_regression.py`、`python3 tests/native/texture_regression.py` |
| 3D描画・モデル・深度 | `python3 tests/native/three_d_regression.py`、`model_regression.py`、`model_shared_regression.py`、`depth_regression.py`、`triangle_regression.py`（同じディレクトリ） |
| Webコンポーネント | `ruby tests/web/components_test.rb` |
| ドット絵・素材変換 | `node --test tests/web/pixel-art-model.test.js` |

`minimum_profile_regression.py`は、PicoRubyと同じmrubyソースからホストVMを作り、
標準・比較ROMをコンパイルして実行します。表示・音声・入力は代役なので実機確認も必要です。
`model_regression.py`はブラウザ比較用の`build/tests/model-cases.json`も生成します。
旧版比較を行うテストはGit履歴を参照するため、通常のcloneを使ってください。
shallow cloneでは比較元コミットの取得が必要です。

## ブラウザ描画の回帰テスト

旧版比較に必要なデータはGit履歴から`build/`へ生成します。固定コミットは各テストの
比較基準です。過去のUF2や開発者のローカルファイルを用意する必要はありません。

```sh
mkdir -p build/tests/font-baseline
git show 219f244:emulator/emulator.js > build/tests/canvas-baseline.js
git show 692f2c0:emulator/emulator.js > build/tests/3d-baseline.js
git show 41a47ed:emulator/emulator.js > build/tests/triangle-baseline.js
git show 6acee9e:emulator/emulator.js > build/tests/model-shared-baseline.js
git show 8829082:emulator/emulator.js > build/tests/font-baseline/emulator.js
ruby tools/fixtures/generate_font_checks.rb build/tests/font-fixtures
python3 tests/native/model_regression.py
python3 -m http.server 8080 --bind 127.0.0.1
```

| ページ | 確認対象 |
| --- | --- |
| [Canvas batching](http://localhost:8080/tests/web/canvas-batching.html) | 3D反映と2Dとの描画順 |
| [3D regression](http://localhost:8080/tests/web/three-d-regression.html) | クリップと投影キャッシュ |
| [Triangle scans](http://localhost:8080/tests/web/triangle-scans.html) | 三角形の走査方式 |
| [Model sharing](http://localhost:8080/tests/web/model-shared.html) | 面と線の準備結果の共有 |
| [Model rendering](http://localhost:8080/tests/web/model-rendering.html) | CとJavaScriptのモデル変換・描画 |
| [Font rendering](http://localhost:8080/tests/web/font-rendering.html) | PBF・文字キャッシュ・クリップ |
| [Audio dial](http://localhost:8080/tests/web/audio-dial.html) | 全ADC値の変換、UI、模擬Web Audio |

各ページのPASSとコンソールに例外がないことを確認します。これらは描画やブリッジの
検査なので、通常のWASM契約テストも実行します。

## 失敗したとき

使用ボード、ソースのコミット、ビルドコマンド、最後のログと例外を記録します。
実機ではPicoRuby版、`STACK`、`VM: profile`、ROMサイズとFNV-1a、
最後の`[CHECK]`／`[CONTRACT]`を残します。ROM生成時と起動時のFNV-1aを照合すると、
SDへコピーした内容を確認できます。
ブラウザではURL、コンソール例外、表示・入力・音声の状態を記録します。
再起動、SDアクセス、描画と音声の併用、連続稼働も実機で確認してください。

## I2Sの連続転送を確認する

```sh
python3 tests/native/audio_regression.py
python3 tests/native/i2s_dma_regression.py
python3 tests/native/i2s_regression.py
```

既存のPWM／I2S単独DMAに加え、実際のチェーン用C関数をDMA・PIOの模擬レジスタで実行します。
ホスト側はW1C（書き込んだビットをクリア）の振る舞いをC++で再現し、1000回の自動切替、
遅延したIRQ、補充中の期限超過、read ring設定、活動中バッファの保護、EOF、両チャネルの
停止・再開と診断値を確認します。最終バッファ再生中に新しいBGMを登録する境界も検査します。
PCM生成とPIOのビット列は既存の回帰で別に確認します。

実機では標準I2SのUF2と同じROM・素材・配線・外部電源を使います。
AUDIO / DIALのBeep・4音・BGMを小音量で確認し、STARTで止めて`AUDIO: I2S stats`を記録します。
FIFO停止や補充遅延が0でも、配線・アンプ・スピーカー等に起因する音質問題は別に切り分けます。

### WeActの電池警告

[配線](hardware.md#電池残量のled警告weact単3アルカリ2本)を確認し、可変電源または
電池の負荷時電圧をテスターで測って、約2.2V以下で3秒後にLEDが点滅すること、約2.4V以上で
3秒後にLEDが点灯に戻ることを確認します。画面の連続描画および音量ダイヤルとの併用も
確認します。値は3.3V基準・1:2分圧の公称値です。
