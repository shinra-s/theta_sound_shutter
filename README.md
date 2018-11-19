# 音量感知自動シャッター for RICOH THETA
## 概要
theta_sound_shutter は，マイクから一定の音量を感知して自動でカメラのシャッターを切る ESP32 用のプログラムである．
使用するカメラは RICOH THETA という全天球カメラであり，ESP32 とカメラとは Wi-Fi で接続する．

## 使用方法
ここで，使用する環境において，esp-idf を用いた ESP32 の開発環境が整っているものとする．
また，以下の手順はクローンした本リポジトリのディレクトリ内で行うものとする．
+ `make menuconfig`を実行し，設定メニューを開き，以下の項目の設定を行う．
  + Serial flasher config > Default serial port を選択し，ESP32 の接続するシリアルポートのデバイス名を入力
  + Example Configuration > WiFi SSID を選択し，RICOH THETA の Wi-FiのSSIDを入力
  + Example Configuration > WiFi Password を選択し，RICOH THETA の Wi-Fiのパスワードを入力
+  `make flash`を実行し，ビルドする．

## 処理内容
本プログラムは，ESP32 の持つ 2 つのコアにそれぞれ以下のタスクを割当て実行している．
+ マイクから音量を取得し，しきい値を超えていればフラグを立てる．
+ フラグが立っていれば，カメラのシャッターを切る．
  + カメラとは Wi-Fi で接続し，HTTP リクエストを送信することでカメラを操作する．

## 配線図
![sound_shutter_circuit](https://user-images.githubusercontent.com/18206832/48038382-ad701700-e1b3-11e8-8901-628451cbf498.png)

## TODO
+ ソースコードの整理
  + 現在はサンプルのソースコードのつぎはぎ状態
  + 使用している音声モジュールはデジタル信号を処理しているにもかかわらず，ソースコードはアナログ信号として処理している
