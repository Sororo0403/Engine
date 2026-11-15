# 第5章 入力とアクション

目次で **第5章 入力とアクション** を開きます。キーの長押し、フォーカス喪失、Wiiリモコンの割り当て、軸の閾値、アクションの押下が題材です。

## デバイスの状態を受け取る

[WindowsInputBackend.cpp](../../Engine/src/platform/windows/WindowsInputBackend.cpp) は Microsoft GameInput の集約デバイスからキーボードとマウスを読み、ゲームパッドのボタン・軸も共通の表現に変換します。Wiiリモコンは [WindowsWiiRemote.cpp](../../Engine/src/platform/windows/WindowsWiiRemote.cpp) で取得し、接続、Aボタン、振りを `InputState` に渡します。[InputSystem.cpp](../../Engine/src/input/InputSystem.cpp) はバックエンドを所有し、非アクティブ時に各デバイスの状態をクリアします。取得に失敗した場合は状態をクリアして失敗を返します。

[Keyboard.cpp](../../Engine/src/input/Keyboard.cpp) の `Update` は現在値を前回値へ移してから新しい値を取り込みます。`IsDown` は現在値、`IsPressed` と `IsReleased` は前回値との差を使います。同じキーを長押ししても、押し始めのフレームだけ `IsPressed` が真になります。

## 操作名にまとめる

[GameActionBinding.cpp](../../Engine/src/gameplay/GameActionBinding.cpp) は設定からキーやデバイスの種類を解決します。[GameActionReader.cpp](../../Engine/src/gameplay/GameActionReader.cpp) は同じ操作名に属する複数のデバイスをまとめ、押下・解放と強さを返します。例えば、同じ操作を実行するキーとゲームパッドを交互に押しても、いずれかが押された状態を保っていれば操作全体の押下は続きます。

軸は0.6で押下し、押下後は0.4以下で解放します。フォーカスを取り戻した直後は、その間押されていた入力が一度離されるまで新しい押下を受け付けません。接続が切れた入力も取り消しとして扱います。

移動・ジャンプなどの固定操作は [CharacterInputReader.cpp](../../Engine/src/gameplay/CharacterInputReader.cpp)、Canvasの決定・移動は [UIInputReader.cpp](../../Engine/src/ui/UIInputReader.cpp) で、それぞれの処理が必要とする形に変換します。
