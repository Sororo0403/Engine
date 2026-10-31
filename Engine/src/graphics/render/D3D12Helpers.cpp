#include "graphics/render/D3D12Helpers.h"

#include <cassert>

void D3D12Helpers::TransitionResource(ID3D12GraphicsCommandList *commandList,
                                      ID3D12Resource *resource,
                                      D3D12_RESOURCE_STATES before,
                                      D3D12_RESOURCE_STATES after) {
	assert(commandList && resource);

	if (before == after) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;

	commandList->ResourceBarrier(1, &barrier);
}
