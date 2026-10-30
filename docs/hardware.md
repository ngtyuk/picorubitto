# 実機の接続と起動

対応ボードはPimoroni Pico Plus 2（RP2350）とWeAct RP2350B Core Board（8MB PSRAM）です。
UF2のビルド環境は[セットアップ](getting-started.md)、ゲームROMの生成は
[ゲーム作成](game-development.md)を参照してください。microSDカードはFAT32で用意します。

## 配線

Pimoroni Pico Plus 2、WeAct RP2350B Core Boardともに、TFT、microSD、操作ボタン、
アンプを別途接続します。周辺機器のGPIO割り当ては両ボードで共通です。
次の表のGP番号はGPIO番号で、基板上の物理ピン番号ではありません。
各モジュールは3.3Vロジックを使い、GNDを共通にします。

### 共通の信号配線

| 機能 | GPIO |
| --- | ---: |
| 十字キー ↑ / ↓ / ← / → | GP2 / GP3 / GP4 / GP5 |
| A / B | GP6 / GP7 |
| L / R / START / SELECT | GP9 / GP12 / GP14 / GP16 |
| SD MISO / CLK / MOSI / CS | GP8 / GP10 / GP11 / GP13 |
| TFT SCK / MOSI / RST / DC / CS | GP18 / GP19 / GP20 / GP21 / GP22 |
| アンプへのPWM音声入力 | GP15 |

ボタンはGPIOとGNDの間に接続します。入力は内部プルアップで、押すとLOWになります。
TFTはST7789V、240×320の4線式SPIパネルを横向きに使います。ゲームから見える画面は
320×240です。

