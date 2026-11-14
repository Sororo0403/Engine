#pragma once

#include "input/WiiRemoteState.h"

#include <Windows.h>
#include <array>

/// <summary>
/// WindowsのHIDインターフェースからペアリング済みのWiiリモコンを1台読み取る。
/// </summary>
class WindowsWiiRemote {
  public:
	WindowsWiiRemote() = default;
	~WindowsWiiRemote();
	WindowsWiiRemote(const WindowsWiiRemote &) = delete;
	WindowsWiiRemote &operator=(const WindowsWiiRemote &) = delete;

	WiiRemoteState Update(bool active);
	void Close();

  private:
	bool Open();
	bool BeginRead();
	bool ReadReports(bool active);
	void ReadReport(bool active, DWORD size, ULONGLONG now);

	HANDLE device_ = INVALID_HANDLE_VALUE;
	HANDLE event_ = nullptr;
	OVERLAPPED io_{};
	bool writing_ = false;
	std::array<BYTE, 3> mode_{0x12, 0x04, 0x31};
	std::array<BYTE, 64> report_{};
	DWORD inputSize_ = 0;
	ULONGLONG nextScan_ = 0;
	ULONGLONG lastReport_ = 0;
	ULONGLONG nextShake_ = 0;
	std::array<float, 3> resting_{};
	bool hasResting_ = false;
	bool shakeArmed_ = false;
	WiiRemoteState state_{};
};
