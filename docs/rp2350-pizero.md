# Waveshare RP2350-PiZero対応ロードマップ

8MiBのPSRAMを増設したWaveshare RP2350-PiZeroで、既存の `game.mrb` を内蔵microSDから
実行する。初期構成は外付けST7789V、GPIOボタン、外付けアンプ。mini HDMI端子からのDVI映像と
PIO-USB経由のコントローラーは、起動とストレージの実機確認後に順に検証する。

## 基板仕様とPicoRubittoでの扱い

| 項目 | 回路・制約 | 初期構成 |
| --- | --- | --- |
| MCU | RP2350B、デュアルコア、内蔵SRAM 520KiB | Arm Cortex-M33、定格150MHz |
| フラッシュ | 基板上の16MiB QSPIフラッシュ、U4 | Pico SDK標準ボード定義の起動設定 |
| PSRAM | U1の増設ランド、3.3V、CS1=GP47。データとクロックをフラッシュと共用 | 8MiB、容量はSDKがチップIDから検出。QPI上限75MHz |
| microSD | SPI1でSCK=GP30、MOSI=GP31、MISO=GP40、ソフトウェアCS=GP43。4-bit SDIOのDAT0～3はGP40～43 | 既存FatFsのSPIモード。400kHzで初期化し、25MHzを上限にマウント |
| DVI | mini HDMIのデータペアはGP32–33（D2）、GP34–35（D1）、GP36–37（D0）、クロックはGP38–39 | 外付けSPI TFTで先行検証。オンボードDVIはPIO/DMAで実装予定 |
| USB | J4はRP2350のUSB、J2はGP28/29のPIO-USB | J4でUF2書き込み・USB CDC |
| 操作・音声 | 40ピンヘッダーからGPIO。GP42はmicroSDに接続 | GPIOボタン、PWM GP15、またはI2S GP26/27/17 |
| 電源 | USB給電、基板上にバッテリーコネクタと充電回路 | 初回はUSB給電で検証 |

基板の根拠は[Waveshare回路図](https://files.waveshare.com/wiki/RP2350-PiZero/RP2350-PiZero.pdf)と
[Waveshare Wiki](https://www.waveshare.com/wiki/RP2350-PiZero)。
GPIOの機能割り当ては[RP2350データシート](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
のGPIO bank 0 function selectionを参照。40ピンの物理番号は[実機ガイド](hardware.md#waveshare-rp2350-pizeroの場合)に掲載。

## PSRAMの増設

候補はAP Memoryの **APS6404L-3SQR-SN**（64Mbit＝8MiB、2.7〜3.6V、SOP-8 150mil）。
購入・実装前に手元の基板リビジョンでU1のパッド寸法、1番ピンの向き、R39（CSのプルアップ）と
C33（電源のデカップリング）の実装状態を照合する。基板の電源・USB・バッテリーを外した状態で
作業し、通電前に電源とGNDの短絡がないことを確認する。フットプリントと端子番号は
[AP Memoryデータシート](https://www.apmemory.com/en/downloadFiles/032411212009597427)を参照。

APS6404Lのリニアバースト上限は84MHz。初期UF2ではQPIの上限を75MHzに固定し、
CS1はGP47だけを使用する。容量検出後、ブート時に8MiB以上あることと先頭・末尾の
読み書きを確認する。診断UF2はさらに、VM起動前にキャッシュを迂回して
8MiB全域を2通りのアドレスパターンで検査する。PSRAM上に展開するゲームデータは
起動のたびに作り直され、セーブはmicroSDに保存する。

## 対応ロードマップ

| 段階 | 作業 | 完了条件 |
| --- | --- | --- |
| 1. 初期ファームウェア | SDKボード定義、PSRAM検出、内蔵SD、TFT・GPIO、PWM/I2Sをワークフローへ接続 | PiZeroのUF2とWASMをビルドし、WASM契約テストと実機ROM生成を通す |
| 2. 実機で起動と保存を確認 | PSRAMを実装した基板へ診断UF2と統合ROMを入れる | 冷間起動で8MiB認識と全域診断に合格し、画面、全ボタン、音声、SD読み書き、電源再投入後のSAVE復元を確認 |
| 3. 内蔵DVIで表示 | ベンダーデモでモニター出力を確認後、PIO/DMAの映像バックエンドを試作。[DVI検証ROM](#dvi検証rom)の320×240 RGB565を640×480で2倍表示 | 色・画素位置・フレーム境界を確認し、SDアクセス、音声、2D/3Dの同時実行で映像が維持される |
| 4. USB入力と公開判定 | PIO-USBホストでHIDゲームパッドの入力を既存のボタン状態へ割り当てる | 抜き差し、長押しと同時押し、DVI・I2S・SDとの同時動作を再現し、対応機器と設定を文書化 |

段階1の初期構成を実装済み。段階2以降の確認にはPSRAM増設済みの基板、モニター、
コントローラーなど、各段階に対応した機材が必要。SPIのSD読み込みがゲームのボトルネックと
計測された場合は、段階2の結果を基準に4-bit SDIOを別途評価する。

### DVI実装の判断点

ボード側の映像配線はGP32〜39にあり、RP2350のHSTXはGP12〜19に出る。Waveshareが
PicoDVIベースで配布している640×480・60Hzのデモを、基板とモニターの確認に使う。
PicoRubittoへの組み込みではPIOのGPIOベース16を使ってGP32〜39を扱い、DMA・IRQ・
Core 1の使用量を先に測る。I2Sと将来のPIO-USBもPIOを使うため、使用するPIOブロックと
ステートマシンを決めてから併用する。映像の連続転送用にSRAMの走査線バッファを評価し、
PSRAM・Flash・SDへのアクセスと競合する条件でフレーム落ちと音切れを計測する。

PIO方式で映像のビットレートを満たすクロックを選ぶ場合は、PiZero上でPSRAM・Flashの
分周と安定性を改めて検証する。DVIでの最初の音声経路は外付けPWM/I2Sとし、
HDMI音声の扱いは映像とゲームの同時動作を確認した後に判断する。実装の出発点は
[Waveshare DVIデモの説明](https://www.waveshare.com/wiki/RP2350-PiZero)と
[PicoDVIのソース](https://github.com/Wren6991/PicoDVI)。

### DVI検証ROM

```sh
ruby tools/build/workflow.rb checks --dvi-only
```

`build/tests/dvi-check/game.mrb` を検証用のFAT32カード直下に置く。素材ファイルは不要。
DVI実装前は既存のTFT出力で確認できる。mini HDMIでの検証にはDVI対応UF2を使う。

起動直後は約5秒ごとに下表の順で切り替わる。Aで次、Bで前へ移動するとそのパターンを保持する。
STARTで自動切替と保持を切り替える。USBシリアルに `[DVI]` とパターン名が出る。

| パターン | モニターで見る点 |
| --- | --- |
| RGB565 COLORS | 左から白・黄・シアン・緑・マゼンタ・赤・青・黒。下の赤・緑・青・白の階調が左の黒から右へ明るくなる |
| BORDER AND GRID | 白い1ピクセル枠が全辺で見える。左上が赤、右上が緑、左下が青、右下が黄。格子と中央の十字に位置ずれがない |
| 1 PX PATTERNS | 縦縞・横縞・市松模様で白黒が交互に並ぶ。DVIの2倍表示では元の1ピクセルが出力の2×2ピクセルになるため、確認時はモニター側も等倍表示にする |
| MOTION | 60fpsを目標にフレーム全体を書き換える。白いバーが左右に往復し、上下の緑マーカーと途中の白線が同じ位置で揃う |
