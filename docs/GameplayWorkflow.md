# Editorだけで小さなステージを作る

## 最初の確認

1. `File > New Sample Stage...`を押す。編集内容が未保存なら、通常の新規作成と同じ保存確認が出る。
2. `Hierarchy`で`Floor`、壁、`Player`、`Follow Camera`を選ぶと、通常のTransformとコンポーネントを編集できる。専用のゲームコードは不要。
3. `File > Save As...`からシーンを保存する。開き直してもコンポーネントと追従先は保持される。
4. `Game`パネルの`Play`を押す。Game画像をクリックし、WASDで移動、Spaceでジャンプする。W/Sはワールドの+Z/-Z、D/Aは+X/-X。
5. サンプルの手前（-Z）には壁を置いていない。Sで床から外へ出ると、落下後に開始位置へ戻る。カメラも復帰位置へ切り替わる。
6. `Stop`、またはGameにフォーカスがある状態でEscを押す。編集時の位置・設定・選択・Undo履歴・保存地点を引き継いで制作へ戻る。

Gameが保存済みレイアウトに入っていない場合は、浮いたGameウィンドウをScene中央へドッキングする。`View`のレイアウト初期化でもSceneとGameが同じ中央領域へ配置される。

## 自分で構成する

空のシーンやモデルだけのシーンでもPlayできる。Assetsからモデルを置き、Inspectorの`Add Component`で動かしたい機能を追加する。PrimaryのCameraがない場合は、Playを押した時点のSceneビューのカメラ姿勢で表示する。Character Controllerがなければ入力による移動や重力は実行しない。

| 対象 | コンポーネント | 設定 |
| --- | --- | --- |
| 床・壁 | Box Collider | `Fit Collider to Model`でモデルに合わせ、必要ならCenterとHalf Sizeを編集する |
| 主人公 | Box Collider + Character Controller | 衝突形状、移動速度、ジャンプ速度、重力、落下の最高速度 |
| 主人公 | Respawn | `Fall Below Y`より原点が下がると復帰。`Use Start Position`でPlay開始時の位置、解除すると指定位置へ戻る |
| カメラ用の空物体 | Camera | Primary、縦視野角（度）、Near Clip、Far Clipを設定。固定カメラは物体のTransformで位置・回転を指定する |
| 追従させるカメラ | Camera + Follow Camera | `Target`に主人公を指定。Camera Offset、Look Offset、Follow Smoothingを編集する |

追加・削除、追従対象の選択、チェックの切り替えは1回のUndo操作になる。数値のドラッグも押してから離すまでが1操作。変更は通常のシーン保存・読み込み、物体の複製・削除Undoに対応する。

カメラのオフセットはワールド座標。Follow Smoothingを0にすると即座に追従する。再生中は追従結果をカメラ物体のTransformへ反映する。編集シーンのTransformは保持する。Follow Cameraを外せば、Cameraは物体のTransformをそのまま使用する固定カメラになる。

Box Colliderは回転・拡大を反映したワールド軸の外接ボックスとして衝突する。Sceneの緑の線が実際の形状。Visibleを外しても衝突は残るため、見えない壁にも使える。モデルなしの物体にも衝突形状を追加できる。

## 再生と編集データ

Playはシーンを複製して実行する。再生中の移動や復帰は保存対象へ書き込まず、編集操作を止める。Stop後も未保存の編集はそのまま残る。ウィンドウの終了要求は先にPlayを止め、元の編集データに対して保存確認を行う。

入力はGameへフォーカスがあり、アプリがアクティブで、文字入力・ウィジェット操作中でない場合だけ渡す。Gameが隠れたときとアプリからフォーカスが外れたときはゲーム時間を止める。

Character ControllerとFollow Cameraはそれぞれ任意で、追加する場合は各1つまで対応する。Cameraは複数配置でき、描画に使う1台だけPrimaryにする。Primaryがない場合もPlayでき、開始時のSceneビューを使用する。Follow Cameraには同じ物体のCameraが必要。床・壁は再生開始時の静的な形状を使用する。開始位置や復帰位置が壁・床の内部にある、追加済みコンポーネントの依存先（Character Controllerに対するBox Collider、Respawnに対するCharacter Controllerなど）がない、追従先が削除されている場合は、Play開始時に理由を表示する。編集中は未設定のまま保存できるので、構成の途中で保存してもよい。

シーン形式はversion 6。version 1〜5も読み込める。version 5のFollow CameraにはCameraを補って移行する。古い実行ファイルが新しいコンポーネントを失った状態で上書き保存しないよう、形式番号を更新している。

Character Controllerを使う物体にはBox Colliderが必要。Character Controllerがないシーンにはこの条件はなく、空のシーンやモデルだけのシーンもそのままPlayできる。
