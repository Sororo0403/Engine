#include "platform/windows/WiiRemotePairing.h"

#include <Windows.h>
#include <bluetoothapis.h>
#include <array>
#include <format>
#include <string_view>

#pragma comment(lib, "Bthprops.lib")

namespace {
bool IsWiiRemote(std::wstring_view name) {
	return name == L"Nintendo RVL-CNT-01" ||
	       name == L"Nintendo RVL-CNT-01-TR";
}

DWORD PairRemote(HANDLE radio, const BLUETOOTH_RADIO_INFO &host,
                 BLUETOOTH_DEVICE_INFO &remote) {
	if (!remote.fAuthenticated) {
		// 赤いSYNCでのレガシー認証には、PC側のBluetoothアドレスの
		// 6バイトを下位バイトからそのままPINとして渡す。
		std::array<wchar_t, sizeof(host.address.rgBytes) + 1> pin{};
		for (std::size_t i = 0; i < sizeof(host.address.rgBytes); ++i)
			pin[i] = host.address.rgBytes[i];

		// BluetoothAuthenticateDeviceExには任意のPINを渡す引数がない。
#pragma warning(push)
#pragma warning(disable : 4995)
		const DWORD auth = BluetoothAuthenticateDevice(
		    nullptr, radio, &remote, pin.data(), sizeof(host.address.rgBytes));
#pragma warning(pop)
		if (auth != ERROR_SUCCESS && auth != ERROR_NO_MORE_ITEMS)
			return auth;

		DWORD serviceCount = 0;
		const DWORD services =
		    BluetoothEnumerateInstalledServices(radio, &remote, &serviceCount,
		                                        nullptr);
		if (services != ERROR_SUCCESS && services != ERROR_MORE_DATA)
			return services;
	}

	const DWORD hid = BluetoothSetServiceState(
	    radio, &remote, &HumanInterfaceDeviceServiceClass_UUID,
	    BLUETOOTH_SERVICE_ENABLE);
	if (hid == static_cast<DWORD>(E_INVALIDARG))
		return ERROR_SUCCESS; // HIDはすでに有効。
	return hid;
}

DWORD FindRemote(HANDLE radio) {
	BLUETOOTH_RADIO_INFO host{};
	host.dwSize = sizeof(host);
	if (const DWORD result = BluetoothGetRadioInfo(radio, &host);
	    result != ERROR_SUCCESS)
		return result;

	BLUETOOTH_DEVICE_SEARCH_PARAMS search{};
	search.dwSize = sizeof(search);
	search.fReturnAuthenticated = TRUE;
	search.fReturnRemembered = TRUE;
	search.fReturnUnknown = TRUE;
	search.fReturnConnected = TRUE;
	search.fIssueInquiry = TRUE;
	search.cTimeoutMultiplier = 4; // 5.12秒。
	search.hRadio = radio;
	BLUETOOTH_DEVICE_INFO remote{};
	remote.dwSize = sizeof(remote);
	const auto found = BluetoothFindFirstDevice(&search, &remote);
	if (!found)
		return GetLastError();

	DWORD result = ERROR_NO_MORE_ITEMS;
	do {
		if (!IsWiiRemote(remote.szName))
			continue;
		// Windowsの検索結果には電源が切れている登録済み機器も含まれる。
		if (remote.fAuthenticated && !remote.fConnected) {
			SYSTEMTIME now{};
			GetSystemTime(&now);
			FILETIME seenTime{}, nowTime{};
			if (!SystemTimeToFileTime(&remote.stLastSeen, &seenTime) ||
			    !SystemTimeToFileTime(&now, &nowTime))
				continue;
			ULARGE_INTEGER seen{};
			seen.LowPart = seenTime.dwLowDateTime;
			seen.HighPart = seenTime.dwHighDateTime;
			ULARGE_INTEGER current{};
			current.LowPart = nowTime.dwLowDateTime;
			current.HighPart = nowTime.dwHighDateTime;
			constexpr ULONGLONG recent = 10ULL * 10'000'000;
			if (seen.QuadPart + recent < current.QuadPart)
				continue;
		}
		result = PairRemote(radio, host, remote);
		if (result == ERROR_SUCCESS)
			break;
	} while (BluetoothFindNextDevice(found, &remote));

	BluetoothFindDeviceClose(found);
	return result;
}
} // namespace

std::string WiiRemotePairing::Pair() {
	const BLUETOOTH_FIND_RADIO_PARAMS params{sizeof(params)};
	DWORD result = ERROR_NO_MORE_ITEMS;
	// SYNCを押すまでの時間を確保し、最初の検索で見つからなければ再試行する。
	for (int attempt = 0; attempt < 3 && result != ERROR_SUCCESS; ++attempt) {
		HANDLE radio = nullptr;
		const auto radios = BluetoothFindFirstRadio(&params, &radio);
		if (!radios) {
			const DWORD error = GetLastError();
			if (error == ERROR_NO_MORE_ITEMS)
				return "No Bluetooth adapter found. Turn on Bluetooth in Windows and try again.";
			return std::format("Could not access Bluetooth (Windows error {}).", error);
		}

		do {
			const DWORD scan = FindRemote(radio);
			CloseHandle(radio);
			if (scan != ERROR_NO_MORE_ITEMS)
				result = scan;
			if (result == ERROR_SUCCESS)
				break;
		} while (BluetoothFindNextRadio(radios, &radio));

		BluetoothFindRadioClose(radios);
	}

	if (result == ERROR_SUCCESS)
		return "Wii Remote registered in Windows. When player 1 lights up, test it in Play.";
	if (result == ERROR_NO_MORE_ITEMS)
		return "Wii Remote not found. Press the red SYNC button while searching.";
	return std::format("Pairing failed (Windows error {}). Close any Windows PIN prompt and try again.", result);
}
