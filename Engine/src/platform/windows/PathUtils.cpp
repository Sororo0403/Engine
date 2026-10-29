#include "platform/windows/PathUtils.h"

#include <Windows.h>
#include <string>

std::filesystem::path PathUtils::GetExecutablePath() {
	std::wstring buffer(32768, L'\0');

	const DWORD length = GetModuleFileNameW(
		nullptr,
		buffer.data(),
		static_cast<DWORD>(buffer.size())
	);

	if (length == 0 || length >= buffer.size()) {
		return {};
	}

	buffer.resize(length);

	return std::filesystem::path(buffer);
}

std::filesystem::path PathUtils::GetExecutableDirectory() {
	return GetExecutablePath().parent_path();
}
