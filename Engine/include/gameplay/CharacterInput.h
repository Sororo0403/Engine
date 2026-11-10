#pragma once

/// <summary>
/// 主人公のワールドX・Z方向への入力と、ジャンプ・操作ボタンを押した瞬間の状態
/// </summary>
struct CharacterInput {
	float moveX = 0.0f;
	float moveZ = 0.0f;
	bool jumpPressed = false;
	bool interactPressed = false;
	bool jumpHeld = false;
};
