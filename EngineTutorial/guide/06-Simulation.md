# 第6章 シミュレーションと物理

目次で **第6章 シミュレーションと物理** を開きます。移動床と剛体の更新順、Trigger、足場を出た直後のジャンプ、復帰とカメラ追従を扱います。

**実装索引を開く** から、衝突形状、剛体ワールド、Editorの物理設定などに関わる47ファイルを確認できます。教材の目次からは **実装索引 → 第6章** を選びます。

## フレームを進める

[SceneSimulation.cpp](../../Engine/src/gameplay/SceneSimulation.cpp) の `Advance` は、作業用のシーンにアニメーション、経路移動、剛体の更新を順に反映します。物体ごとの処理では、親や追従対象を先に更新する依存順を使います。作業データで更新を進め、検証を通った状態を採用します。

[ScenePhysics.cpp](../../Engine/src/gameplay/ScenePhysics.cpp) はシーンのColliderと剛体設定を物理用の記述に変換します。[RigidBodyWorld.cpp](../../Engine/src/physics/RigidBodyWorld.cpp) が、その記述からJoltの形状や剛体を作ります。動的な剛体から読み出した位置と回転は、`SetWorldPose` でシーンへ反映します。経路に沿う床などは、決められた変形を物理側に伝えます。

## 固体との衝突とTrigger

[CollisionWorld.cpp](../../Engine/src/physics/CollisionWorld.cpp) は、キャラクターが移動できる範囲を求めるため、固体のColliderを候補にします。Triggerはその候補から外れますが、[TriggerWorld.cpp](../../Engine/src/physics/TriggerWorld.cpp) が重なりを調べ、入った・接触している・出たというイベントを作ります。イベントに割り当てた操作の処理は [SceneSimulationActions.cpp](../../Engine/src/gameplay/SceneSimulationActions.cpp) を参照します。

## ジャンプ・落下復帰・カメラ

[CharacterController.cpp](../../Engine/src/gameplay/CharacterController.cpp) は、足元の短い移動で接地を調べます。足場を出た後の猶予時間や、着地直前のジャンプを覚えておく入力バッファを使い、条件がそろったときに上向きの速度を設定します。

復帰を設定したキャラクターが指定の高さを下回った場合は、復帰地点に移動します。追従カメラは更新された対象の位置を使い、通常は経過秒数に応じて補間します。復帰直後は係数を1にし、同じ更新内で追従先へ合わせます。障害物回避は [SceneCameraUtils.cpp](../../Engine/src/gameplay/SceneCameraUtils.cpp) で処理します。
