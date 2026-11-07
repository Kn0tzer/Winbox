package boxedwine.org.marshal;

import boxedwine.org.data.VkData;
import boxedwine.org.data.VkFunction;
import boxedwine.org.data.VkParam;

public class VkCmdPushDescriptorSetWithTemplateMarshal extends VkHostMarshal {
    public void before(VkData data, VkFunction fn, StringBuilder out, VkParam param) throws Exception {
        out.append("    U32 dataSize = calculateUpdateDescriptorSetWithTemplateDataSize(pBoxedInfo, descriptorUpdateTemplate);\n");
        out.append("    const void* pData = ARG5 && dataSize ? cpu->memory->lockReadOnlyMemory(ARG5, dataSize) : nullptr;\n");
    }

    public void after(VkData data, VkFunction fn, StringBuilder out, VkParam param) throws Exception {
        out.append("    cpu->memory->unlockMemory((U8*)pData);\n");
    }
}
