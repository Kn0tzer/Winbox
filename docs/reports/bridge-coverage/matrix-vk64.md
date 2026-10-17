# vk64 Bridge Coverage Matrix

Source: vkcov in-browser probe (266 handlers tested).
Slow threshold: 10000us (see METHOD.md).

## Four-bucket summary

| Bucket | Count |
|---|---|
| 1. Total handlers tested | 266 |
| 2. Fully working | 170 (165 + 5 re-bucketed non-VkResult handlers, see MATRIX.md) |
| 3. Slow or flaky | 0 |
| 4. Broken, stub, or guarded | 96 (12 guarded by d1713623, 2 stub-contract violations, 82 stubs) |


**Note (2026-10-09):** The 12 handlers marked "guarded" crashed in the 2026-10-09 probe
(garbage regionCount -> C++ exception). Commit d1713623 ("Vulkan: defensive guards
against garbage regionCount in copy functions") adds `if (nreg > 1024) return 0;`
guards. They no longer crash on garbage input; legitimate calls (nreg <= 1024)
proceed normally. Verified: d3d-conformance-vulkandefensive/ 15/16 ops, no regression.

## Per-handler table

| Handler | Bucket | Detail | rc1 | rc2 | us1 | us2 |
|---|---|---|---|---|---|---|
| vkAcquireNextImageKHR | working | 1000us | 0 | 0 | 0 | 1000 |
| vkAllocateCommandBuffers | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkAllocateDescriptorSets | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkAllocateMemory | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkBeginCommandBuffer | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkBindBufferMemory | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkBindBufferMemory2 | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkBindBufferMemory2KHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkBindImageMemory | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkBindImageMemory2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkBindImageMemory2KHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBeginConditionalRenderingEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCmdBeginQuery | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdBeginRenderPass | working | 4000us | 0 | 0 | 4000 | 3000 |
| vkCmdBeginRenderPass2 | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkCmdBeginRenderPass2KHR | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkCmdBeginRendering | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBeginRenderingKHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBindDescriptorSets | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdBindIndexBuffer | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBindIndexBuffer2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBindIndexBuffer2KHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBindPipeline | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBindVertexBuffers | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCmdBindVertexBuffers2 | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCmdBindVertexBuffers2EXT | working | 2000us | 0 | 0 | 2000 | 0 |
| vkCmdBindVertexBuffers2KHR | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCmdBlitImage | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdBlitImage2 | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdBlitImage2KHR | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdClearAttachments | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCmdClearAttachmentsKHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdClearColorImage | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdCopyBuffer | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdCopyBuffer2 | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdCopyBuffer2KHR | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdCopyBufferToImage | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdCopyBufferToImage2 | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCmdCopyBufferToImage2KHR | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdCopyImage | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdCopyImage2 | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdCopyImage2KHR | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdCopyImageToBuffer | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdCopyImageToBuffer2 | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdCopyImageToBuffer2KHR | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdDispatch | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdDispatchBase | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdDispatchIndirect | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdDraw | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdDrawIndexed | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdDrawIndexedIndirect | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCmdDrawIndexedIndirectCount | working | 3000us | 0 | 0 | 3000 | 1000 |
| vkCmdDrawIndirect | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdDrawIndirectByteCountEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdDrawIndirectCount | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdDrawIndirectCountKHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdEndConditionalRenderingEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdEndQuery | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdEndRenderPass | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdEndRenderPass2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdEndRenderPass2KHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdEndRendering | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdEndRenderingKHR | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdFillBuffer | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdNextSubpass | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCmdNextSubpass2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdNextSubpass2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdNextSubpassKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdPipelineBarrier | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdPipelineBarrier2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdPipelineBarrier2KHR | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdPushConstants | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdPushConstants2 | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdPushConstants2KHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdPushDescriptorSet | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdResetEvent | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdResetQueryPool | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCmdResolveImage | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdResolveImageKHR | guarded | d1713623: no longer crashes (nreg>1024 guard); was crashed in 2026-10-09 probe | -2 | -2 | 0 | 0 |
| vkCmdSetBlendConstant | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdSetCullModeEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCmdSetDepthBias | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCmdSetDepthBiasEnableEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdSetDepthBiasNV | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdSetDepthBounds | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdSetDeviceMask | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCmdSetEvent | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdSetFrontFaceEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCmdSetLineWidth | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdSetPrimitiveTopologyEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdSetScissor | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdSetScissorWithCount | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetScissorWithCountEXT | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetStencilReference | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdSetVertexInputEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdSetViewport | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdSetViewportWithCount | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetViewportWithCountEXT | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdUpdateBuffer | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdWaitEvents | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdWriteTimestamp | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdWriteTimestamp2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCmdWriteTimestamp2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCreateBuffer | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkCreateBufferView | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateCommandPool | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCreateComputePipelines | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCreateDebugUtilsMessengerEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCreateDescriptorPool | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateDescriptorSetLayout | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCreateDescriptorUpdateTemplate | working | 3000us | 0 | 0 | 3000 | 2000 |
| vkCreateDevice | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkCreateEvent | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCreateFence | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateFramebuffer | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateFramebuffer2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCreateGraphicsPipelines | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCreateHeadlessSurfaceEXT | working | 3000us | 0 | 0 | 3000 | 2000 |
| vkCreateImage | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateImageView | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateInstance | working | 2000us | 0 | 0 | 2000 | 0 |
| vkCreatePipelineCache | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreatePipelineLayout | working | 3000us | 0 | 0 | 3000 | 3000 |
| vkCreateQueryPool | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCreateRenderPass | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateSampler | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateSamplerYcbcrConversion | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCreateSemaphore | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkCreateShaderModule | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkCreateSwapchainKHR | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCreateSwapchainKHR2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCreateWin32SurfaceKHR | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCreateXcbSurfaceKHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateXlibSurfaceKHR | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkDestroyBuffer | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyBufferView | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkDestroyCommandPool | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyDebugUtilsMessengerEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkDestroyDescriptorPool | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyDescriptorSetLayout | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyDescriptorUpdateTemplate | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkDestroyDevice | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyDeviceKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkDestroyEvent | working | 1000us | 0 | 0 | 1000 | 0 |
| vkDestroyFence | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyFramebuffer | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyImage | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyImageView | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyInstance | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyPipelineCache | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkDestroyPipelineLayout | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyQueryPool | working | 2000us | 0 | 0 | 2000 | 0 |
| vkDestroyRenderPass | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroySampler | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroySamplerYcbcrConversion | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkDestroySemaphore | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyShaderModule | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroySurfaceKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroySwapchainKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroySwapchainKHR2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkDeviceWaitIdle | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkEndCommandBuffer | working | 3000us | 0 | 0 | 2000 | 3000 |
| vkEnumerateDeviceExtensionProperties | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkEnumerateInstanceExtensionProperties | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkEnumerateInstanceLayerProperties | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkEnumerateInstanceVersion | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkEnumeratePhysicalDevices | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkFlushMappedMemoryRanges | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkFreeCommandBuffers | working | 0us | 0 | 0 | 0 | 0 |
| vkFreeDescriptorSets | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkFreeMemory | working | 0us | 0 | 0 | 0 | 0 |
| vkGetBufferDeviceAddress | broken | rc=1 | 1 | 1 | 1000 | 1000 |
| vkGetBufferDeviceAddressKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetBufferMemoryRequirements | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetBufferMemoryRequirements2 | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkGetBufferMemoryRequirements2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetBufferOpaqueCaptureAddressKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetDescriptorSetLayoutSupport | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetDescriptorSetLayoutSupportKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetDeviceBufferMemoryRequirements | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetDeviceGroupPeerMemoryFeatures | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetDeviceGroupPresentCapabilitiesKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetDeviceGroupSurfacePresentModesKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetDeviceImageMemoryRequirements | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetDeviceImageSparseMemoryRequirements | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetDeviceImageSubresourceLayout | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetDeviceMemoryCommitment | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetDeviceProcAddr | broken | rc=8233161616 | 8233161616 | 8233161616 | 2000 | 2000 |
| vkGetDeviceQueue | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetDeviceQueue2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetEventStatus | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetFenceStatus | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetFenceStatus2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetFenceStatus2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetImageMemoryRequirements | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetImageMemoryRequirements2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetImageMemoryRequirements2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetImageSparseMemoryRequirements | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetImageSubresourceLayout | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetImageSubresourceLayout2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 2000 |
| vkGetImageSubresourceLayout2EXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetInstanceProcAddr | broken | rc=8233161616 | 8233161616 | 8233161616 | 2000 | 1000 |
| vkGetPastPresentationTimingGOOGLE | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceFeatures | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceFeatures2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceFeatures2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceFormatProperties | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceFormatProperties2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceFormatProperties2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceImageFormatProperties | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceImageFormatProperties2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceImageFormatProperties2KHR | broken | rc=-3 | -3 | -3 | 0 | 0 |
| vkGetPhysicalDeviceMemoryProperties | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceMemoryProperties2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceMemoryProperties2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDevicePresentRectanglesKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetPhysicalDeviceProperties | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceProperties2 | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkGetPhysicalDeviceProperties2KHR | broken | rc=-3 | -3 | -3 | 2000 | 0 |
| vkGetPhysicalDeviceQueueFamilyProperties2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceQueueFamilyProperties2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceSurfaceCapabilities2EXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetPhysicalDeviceSurfaceCapabilitiesKHR | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkGetPhysicalDeviceSurfaceFormats2EXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 2000 |
| vkGetPhysicalDeviceSurfaceFormatsKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceSurfacePresentModes2EXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetPhysicalDeviceSurfacePresentModesKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceSurfaceSupportKHR | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkGetPhysicalDeviceWin32PresentationSupportKHR | broken | rc=1 | 1 | 1 | 0 | 0 |
| vkGetPhysicalDeviceXlibPresentationSupportKHR | broken | rc=1 | 1 | 1 | 0 | 0 |
| vkGetPipelineCacheData | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetPipelineLayoutSupport | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetQueryPoolResults | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 2000 |
| vkGetQueryPoolResults64 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetQueryPoolResults64KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetRenderAreaGranularity | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkGetRenderingAreaGranularity | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetSemaphoreCounterValue | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkGetSwapchainImagesKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkInvalidateMappedMemoryRanges | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkMapMemory | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkMergePipelineCaches | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkQueueBindSparse | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkQueuePresentKHR | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkQueueSubmit | working | 3000us | 0 | 0 | 3000 | 2000 |
| vkQueueSubmit2 | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkQueueSubmit2KHR | working | 3000us | 0 | 0 | 2000 | 3000 |
| vkQueueWaitIdle | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkResetCommandBuffer | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkResetCommandPool | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkResetEvent | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkResetFences | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkResetPipelineCache | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkSetDebugUtilsObjectNameEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkSetDebugUtilsObjectTagEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkSetEvent | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 2000 |
| vkSignalSemaphore | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkSubmitDebugUtilsMessageEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 2000 |
| vkSwapchainImagesKHR2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkTrimCommandPool | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkTrimCommandPoolKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkUnmapMemory | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkUpdateDescriptorSetWithTemplate | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkUpdateDescriptorSets | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkWaitForFences | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkWaitSemaphores | working | 1000us | 0 | 0 | 1000 | 1000 |