GP15はアンプの入力へ接続してください。スピーカーをGPIOへ直接接続してはいけません。
PAM8302を使う場合は、モジュールの電源、GND、入力カップリング、SD/SHDNの説明に従います。
音割れの切り分けでは、入力レベル・音量調整・PWM入力の平滑化・入力カップリング・電源の安定性と
スピーカー負荷を確認します。[PAM8302Aのメーカー資料](https://www.diodes.com/datasheet/download/PAM8302A.pdf)は
ICの利得を23.5dBとし、入力カップリングと電源デカップリングの条件を説明しています。
モジュールの実際の入力回路・利得は製品ごとに確認してください。
「Filterless」はアンプ出力からスピーカーへの説明です。GPIOのPWMをアナログ入力へ渡す経路は別に扱います。
[統合ROMの音量・実4音／事前合成比較](compatibility-testing.md#音声の確認)も利用できます。

### MAX98357AのI2S出力

`engine --audio-output i2s`でMAX98357A向けのUF2を生成できます。既定は従来のPWMです。
I2S版ではGP26〜GP28を使い、TFT・SD・ボタン・PSRAMの割り当てを維持します。

| MAX98357Aモジュール | 両ボード共通の接続 |
| --- | --- |
| BCLK | GP26 |
| LRC / LRCLK / WS | GP27 |
| DIN | GP28 |
| GND | ボードのGNDへ直接接続し、電源とも共通GND |
| VIN | モジュール仕様に合う電源（ICは2.5〜5.5V） |
| SPK+ / SPK- | スピーカーの両端 |
| SD / EN・GAIN | モジュールの説明に従い、再生有効・適切な利得に設定 |

[MAX98357Aメーカー資料](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)に
従い、標準I2S・16-bit・48kHz、BCLK 1.536MHz、MCLKなしで送ります。左右は同じモノラル音です。
元のWAVは従来の8-bit PCMで、22.05kHz等から48kHzへ内部で線形補間します。
GPIOは3.3Vのデジタル信号です。スピーカー電流を含む電源容量とモジュールのデカップリングも確認します。
起動時は`AUDIO: I2S BCLK=GP26 WS=GP27 DATA=GP28 rate=48000 bits=16 ... chain=1 peer=...`が目印です。
I2SはDMAを2チャネル使い、自動でバッファを切り替えます。
再生を止めると`AUDIO: I2S stats`が出るため、
断続音の切り分けでは`stalls`・`late_irqs`・`late_refills`・`refill_max_us`を確認してください。

TFT転送に連動してノイズが出る場合は、信号線の距離に加えてGNDの戻り経路も確認します。
共通GNDを維持したまま、アンプとボード間のGNDを短くし、スピーカーの電流が流れる配線を
TFTのGNDと長く共有しない配置で比較します。アンプ直近のVIN–GNDのバイパス容量も確認します。
メーカー資料のPower-Supply Input／Layout and Groundingでは、10µFを電源端子近くに置き、
長い電源配線では追加容量を設けること、低インピーダンスのGNDを推奨しています。
実装済み容量はモジュールごとに確認してください。TFTクロックの低下だけで改善するとは限りません。

WeActでの実機確認（2026-10-27）では、電源を含めてGNDが共通でもLIVE表示中に断続音が発生し、
アンプのGNDからWeActの空きGND端子へ配線を追加すると、LIVEでも断続音が聞こえなくなりました。
共通GNDになっていることに加え、アンプとボードを結ぶ直接の戻り経路を確保してください。
この構成で残っていたノイズは、GND経路の影響が主因と判断しています。

```sh
ruby tools/build/workflow.rb engine --audio-output i2s
ruby tools/build/workflow.rb engine --board weact_studio_rp2350b_core --audio-output i2s
```

出力先は`build/firmware-release-audio-i2s/pico_ruby_gaming.uf2`と
`build/firmware-release-weact-studio-rp2350b-core-audio-i2s/pico_ruby_gaming.uf2`です。
PWM版は通常の出力先です。既存のAPI14 ROMと音声素材を継続利用できます。
ピンを変える場合は`--i2s-bclk 27 --i2s-data 26`でBCLK=27・WS=28・DIN=26にできます。
時計の2本はGP26/27またはGP27/28、DINはGP17・GP26〜28から時計と重ならないピンを選びます。
既定と異なるピンを明示した版は出力先に`-bclk27-data26`などが付きます。起動ログと配線を揃えてください。

### Pimoroni Pico Plus 2の場合

対象はRP2350搭載のPimoroni Pico Plus 2です。基板のGP表記を確認し、上の表どおりに
外周のGPIO端子へ接続します。ピン配置や回路図は
[Pimoroni公式製品ページ](https://shop.pimoroni.com/products/pimoroni-pico-plus-2)を参照してください。

- TFTはGP18〜GP22、microSDはGP8 / GP10 / GP11 / GP13へ接続します。
  TFTのSDA / DIN表記はMOSI、SCL / CLK表記はSCKに対応します。
- ボタンの片側を表のGPIO、もう片側をGNDへ接続します。オンボードのBOOTボタンは
  ゲームのSTART / SELECTボタンの代わりにはなりません。
- アンプの音声入力はGP15へ接続します。電源は各モジュールの仕様に合わせ、
  3.3V対応モジュールの電源には3V3(OUT)、GNDには基板のGND端子を使います。
  TFTのバックライト電源も、使用するモジュールの仕様に従って接続します。
- オンボードPSRAMはGP47をCSに使用します。PSRAMの追加配線は不要です。
  WeAct向けのGP0予約とは異なります。
- Qw/STとSP/CEは、上の表の信号配線の代わりにはなりません。特にSP/CEのSPI端子は
  このエンジンのTFT・SD用GPIOとは異なるため、表のGPIO端子を使ってください。

### WeAct RP2350B Core Boardの場合

基板のピン配置は
[WeAct公式資料](https://github.com/WeActStudio/WeActStudio.RP2350BCoreBoard)で確認し、
共通の信号配線表に従って接続します。GP0はオンボードPSRAMのCS1に使うため、
他の周辺機器へ接続しないでください。PSRAMの追加配線は不要です。

## 電池残量のLED警告（WeAct、単3アルカリ2本）

電池電圧を昇圧前に測り、LEDで電池の状態を知らせます。ゲームROMの変更は不要です。
警告は目安で、残り時間を示しません。
WeAct専用の任意機能です。未配線で有効にしないでください。
GP24に接続したLEDは通常時に点灯し、同じ電圧低下判定で0.5秒ごとに点滅します。

```text
電池＋ ── 電源スイッチ ──┬── MT3608 IN+
                           └── 100kΩ ──┬── GP40 / ADC0
                                        ├── 100kΩ ── GND
                                        └── 100nF ─── GND
電池− ─────────────────────┬── MT3608 IN−
                           └── WeAct GND
MT3608 OUT+（5Vに調整） ───── ボードと周辺機器の適切な電源入力
MT3608 OUT− ───────────────── WeAct GND
WeAct GP24 ── 1kΩ ── LEDの＋（長い足）
                       LEDの−（短い足） ── WeAct GND
```

LEDは一般的な赤色LEDを想定し、1kΩを直列に入れます。GP24は他の用途と共用しないで
ください。LEDを省略すると電池の状態は通知されません。起動直後は測定が確定するまで点灯し、電圧低下が
約3秒続くと点滅します。

100kΩは各1%、100nFはセラミックの目安です。GNDを共通にし、GP40へは分圧の
中点だけを接続します。5V出力をGP40へ接続しないでください。スイッチは電池＋から
分岐する前に置き、オフ時に分圧回路も切り離します。電池ケースの極性、ボードの電源入力、
MT3608の入出力端子は製品の表示と回路図で確認し、接続前に出力を5Vに調整してください。
USBと外部5Vを同時接続する場合は、ボードと電源回路が逆流を防げることを確認します。

分圧比1:2、ADC基準3.3Vとして、電池電圧が約2.2V以下の状態が約3秒続くとLEDが
点滅し、約2.4V以上が約3秒続くと点灯に戻ります。電池と負荷、ADC基準電圧や抵抗の個体差により
実際の電圧はずれます。音量や画面の負荷が大きい場面で電池電圧とMT3608出力を
テスターで確認し、警告より先にリセットしないか実機で試してください。電池未接続で
USBから起動するとLEDが点滅します。LEDは画面更新とは独立に動作します（入力タイマーが利用できない場合は
ゲームのフレーム更新に依存します）。昇圧回路の入力動作範囲と必要な電流も確認してください。

```sh
ruby tools/build/workflow.rb engine --board weact_studio_rp2350b_core --battery-warning
# I2S、音量ダイヤル、最大音量50%を併用する例
ruby tools/build/workflow.rb engine --board weact_studio_rp2350b_core \
  --battery-warning --audio-output i2s --volume-knob --i2s-data 28 --audio-max-gain 50
```

前者のUF2は`build/firmware-release-weact-studio-rp2350b-core-battery-warning/pico_ruby_gaming.uf2`、
後者は`build/firmware-release-weact-studio-rp2350b-core-audio-i2s-battery-warning-volume-knob-audio-gain50-bclk26-data28/pico_ruby_gaming.uf2`です。
GP40はADC0で、音量ダイヤルのGP42/ADC2とは別です。起動ログの`BATTERY: GP40`を確認します。
WASMには物理電池やLEDがないため、この機能は動作しません。

## ダイヤル音量（ADC）

10kΩ程度のリニアポテンショメータを使い、両端を**3V3(OUT)とGND**、中央の摺動端子を
次の端子へ接続します。ADCへ入れる電圧は0〜3.3Vです。回転方向が逆なら両端を入れ替えます。

| ボード | 中央端子の接続先 | SDK上のGPIO・ADC |
| --- | --- | --- |
| Pico Plus 2 | ヘッダのGP28 / A2 | GP42 / ADC2 |
| WeAct RP2350B Core | GP42 / ADC2 | GP42 / ADC2 |

Pico Plus 2のGP26/27/28はADC用GP40/41/42と接続されています。
[公式回路図へのリンクがある製品ページ](https://shop.pimoroni.com/products/pimoroni-pico-plus-2)と
[Pimoroniの接続説明](https://forums.pimoroni.com/t/why-do-pimoroni-pico-2-boards-have-1k-resistors-on-some-all-of-the-adc-capable-gpio/28781)も参照してください。
この機能ではデジタル側GP28を高インピーダンスにし、ADC側GP42を読みます。
WeActではGP28とGP42は別の端子なので、GP42を使います。

```sh
# 現在のPWMアンプ + ダイヤル
ruby tools/build/workflow.rb engine --volume-knob
# MAX98357A + ダイヤル
ruby tools/build/workflow.rb engine --audio-output i2s --volume-knob
# WeActは上記に --board weact_studio_rp2350b_core を追加
```

ダイヤル併用I2S版の配線は**BCLK=GP26、LRC/WS=GP27、DIN=GP17**です。
Pico PlusではヘッダGP28とADC GP42が接続されているため、ダイヤルとDINの共用を拒否します。
WeActではGP28とADC GP42は独立しているため、次の指定でDINをGP28へ移せます。
メーカーの[ピン配置図](https://github.com/WeActStudio/WeActStudio.RP2350BCoreBoard/blob/main/HDK/RP2350B_PINOUT.png)では、
GP28はTFTのGP18〜22と反対側のヘッダで、BCLK/WSのGP26/27の近くにあります。

```sh
ruby tools/build/workflow.rb engine --board weact_studio_rp2350b_core \
  --audio-output i2s --volume-knob --i2s-data 28
```

この版は**BCLK=GP26、LRC/WS=GP27、DIN=GP28、ダイヤル中央=GP42**です。
既定のGP17版は維持し、GP28版は
`build/firmware-release-weact-studio-rp2350b-core-audio-i2s-volume-knob-bclk26-data28/`へ生成します。
ダイヤルなしI2Sの既定配置はGP26/27/28のままです。明示的に既定と異なるI2Sピンを選んだ版は、
出力先へ`-bclk番号-data番号`を付け、既定版の上書きを避けます。

出力先はPWMで`build/firmware-release-volume-knob/`、I2Sで
`build/firmware-release-audio-i2s-volume-knob/`です。WeActは`firmware-release`直後に
`-weact-studio-rp2350b-core`が付きます。音源ごとの音量に全体音量が掛かり、
検証ROMの`AUDIO / DIAL`でADC値と音の追従を確認します。
音量変換は下端128以下を無音、上端3800以上を最大とします。両端の個体差を吸収する固定範囲で、
実際のADC値（0〜4095）の表示は維持します。
通常UF2はダイヤルを無効にし、全体音量255で動作します。

`--audio-max-gain 50`を追加すると、ダイヤル最大時の出力PCM振幅を既定の50%にします。
0〜100の整数を指定でき、省略時は100です。PWM・I2Sとダイヤル有無に共通で、
音量の読み取り値は0〜255のままです。起動ログは`AUDIO: max_gain=50%`です。
設定違いの既定の出力先には`-audio-gain50`などが付きます。明示的な`--firmware-dir`を
共用すると同じ出力先に上書きします。振幅半分は約−6dBで、体感音量やスピーカーの
定格電力を正確に半分にするものではありません。

WeActでDIN=GP28、ダイヤル有効、最大ゲイン50%を組み合わせる場合は、次のように
`--i2s-data 28`と`--audio-max-gain 50`を両方指定します。省略するとDINはGP17、
最大ゲインは100%になります。

```sh
ruby tools/build/workflow.rb engine --board weact_studio_rp2350b_core \
  --audio-output i2s --volume-knob --i2s-data 28 --audio-max-gain 50
```

配線は**BCLK=GP26、WS=GP27、DIN=GP28、ダイヤル中央=GP42**です。UF2は
`build/firmware-release-weact-studio-rp2350b-core-audio-i2s-volume-knob-audio-gain50-bclk26-data28/pico_ruby_gaming.uf2`、
WASMランタイムは`emulator/picoruby/`に生成されます。起動ログのI2S設定と
`AUDIO: max_gain=50%`を確認してください。

## UF2を書き込む

起動すると、ルビー色のTFT画面の中央に大きく「PICORUBITTO」と表示し、SDに依存しない短い起動音を再生します。
正常起動では画面を約3秒以上表示し、その間の音声・SD初期化の時間も含みます。
SD初期化などのエラーは待たずに表示します。
音量ダイヤルと`--audio-max-gain`が起動音にも適用されます。音声初期化に失敗した場合も
起動を続けます。PSRAMやTFTの初期化前には画面を表示できません。

エラー画面の`E01`は起動処理、`E02`はSD／ROM、`E03`はVM、`E04`はゲーム実行中の
エラーです。詳細はUSBシリアルに出力します。画面の`RESTART TO RETRY`はボードの再起動を
意味します。

1. 対象ボードをBOOTSELモードでUSB接続する。
2. Pico Plus 2では`build/firmware-release/pico_ruby_gaming.uf2`、WeAct RP2350B Core Boardでは
   `build/firmware-release-weact-studio-rp2350b-core/pico_ruby_gaming.uf2`を表示されたドライブへ
   コピーする。
3. `game.mrb`と`assets/`をSDカードのルートへコピーする。
4. SDカードを挿入して再起動する。

起動に失敗した場合は、USB CDCシリアルのログ、SDカードの形式、配線を確認します。
