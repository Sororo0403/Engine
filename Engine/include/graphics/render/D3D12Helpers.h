#pragma once

#include <d3d12.h>

namespace D3D12Helpers {
/// <summary>
/// リソース全体の状態遷移を記録する。状態の追跡やGPU待機は行わない
/// </summary>
/// <param name="commandList">記録中のコマンドリスト</param>
/// <param name="resource">状態を変更するリソース</param>
/// <param name="before">変更前の状態</param>
/// <param name="after">変更後の状態</param>
void TransitionResource(ID3D12GraphicsCommandList *commandList,
                        ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                        D3D12_RESOURCE_STATES after);
} // namespace D3D12Helpers
