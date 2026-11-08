# vk64 Bridge Coverage Matrix

Source: vkcov in-browser probe (309 handlers tested).
Slow threshold: 10000us (see METHOD.md).

## Four-bucket summary

| Bucket | Count |
|---|---|
| 1. Total handlers tested | 309 |
| 2. Fully working | 221 |
| 3. Slow or flaky | 3 |
| 4. Broken or stub | 85 |

## Per-handler table

| Handler | Bucket | Detail | rc1 | rc2 | us1 | us2 |
|---|---|---|---|---|---|---|
| vkAcquireNextImageKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkAllocateCommandBuffers | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkAllocateDescriptorSets | working | 2000us | 0 | 0 | 2000 | 0 |
| vkAllocateMemory | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkBeginCommandBuffer | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkBindBufferMemory | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkBindBufferMemory2 | working | 1000us | 0 | 0 | 1000 | 0 |
| vkBindBufferMemory2KHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkBindImageMemory | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkBindImageMemory2 | working | 1000us | 0 | 0 | 1000 | 0 |
| vkBindImageMemory2KHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBeginConditionalRenderingEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdBeginQuery | working | 3000us | 0 | 0 | 3000 | 2000 |
| vkCmdBeginRenderPass | working | 4000us | 0 | 0 | 4000 | 2000 |
| vkCmdBeginRenderPass2 | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdBeginRenderPass2KHR | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdBeginRendering | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBeginRenderingKHR | working | 2000us | 0 | 0 | 0 | 2000 |
| vkCmdBindDescriptorSets | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdBindIndexBuffer | working | 2000us | 0 | 0 | 2000 | 0 |
| vkCmdBindIndexBuffer2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBindIndexBuffer2KHR | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCmdBindPipeline | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdBindVertexBuffers | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdBindVertexBuffers2 | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdBindVertexBuffers2EXT | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCmdBindVertexBuffers2KHR | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdBlitImage | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| vkCmdBlitImage2 | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| vkCmdBlitImage2KHR | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| vkCmdClearAttachments | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdClearAttachmentsKHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdClearColorImage | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdClearDepthStencilImage | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdCopyBuffer | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdCopyBuffer2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdCopyBuffer2KHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdCopyBufferToImage | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdCopyBufferToImage2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdCopyBufferToImage2KHR | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdCopyImage | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| vkCmdCopyImage2 | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| vkCmdCopyImage2KHR | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| vkCmdCopyImageToBuffer | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdCopyImageToBuffer2 | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| vkCmdCopyImageToBuffer2KHR | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| vkCmdCopyQueryPoolResults | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdDispatch | working | 2000us | 0 | 0 | 2000 | 0 |
| vkCmdDispatchBase | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdDispatchIndirect | working | 2000us | 0 | 0 | 2000 | 0 |
| vkCmdDraw | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdDrawIndexed | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdDrawIndexedIndirect | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdDrawIndexedIndirectCount | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdDrawIndirect | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdDrawIndirectByteCountEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdDrawIndirectCount | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCmdDrawIndirectCountKHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdEndConditionalRenderingEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkCmdEndQuery | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCmdEndRenderPass | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdEndRenderPass2 | working | 2000us | 0 | 0 | 2000 | 0 |
| vkCmdEndRenderPass2KHR | working | 2000us | 0 | 0 | 2000 | 0 |
| vkCmdEndRendering | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdEndRenderingKHR | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdExecuteCommands | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdFillBuffer | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdNextSubpass | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCmdNextSubpass2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkCmdNextSubpass2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 0 |
| vkCmdNextSubpassKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkCmdPipelineBarrier | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdPipelineBarrier2 | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdPipelineBarrier2KHR | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkCmdPushConstants | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdPushConstants2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdPushConstants2KHR | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCmdPushDescriptorSet | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdPushDescriptorSetKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdPushDescriptorSetWithTemplate | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCmdPushDescriptorSetWithTemplateKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdResetEvent | broken | rc=-3 | -3 | -3 | 0 | 0 |
| vkCmdResetEvent2 | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdResetEvent2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdResetQueryPool | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCmdResolveImage | working | 5000us | 0 | 0 | 5000 | 0 |
| vkCmdResolveImage2 | working | 2000us | 0 | 0 | 0 | 2000 |
| vkCmdResolveImage2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdResolveImageKHR | working | 3000us | 0 | 0 | 3000 | 2000 |
| vkCmdSetBlendConstant | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdSetBlendConstants | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCmdSetCullMode | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCmdSetCullModeEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkCmdSetDepthBias | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdSetDepthBiasEnable | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetDepthBiasEnableEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdSetDepthBiasNV | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdSetDepthBounds | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdSetDepthBoundsTestEnable | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetDepthBoundsTestEnableEXT | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetDepthCompareOp | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCmdSetDepthCompareOpEXT | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetDepthTestEnable | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCmdSetDepthTestEnableEXT | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetDepthWriteEnable | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetDepthWriteEnableEXT | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetDeviceMask | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdSetEvent | broken | rc=-3 | -3 | -3 | 0 | 0 |
| vkCmdSetEvent2 | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetEvent2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetFrontFace | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetFrontFaceEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkCmdSetLineWidth | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdSetPrimitiveRestartEnable | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetPrimitiveRestartEnableEXT | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetPrimitiveTopology | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCmdSetPrimitiveTopologyEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdSetRasterizerDiscardEnable | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetRasterizerDiscardEnableEXT | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCmdSetScissor | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdSetScissorWithCount | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetScissorWithCountEXT | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetStencilCompareMask | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetStencilOp | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetStencilOpEXT | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetStencilReference | working | 3000us | 0 | 0 | 3000 | 1000 |
| vkCmdSetStencilTestEnable | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetStencilTestEnableEXT | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCmdSetStencilWriteMask | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetVertexInputEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkCmdSetViewport | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdSetViewportWithCount | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdSetViewportWithCountEXT | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdUpdateBuffer | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdWaitEvents | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdWaitEvents2 | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdWaitEvents2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkCmdWriteTimestamp | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCmdWriteTimestamp2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCmdWriteTimestamp2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 3000 |
| vkCreateBuffer | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCreateBufferView | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCreateCommandPool | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateComputePipelines | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateDebugUtilsMessengerEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 2000 |
| vkCreateDescriptorPool | working | 5000us | 0 | 0 | 3000 | 5000 |
| vkCreateDescriptorSetLayout | working | 3000us | 0 | 0 | 2000 | 3000 |
| vkCreateDescriptorUpdateTemplate | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkCreateDevice | working | 2000us | 0 | 0 | 2000 | 2000 |
| vkCreateEvent | working | 0us | 0 | 0 | 0 | 0 |
| vkCreateFence | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCreateFramebuffer | working | 3000us | 0 | 0 | 3000 | 1000 |
| vkCreateFramebuffer2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkCreateGraphicsPipelines | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCreateHeadlessSurfaceEXT | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCreateImage | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkCreateImageView | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCreateInstance | slow | 15000us >= 10000us | 0 | 0 | 15000 | 0 |
| vkCreatePipelineCache | working | 0us | 0 | 0 | 0 | 0 |
| vkCreatePipelineLayout | working | 5000us | 0 | 0 | 5000 | 3000 |
| vkCreateQueryPool | working | 0us | 0 | 0 | 0 | 0 |
| vkCreateRenderPass | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkCreateRenderPass2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 0 | 0 |
| vkCreateRenderPass2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 0 | 0 |
| vkCreateSampler | working | 0us | 0 | 0 | 0 | 0 |
| vkCreateSamplerYcbcrConversion | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCreateSemaphore | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkCreateShaderModule | slow | 16000us >= 10000us | 0 | 0 | 16000 | 3000 |
| vkCreateSwapchainKHR | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCreateSwapchainKHR2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkCreateWin32SurfaceKHR | working | 1000us | 0 | 0 | 0 | 1000 |
| vkCreateXcbSurfaceKHR | working | 1000us | 0 | 0 | 1000 | 0 |
| vkCreateXlibSurfaceKHR | working | 1000us | 0 | 0 | 1000 | 0 |
| vkDestroyBuffer | working | 1000us | 0 | 0 | 1000 | 0 |
| vkDestroyBufferView | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkDestroyCommandPool | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyDebugUtilsMessengerEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkDestroyDescriptorPool | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyDescriptorSetLayout | working | 1000us | 0 | 0 | 1000 | 0 |
| vkDestroyDescriptorUpdateTemplate | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkDestroyDevice | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyDeviceKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkDestroyEvent | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyFence | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyFramebuffer | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyImage | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyImageView | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyInstance | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyPipeline | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyPipelineCache | working | 1000us | 0 | 0 | 0 | 1000 |
| vkDestroyPipelineLayout | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyQueryPool | working | 1000us | 0 | 0 | 1000 | 0 |
| vkDestroyRenderPass | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroySampler | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroySamplerYcbcrConversion | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkDestroySemaphore | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroyShaderModule | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroySurfaceKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroySwapchainKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkDestroySwapchainKHR2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 0 | 0 |
| vkDeviceWaitIdle | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkEndCommandBuffer | working | 3000us | 0 | 0 | 3000 | 2000 |
| vkEnumerateDeviceExtensionProperties | working | 2000us | 0 | 0 | 2000 | 0 |
| vkEnumerateInstanceExtensionProperties | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkEnumerateInstanceLayerProperties | working | 1000us | 0 | 0 | 0 | 1000 |
| vkEnumerateInstanceVersion | working | 2000us | 0 | 0 | 2000 | 0 |
| vkEnumeratePhysicalDevices | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkFlushMappedMemoryRanges | working | 1000us | 0 | 0 | 0 | 1000 |
| vkFreeCommandBuffers | working | 0us | 0 | 0 | 0 | 0 |
| vkFreeDescriptorSets | broken | rc=-3 | -3 | -3 | 0 | 0 |
| vkFreeMemory | working | 0us | 0 | 0 | 0 | 0 |
| vkGetBufferDeviceAddress | broken | rc=1 | 1 | 1 | 1000 | 1000 |
| vkGetBufferDeviceAddressKHR | working | 1000us | 0 | 0 | 0 | 1000 |
| vkGetBufferMemoryRequirements | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetBufferMemoryRequirements2 | working | 3000us | 0 | 0 | 2000 | 3000 |
| vkGetBufferMemoryRequirements2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkGetBufferOpaqueCaptureAddressKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 3000 | 1000 |
| vkGetDescriptorSetLayoutSupport | working | 0us | 0 | 0 | 0 | 0 |
| vkGetDescriptorSetLayoutSupportKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkGetDeviceBufferMemoryRequirements | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetDeviceGroupPeerMemoryFeatures | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetDeviceGroupPresentCapabilitiesKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetDeviceGroupSurfacePresentModesKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetDeviceImageMemoryRequirements | broken | rc=-3 | -3 | -3 | 0 | 0 |
| vkGetDeviceImageSparseMemoryRequirements | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkGetDeviceImageSubresourceLayout | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 0 |
| vkGetDeviceMemoryCommitment | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkGetDeviceProcAddr | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| vkGetDeviceQueue | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetDeviceQueue2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkGetEventStatus | broken | rc=-3 | -3 | -3 | 0 | 0 |
| vkGetFenceStatus | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetFenceStatus2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkGetFenceStatus2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkGetImageMemoryRequirements | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkGetImageMemoryRequirements2 | working | 1000us | 0 | 0 | 0 | 1000 |
| vkGetImageMemoryRequirements2KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 0 |
| vkGetImageSparseMemoryRequirements | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkGetImageSubresourceLayout | working | 3000us | 0 | 0 | 1000 | 3000 |
| vkGetImageSubresourceLayout2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkGetImageSubresourceLayout2EXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 9000 | 1000 |
| vkGetInstanceProcAddr | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| vkGetPastPresentationTimingGOOGLE | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceFeatures | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceFeatures2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceFeatures2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceFormatProperties | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceFormatProperties2 | working | 1000us | 0 | 0 | 0 | 1000 |
| vkGetPhysicalDeviceFormatProperties2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceImageFormatProperties | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceImageFormatProperties2 | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkGetPhysicalDeviceImageFormatProperties2KHR | broken | rc=-3 | -3 | -3 | 0 | 0 |
| vkGetPhysicalDeviceMemoryProperties | working | 1000us | 0 | 0 | 0 | 1000 |
| vkGetPhysicalDeviceMemoryProperties2 | working | 3000us | 0 | 0 | 3000 | 1000 |
| vkGetPhysicalDeviceMemoryProperties2KHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDevicePresentRectanglesKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkGetPhysicalDeviceProperties | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceProperties2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetPhysicalDeviceProperties2KHR | broken | rc=-3 | -3 | -3 | 1000 | 0 |
| vkGetPhysicalDeviceQueueFamilyProperties | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceQueueFamilyProperties2 | working | 1000us | 0 | 0 | 0 | 1000 |
| vkGetPhysicalDeviceQueueFamilyProperties2KHR | working | 1000us | 0 | 0 | 0 | 1000 |
| vkGetPhysicalDeviceSurfaceCapabilities2EXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkGetPhysicalDeviceSurfaceCapabilitiesKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceSurfaceFormats2EXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkGetPhysicalDeviceSurfaceFormatsKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceSurfacePresentModes2EXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkGetPhysicalDeviceSurfacePresentModesKHR | working | 1000us | 0 | 0 | 1000 | 0 |
| vkGetPhysicalDeviceSurfaceSupportKHR | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPhysicalDeviceWin32PresentationSupportKHR | broken | rc=1 | 1 | 1 | 0 | 0 |
| vkGetPhysicalDeviceXlibPresentationSupportKHR | broken | rc=1 | 1 | 1 | 1000 | 0 |
| vkGetPipelineCacheData | working | 0us | 0 | 0 | 0 | 0 |
| vkGetPipelineLayoutSupport | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkGetQueryPoolResults | broken | rc=-3 | -3 | -3 | 0 | 0 |
| vkGetQueryPoolResults64 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 2000 |
| vkGetQueryPoolResults64KHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkGetRenderAreaGranularity | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkGetRenderingAreaGranularity | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 0 |
| vkGetSemaphoreCounterValue | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkGetSwapchainImagesKHR | working | 1000us | 0 | 0 | 1000 | 0 |
| vkInvalidateMappedMemoryRanges | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkMapMemory | slow | 17000us >= 10000us | 0 | 0 | 17000 | 1000 |
| vkMergePipelineCaches | working | 0us | 0 | 0 | 0 | 0 |
| vkQueueBindSparse | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 0 | 2000 |
| vkQueuePresentKHR | working | 3000us | 0 | 0 | 3000 | 0 |
| vkQueueSubmit | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkQueueSubmit2 | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkQueueSubmit2KHR | working | 2000us | 0 | 0 | 1000 | 2000 |
| vkQueueWaitIdle | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkResetCommandBuffer | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkResetCommandPool | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkResetDescriptorPool | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 0 | 0 |
| vkResetEvent | broken | rc=-3 | -3 | -3 | 0 | 0 |
| vkResetFences | working | 1000us | 0 | 0 | 1000 | 1000 |
| vkResetPipelineCache | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 0 |
| vkResetQueryPool | working | 0us | 0 | 0 | 0 | 0 |
| vkSetDebugUtilsObjectNameEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkSetDebugUtilsObjectTagEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkSetEvent | broken | rc=-3 | -3 | -3 | 0 | 0 |
| vkSignalSemaphore | working | 1000us | 0 | 0 | 1000 | 0 |
| vkSubmitDebugUtilsMessageEXT | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 1000 |
| vkSwapchainImagesKHR2 | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 1000 |
| vkTrimCommandPool | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 1000 | 0 |
| vkTrimCommandPoolKHR | stub | VK_ERROR_FEATURE_NOT_PRESENT (-8): explicit bridge stub | -8 | -8 | 2000 | 0 |
| vkUnmapMemory | working | 1000us | 0 | 0 | 0 | 1000 |
| vkUpdateDescriptorSetWithTemplate | working | 1000us | 0 | 0 | 1000 | 0 |
| vkUpdateDescriptorSets | working | 2000us | 0 | 0 | 2000 | 1000 |
| vkWaitForFences | working | 1000us | 0 | 0 | 1000 | 0 |
| vkWaitSemaphores | working | 1000us | 0 | 0 | 1000 | 1000 |
