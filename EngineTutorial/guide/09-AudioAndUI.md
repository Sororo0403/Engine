# 第9章 音と画面UI

目次で **第9章 音と画面UI** を開きます。空間音声のチャンネル、距離による音量、Canvasの幅、同梱フォント、ボタンの押下と解放を扱います。

**実装索引を開く** から、音声の読み込み・再生、Canvasの配置と入力、画像・文字の描画、UIの編集と保存に関わる58ファイルを確認できます。教材の目次からは **実装索引 → 第9章** を選びます。

## 音の再生

[AudioClipManager.cpp](../../Engine/src/audio/AudioClipManager.cpp) はクリップを読み込んで共有します。[SceneAudioPlayer.cpp](../../Engine/src/scene/SceneAudioPlayer.cpp) はシーン上の音源・リスナーと再生中の音を対応させます。[AudioSystem.cpp](../../Engine/src/audio/AudioSystem.cpp) はDirectXTKのオーディオ機能を使って再生を管理し、空間音声の再生前にはモノラルの素材と位置設定を検証します。

[SpatialAudio.cpp](../../Engine/src/audio/SpatialAudio.cpp) は音源、リスナーの位置と向き、音量の距離曲線をX3DAudioへ渡します。最小距離までは音量係数を1に保ち、最大距離では0にします。出力チャンネルへの配分は、音が来る方向と出力構成を使って求めます。

## Canvasの配置と描画

[CanvasLayout.cpp](../../Engine/src/ui/CanvasLayout.cpp) は親の矩形・アンカー・ピボットから子の画面上の矩形を求めます。アンカーの差に親の幅を掛けた値は、親のサイズに連動する幅です。最終的な幅には、画面倍率を反映した `sizeDelta` も加わります。

[CanvasRenderer.cpp](../../Engine/src/graphics/ui/CanvasRenderer.cpp) はレイアウトを元に画像や文字を描きます。[TextRasterizer.cpp](../../Engine/src/platform/windows/TextRasterizer.cpp) では同梱したM PLUS 1pのファイルをDirectWriteへ登録し、日本語の文字列も描画用の画像にします。

## クリックの確定

[SceneUI.cpp](../../Engine/src/ui/SceneUI.cpp) は同じCanvasの配置を元にポインター位置とボタンを照合します。押していたボタンと離した位置のボタンが一致するとクリックになります。教材の選択肢は、このクリックがTrigger Actionsへ渡り、Luaにメッセージが届くことで答え合わせを表示します。
