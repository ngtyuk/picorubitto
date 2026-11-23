# Waveshare RP2350-PiZero対応ロードマップ

8MiBのPSRAMを増設したWaveshare RP2350-PiZeroで、既存の `game.mrb` を内蔵microSDから
実行する。外付けST7789V版に加え、mini HDMI端子に映像を出すDVI検証版をビルドできる。
入力はGPIOボタン、音声は外付けアンプで検証する。PIO-USBコントローラーは後続段階で扱う。

## 基板仕様とPicoRubittoでの扱い

| 項目 | 回路・制約 | 初期構成 |
| --- | --- | --- |
| MCU | RP2350B、デュアルコア、内蔵SRAM 520KiB | Arm Cortex-M33、定格150MHz |
| フラッシュ | 基板上の16MiB QSPIフラッシュ、U4 | Pico SDK標準ボード定義の起動設定 |
| PSRAM | U1の増設ランド、3.3V、CS1=GP47。データとクロックをフラッシュと共用 | 8MiB、容量はSDKがチップIDから検出。QPI上限75MHz |
| microSD | SPI1でSCK=GP30、MOSI=GP31、MISO=GP40、ソフトウェアCS=GP43。4-bit SDIOのDAT0～3はGP40～43 | 既存FatFsのSPIモード。400kHzで初期化し、25MHzを上限にマウント |
| DVI | mini HDMIのデータペアはGP32–33（D2）、GP34–35（D1）、GP36–37（D0）、クロックはGP38–39 | `--display-output dvi` の試験ビルドでPIO/DMAによる640×480・60Hz映像を出力 |
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
| 3. 内蔵DVIで表示 | [検証用UF2とROM](#dvi検証用uf2とrom)で320×240 RGB565を640×480で2倍表示 | 色・画素位置・フレーム境界を確認し、SDアクセス、音声、2D/3Dの同時実行で映像が維持される |
| 4. USB入力と公開判定 | PIO-USBホストでHIDゲームパッドの入力を既存のボタン状態へ割り当てる | 抜き差し、長押しと同時押し、DVI・I2S・SDとの同時動作を再現し、対応機器と設定を文書化 |

段階1の初期構成と段階3の試験ビルドを実装済み。増設済み基板・モニターでの実機確認を行って
段階2と3の合格を判定する。SPIのSD読み込みがゲームのボトルネックと計測された場合は、
段階2の結果を基準に4-bit SDIOを別途評価する。

### DVIの実装構成

ボード側の映像配線はGP32〜39にあり、RP2350のHSTXはGP12〜19に出る。
試験ビルドはCore 1のSIO TMDSエンコーダ、GPIOベース16のPIO1を3ステートマシン、
DMAを6チャネルとDMA IRQ1、画素クロックにGP38/39のPWMを使う。
PIO0を使う外付けI2Sと組み合わせてビルドできる。mini HDMIからはDVI映像を出力し、
音声は従来の外付けPWM/I2Sへ出す。

DVIには25.2MHzの画素クロックが必要なため、検証版はCPUを252MHz、コア電圧下限を1.20Vに
設定する。252MHzはRP2350の定格150MHzを超える。FlashとPSRAMは63MHzになるよう分周し、
冷間起動でPSRAM全域とストレージを確認する。切り分けには定格150MHzのPiZero TFT版も使える。
出典は[Waveshare DVIデモ](https://www.waveshare.com/wiki/RP2350-PiZero)と
[PicoDVIのソース](https://github.com/Wren6991/PicoDVI)。

### DVI検証用UF2とROM

```sh
ruby tools/build/workflow.rb setup
ruby tools/build/workflow.rb engine --board waveshare_rp2350_pizero \
  --display-output dvi --clock-diagnostics
ruby tools/build/workflow.rb checks --dvi-only
```

1. 8MiB PSRAM増設済みPiZeroへ
   `build/firmware-release-waveshare-rp2350-pizero-dvi-clock-check/pico_ruby_gaming.uf2` を書き込む。
2. `build/tests/dvi-check/game.mrb` を検証用のFAT32カード直下へコピーし、内蔵スロットへ挿す。
   素材ファイルは不要。
3. mini HDMIとモニターを接続して冷間起動する。USB CDCで `CLOCK: profile=pizero-dvi`、
   `PSRAM: available=1 size=8388608`、`CLOCK TEST: PASS`、
   `DVI: 640x480p60`、SDのマウント成功、`[DVI] RGB565 COLORS AUTO` を順に確認する。
   モニターには起動画面、続いてカラーバーが出る。

起動直後は約5秒ごとに下表の順で切り替わる。Aで次、Bで前へ移動するとそのパターンを保持する。
STARTで自動切替と保持を切り替える。USBシリアルに `[DVI]` とパターン名が出る。

| パターン | モニターで見る点 |
| --- | --- |
| RGB565 COLORS | 左から白・黄・シアン・緑・マゼンタ・赤・青・黒。下の赤・緑・青・白の階調が左の黒から右へ明るくなる |
| BORDER AND GRID | 白い1ピクセル枠が全辺で見える。左上が赤、右上が緑、左下が青、右下が黄。格子と中央の十字に位置ずれがない |
| 1 PX PATTERNS | 縦縞・横縞・市松模様で白黒が交互に並ぶ。DVIの2倍表示では元の1ピクセルが出力の2×2ピクセルになるため、確認時はモニター側も等倍表示にする |
| MOTION | 60fpsを目標にフレーム全体を書き換える。白いバーが左右に往復し、上下の緑マーカーと途中の白線が同じ位置で揃う |

周期的な赤い走査線が出る場合はTMDSデータの供給遅れ、`DVI: frame handoff timed out` は
Core 1へのフレーム引き渡しが進んでいないことを示す。ログを保存し、別のモニターや
Waveshareのベンダーデモ、定格TFT版で接続とPSRAMの動作を切り分ける。
