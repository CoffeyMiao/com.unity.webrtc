#pragma once

#include <vulkan/vulkan.h>

namespace unity
{
namespace webrtc
{

    /// <summary>
    ///
    /// </summary>
    /// <param name="getInstanceProcAddr"></param>
    /// <param name="userdata"></param>
    /// <returns></returns>
    PFN_vkGetInstanceProcAddr UNITY_INTERFACE_API
    InterceptVulkanInitialization(PFN_vkGetInstanceProcAddr getInstanceProcAddr, void* userdata);

} // namespace webrtc
} // namespace unity
