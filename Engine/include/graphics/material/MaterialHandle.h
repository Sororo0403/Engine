#pragma once

#include <memory>
class MaterialResource;
struct ID3D12Device;

/// <summary>
/// メッシュと独立した材質を、描画物体とマネージャーの間で共有する
/// </summary>
class MaterialHandle {
  public:
	/// <summary>
	/// 初期化済みの材質を参照しているか確認する
	/// </summary>
	/// <returns>true:参照あり false:未設定</returns>
	explicit operator bool() const {
		return resource_ != nullptr;
	}

	/// <summary>
	/// 同じ描画基盤の同じ材質を参照しているか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:同一の共有参照 false:別の参照</returns>
	bool operator==(const MaterialHandle &other) const = default;

  private:
	friend class MaterialManager;
	friend class RenderWorld;
	std::shared_ptr<const MaterialResource> resource_;
	ID3D12Device *device_ = nullptr;
};
