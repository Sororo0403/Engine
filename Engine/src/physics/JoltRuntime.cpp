// Joltの他のヘッダーより先に共通定義を読み込む。
#include <Jolt/Jolt.h>

#include "JoltRuntime.h"
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <stdexcept>

namespace JoltRuntime {
/// <summary>
/// Jolt共通の型登録を、物理ワールドより先に作り、最後に終了する
/// </summary>
struct Registry {
	Registry() {
		if (JPH::Factory::sInstance)
			throw std::runtime_error(
			    "Jolt is already initialized outside the engine.");
		JPH::RegisterDefaultAllocator();
		if (!JPH::VerifyJoltVersionID())
			throw std::runtime_error(
			    "Jolt library configuration does not match the engine.");
		factory = std::make_unique<JPH::Factory>();
		JPH::Factory::sInstance = factory.get();
		JPH::RegisterTypes();
	}

	~Registry() {
		JPH::UnregisterTypes();
		JPH::Factory::sInstance = nullptr;
	}

	std::unique_ptr<JPH::Factory> factory;
};

/// <summary>
/// 初めて物理形状またはワールドを作るときにだけ共通機能を初期化する
/// </summary>
std::shared_ptr<Registry> Initialize() {
	static const auto registry = std::make_shared<Registry>();
	return registry;
}

} // namespace JoltRuntime
