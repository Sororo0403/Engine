#include "platform/windows/WindowsWiiRemote.h"

#include <cmath>
extern "C" {
#include <hidsdi.h>
}
#include <cfgmgr32.h>
#include <vector>

#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "hid.lib")

namespace {
constexpr USHORT kNintendoVendor = 0x057e;
constexpr USHORT kWiiRemoteProduct = 0x0306;
constexpr USHORT kWiiRemotePlusProduct = 0x0330;
constexpr ULONGLONG kScanIntervalMs = 1000;
constexpr ULONGLONG kReportTimeoutMs = 1500;
constexpr ULONGLONG kShakeIntervalMs = 300;
// 0x31の上位8ビットでは、一般的な個体の1 gは約25カウント。
constexpr float kShakeThreshold = 28.f;
constexpr float kSettleThreshold = 12.f;
} // namespace

WindowsWiiRemote::~WindowsWiiRemote() {
	Close();
}

bool WindowsWiiRemote::Open() {
	GUID hidGuid{};
	HidD_GetHidGuid(&hidGuid);
	ULONG length = 0;
	constexpr ULONG flags = CM_GET_DEVICE_INTERFACE_LIST_PRESENT;
	if (CM_Get_Device_Interface_List_SizeW(&length, &hidGuid, nullptr, flags) !=
	        CR_SUCCESS ||
	    length <= 1)
		return false;

	std::vector<wchar_t> paths(length);
	if (CM_Get_Device_Interface_ListW(&hidGuid, nullptr, paths.data(), length,
	                                 flags) != CR_SUCCESS)
		return false;

	for (const auto *path = paths.data(); *path; path += lstrlenW(path) + 1) {
		constexpr DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
		const auto probe = CreateFileW(path, 0, share, nullptr, OPEN_EXISTING,
		                               FILE_FLAG_OVERLAPPED, nullptr);
		if (probe == INVALID_HANDLE_VALUE)
			continue;
		HIDD_ATTRIBUTES attributes{sizeof(HIDD_ATTRIBUTES)};
		HIDP_CAPS caps{};
		PHIDP_PREPARSED_DATA preparsed = nullptr;
		const bool isRemote = HidD_GetAttributes(probe, &attributes) &&
		                      attributes.VendorID == kNintendoVendor &&
		                      (attributes.ProductID == kWiiRemoteProduct ||
		                       attributes.ProductID == kWiiRemotePlusProduct);
		if (isRemote && HidD_GetPreparsedData(probe, &preparsed)) {
			if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS)
				caps = {};
			HidD_FreePreparsedData(preparsed);
		}
		CloseHandle(probe);
		if (caps.InputReportByteLength < 6 ||
		    caps.InputReportByteLength > report_.size())
			continue;

		device_ = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, share, nullptr,
		                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (device_ == INVALID_HANDLE_VALUE)
			continue;

		event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!event_) {
			Close();
			return false;
		}
		io_ = {};
		io_.hEvent = event_;
		inputSize_ = caps.InputReportByteLength;
		lastReport_ = GetTickCount64();
		writing_ = true;
		// WindowsのHID APIでは転送ヘッダー(A2)を付けずに送る。
		if (WriteFile(device_, mode_.data(), static_cast<DWORD>(mode_.size()),
		              nullptr, &io_) || GetLastError() == ERROR_IO_PENDING)
			return true;
		Close();
	}
	return false;
}

bool WindowsWiiRemote::BeginRead() {
	io_ = {};
	io_.hEvent = event_;
	ResetEvent(event_);
	return ReadFile(device_, report_.data(), inputSize_, nullptr, &io_) ||
	       GetLastError() == ERROR_IO_PENDING;
}

void WindowsWiiRemote::ReadReport(bool active, DWORD size, ULONGLONG now) {
	if (size < 6 || report_[0] != 0x31)
		return;
	lastReport_ = now;
	state_.connected = true;
	state_.aDown = (report_[2] & 0x08) != 0;
	const std::array<float, 3> acceleration{
	    float(report_[3]), float(report_[4]), float(report_[5])};

	if (!active || !hasResting_) {
		resting_ = acceleration;
		hasResting_ = active;
		shakeArmed_ = false;
		return;
	}

	// ゆっくり傾けた場合は基準が追従し、急な加速度の変化だけを検出する。
	float motionSquared = 0;
	for (std::size_t axis = 0; axis < resting_.size(); ++axis) {
		const float change = acceleration[axis] - resting_[axis];
		motionSquared += change * change;
		resting_[axis] += 0.10f * change;
	}
	if (motionSquared < kSettleThreshold * kSettleThreshold &&
	    now >= nextShake_)
		shakeArmed_ = true;
	if (shakeArmed_ && motionSquared >= kShakeThreshold * kShakeThreshold) {
		state_.shake = true;
		shakeArmed_ = false;
		nextShake_ = now + kShakeIntervalMs;
	}
}

bool WindowsWiiRemote::ReadReports(bool active) {
	const auto now = GetTickCount64();
	// 受信が続いても1フレームでの処理量を制限する。
	for (int i = 0; i < 32; ++i) {
		DWORD size = 0;
		if (!GetOverlappedResult(device_, &io_, &size, FALSE))
			return GetLastError() == ERROR_IO_INCOMPLETE &&
			       now - lastReport_ < kReportTimeoutMs;

		if (writing_) {
			writing_ = false;
			if (size != mode_.size())
				return false;
		} else {
			ReadReport(active, size, now);
		}
		if (!BeginRead())
			return false;
	}
	return now - lastReport_ < kReportTimeoutMs;
}

WiiRemoteState WindowsWiiRemote::Update(bool active) {
	state_.shake = false;
	if (!active) {
		hasResting_ = false;
		shakeArmed_ = false;
	}
	const auto now = GetTickCount64();
	if (device_ == INVALID_HANDLE_VALUE && now >= nextScan_) {
		nextScan_ = now + kScanIntervalMs;
		Open();
	}
	if (device_ != INVALID_HANDLE_VALUE && !ReadReports(active)) {
		Close();
		nextScan_ = now + kScanIntervalMs;
	}
	return active ? state_ : WiiRemoteState{};
}

void WindowsWiiRemote::Close() {
	if (device_ != INVALID_HANDLE_VALUE) {
		CancelIoEx(device_, &io_);
		DWORD unused = 0;
		GetOverlappedResult(device_, &io_, &unused, TRUE);
		CloseHandle(device_);
		device_ = INVALID_HANDLE_VALUE;
	}
	if (event_) {
		CloseHandle(event_);
		event_ = nullptr;
	}
	state_ = {};
	hasResting_ = false;
	shakeArmed_ = false;
	nextShake_ = 0;
}
