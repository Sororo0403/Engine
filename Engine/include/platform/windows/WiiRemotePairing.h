#pragma once

#include <string>

namespace WiiRemotePairing {
/// <summary>
/// 赤いSYNCボタンで待機中のWiiリモコンをWindowsに1台登録し、結果を返す。
/// Bluetoothの検索と認証を行うため、UIスレッドの外で呼び出す。
/// </summary>
std::string Pair();
}
