#pragma once
#include <memory>

namespace JoltRuntime {
struct Registry;

/// <summary>
/// 剛体と形状問い合わせで同じ型登録を共有し、すべての物理形状より後に終了する
/// </summary>
/// <returns>プロセス内で共用する型登録。設定の不一致や外部での初期化は例外を返す</returns>
std::shared_ptr<Registry> Initialize();
} // namespace JoltRuntime
