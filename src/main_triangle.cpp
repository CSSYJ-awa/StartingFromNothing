#include <iostream>
#include <stdexcept>
#include <vector>
#include <optional>
#include <set>
#include <cstring>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "vert_spv.h"
#include "frag_spv.h"

#ifdef ENABLE_VALIDATION
const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};
#else
const std::vector<const char*> validationLayers = {};
#endif

// Debug callback
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                    VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                    void* pUserData) {
    std::cerr << "Validation: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, messenger, pAllocator);
    }
}

// Replaced with swapchain recreation + MAX_FRAMES_IN_FLIGHT=2 implementation
int main() {
    // ensure runtime logs are captured to files so CI can read validation output
    std::freopen("main.stdout.txt","w",stdout);
    std::freopen("main.stderr.txt","w",stderr);
    try {
        if (!glfwInit()) throw std::runtime_error("Failed to init GLFW");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow* window = glfwCreateWindow(800, 600, "Vulkan Triangle", nullptr, nullptr);
        if (!window) throw std::runtime_error("Failed to create window");

        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

        VkApplicationInfo appInfo{}; appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; appInfo.pApplicationName = "Triangle"; appInfo.applicationVersion = VK_MAKE_VERSION(1,0,0); appInfo.pEngineName = "No Engine"; appInfo.engineVersion = VK_MAKE_VERSION(1,0,0); appInfo.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo createInfo{}; createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; createInfo.pApplicationInfo = &appInfo;

        uint32_t glfwExtensionCount = 0; const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
#ifdef ENABLE_VALIDATION
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
#else
        createInfo.enabledLayerCount = 0;
#endif
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef ENABLE_VALIDATION
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = &debugCreateInfo;
#endif

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) throw std::runtime_error("Failed to create instance");
#ifdef ENABLE_VALIDATION
        VkDebugUtilsMessengerCreateInfoEXT dbgCreate{}; dbgCreate.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT; dbgCreate.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT; dbgCreate.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; dbgCreate.pfnUserCallback = debugCallback; dbgCreate.pUserData = nullptr; VkResult dmRes = CreateDebugUtilsMessengerEXT(instance, &dbgCreate, nullptr, &debugMessenger); if (dmRes != VK_SUCCESS) std::cerr << "CreateDebugUtilsMessengerEXT returned " << dmRes << std::endl; else std::cerr << "Debug messenger created" << std::endl;
#endif

        VkSurfaceKHR surface; if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) throw std::runtime_error("Failed to create surface");
        // pick first physical device
        uint32_t deviceCount = 0; vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr); if (deviceCount == 0) throw std::runtime_error("No GPU");
        std::vector<VkPhysicalDevice> devices(deviceCount); vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()); VkPhysicalDevice physical = devices[0];
        VkPhysicalDeviceProperties pdp; vkGetPhysicalDeviceProperties(physical, &pdp); std::cerr << "Using GPU: " << pdp.deviceName << std::endl;

        // find queue families
        uint32_t qCount=0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, nullptr); std::vector<VkQueueFamilyProperties> qprops(qCount); vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, qprops.data());
        int graphicsFamily=-1, presentFamily=-1; for (int i=0;i< (int)qprops.size(); ++i){ if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphicsFamily=i; VkBool32 present=false; vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present); if (present) presentFamily=i; }
        if (graphicsFamily<0||presentFamily<0) throw std::runtime_error("No suitable queue");
        float priority=1.0f; std::set<int> families = {graphicsFamily, presentFamily}; std::vector<VkDeviceQueueCreateInfo> qcis; for (int f: families){ VkDeviceQueueCreateInfo qci{}; qci.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qci.queueFamilyIndex=f; qci.queueCount=1; qci.pQueuePriorities=&priority; qcis.push_back(qci);} 
        VkDevice device; VkDeviceCreateInfo dci{}; dci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; dci.queueCreateInfoCount=(uint32_t)qcis.size(); dci.pQueueCreateInfos=qcis.data(); const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME }; dci.enabledExtensionCount=1; dci.ppEnabledExtensionNames=devExts; if (vkCreateDevice(physical, &dci, nullptr, &device)!=VK_SUCCESS) throw std::runtime_error("Failed to create device");
        VkQueue graphicsQueue; vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue); VkQueue presentQueue; vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);

        // Helper state that will be recreated with swapchain
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchainExtent{800,600};
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        std::vector<VkFramebuffer> swapchainFramebuffers;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline graphicsPipeline = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers;
        std::vector<VkSemaphore> renderFinishedSemaphores;

        auto createSwapchainAndResources = [&](bool first)->void {
            // cleanup previous if not first
            if (!first) {
                vkDeviceWaitIdle(device);
                for (auto fb: swapchainFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
                swapchainFramebuffers.clear();
                for (auto iv: swapchainImageViews) vkDestroyImageView(device, iv, nullptr);
                swapchainImageViews.clear();
                if (graphicsPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, graphicsPipeline, nullptr); graphicsPipeline = VK_NULL_HANDLE; }
                if (pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, pipelineLayout, nullptr); pipelineLayout = VK_NULL_HANDLE; }
                if (renderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, renderPass, nullptr); renderPass = VK_NULL_HANDLE; }
                if (commandPool != VK_NULL_HANDLE) { vkDestroyCommandPool(device, commandPool, nullptr); commandPool = VK_NULL_HANDLE; }
                if (swapchain != VK_NULL_HANDLE) { vkDestroySwapchainKHR(device, swapchain, nullptr); swapchain = VK_NULL_HANDLE; }
            }

            // basic swapchain create (fixed format/extent for simplicity)
            VkSurfaceCapabilitiesKHR caps; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
            swapchainExtent = {800,600};
            VkSurfaceFormatKHR surfaceFormat{}; surfaceFormat.format = VK_FORMAT_B8G8R8A8_SRGB; surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            uint32_t imageCount = caps.minImageCount + 1; if (caps.maxImageCount>0 && imageCount>caps.maxImageCount) imageCount=caps.maxImageCount;
            VkSwapchainCreateInfoKHR sci{}; sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR; sci.surface = surface; sci.minImageCount=imageCount; sci.imageFormat = surfaceFormat.format; sci.imageColorSpace = surfaceFormat.colorSpace; sci.imageExtent = swapchainExtent; sci.imageArrayLayers=1; sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            uint32_t qfIndices[] = {(uint32_t)graphicsFamily,(uint32_t)presentFamily}; if (graphicsFamily!=presentFamily){ sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT; sci.queueFamilyIndexCount=2; sci.pQueueFamilyIndices = qfIndices; } else sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            sci.preTransform = caps.currentTransform; sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; sci.clipped = VK_TRUE; sci.oldSwapchain = VK_NULL_HANDLE;
            if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain)!=VK_SUCCESS) throw std::runtime_error("Failed to create swapchain");
            uint32_t scImageCount=0; vkGetSwapchainImagesKHR(device, swapchain, &scImageCount, nullptr); swapchainImages.resize(scImageCount); vkGetSwapchainImagesKHR(device, swapchain, &scImageCount, swapchainImages.data());

            swapchainImageViews.resize(scImageCount);
            for (uint32_t i=0;i<scImageCount;++i){ VkImageViewCreateInfo iv{}; iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; iv.image = swapchainImages[i]; iv.viewType = VK_IMAGE_VIEW_TYPE_2D; iv.format = surfaceFormat.format; iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; iv.subresourceRange.baseMipLevel=0; iv.subresourceRange.levelCount=1; iv.subresourceRange.baseArrayLayer=0; iv.subresourceRange.layerCount=1; if (vkCreateImageView(device, &iv, nullptr, &swapchainImageViews[i])!=VK_SUCCESS) throw std::runtime_error("Failed to create image view"); }

            // render pass
            VkAttachmentDescription colorAttachment{}; colorAttachment.format = surfaceFormat.format; colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            VkAttachmentReference colorRef{}; colorRef.attachment=0; colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount=1; subpass.pColorAttachments = &colorRef;
            VkRenderPassCreateInfo rpci{}; rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpci.attachmentCount=1; rpci.pAttachments=&colorAttachment; rpci.subpassCount=1; rpci.pSubpasses=&subpass; if (vkCreateRenderPass(device, &rpci, nullptr, &renderPass)!=VK_SUCCESS) throw std::runtime_error("Failed to create render pass");

            // pipeline
            VkShaderModuleCreateInfo smci{}; smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smci.codeSize = vert_spv_size; smci.pCode = (const uint32_t*)vert_spv; VkShaderModule vertSm; if (vkCreateShaderModule(device, &smci, nullptr, &vertSm)!=VK_SUCCESS) throw std::runtime_error("failed vert module");
            smci.codeSize = frag_spv_size; smci.pCode = (const uint32_t*)frag_spv; VkShaderModule fragSm; if (vkCreateShaderModule(device, &smci, nullptr, &fragSm)!=VK_SUCCESS) throw std::runtime_error("failed frag module");
            VkPipelineShaderStageCreateInfo vertStage{}; vertStage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; vertStage.stage=VK_SHADER_STAGE_VERTEX_BIT; vertStage.module=vertSm; vertStage.pName="main";
            VkPipelineShaderStageCreateInfo fragStage{}; fragStage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; fragStage.stage=VK_SHADER_STAGE_FRAGMENT_BIT; fragStage.module=fragSm; fragStage.pName="main";
            VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};
            VkPipelineVertexInputStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; vp.vertexBindingDescriptionCount=0; vp.vertexAttributeDescriptionCount=0;
            VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; ia.primitiveRestartEnable=VK_FALSE;
            VkViewport viewport{}; viewport.x=0; viewport.y=0; viewport.width=(float)swapchainExtent.width; viewport.height=(float)swapchainExtent.height; viewport.minDepth=0; viewport.maxDepth=1;
            VkRect2D scissor{}; scissor.offset={0,0}; scissor.extent=swapchainExtent;
            VkPipelineViewportStateCreateInfo vpstate{}; vpstate.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpstate.viewportCount=1; vpstate.pViewports=&viewport; vpstate.scissorCount=1; vpstate.pScissors=&scissor;
            VkPipelineRasterizationStateCreateInfo rast{}; rast.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rast.depthClampEnable=VK_FALSE; rast.rasterizerDiscardEnable=VK_FALSE; rast.polygonMode=VK_POLYGON_MODE_FILL; rast.lineWidth=1.0f; rast.cullMode=VK_CULL_MODE_BACK_BIT; rast.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; rast.depthBiasEnable=VK_FALSE;
            VkPipelineMultisampleStateCreateInfo ms{}; ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.sampleShadingEnable=VK_FALSE; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState cbatt{}; cbatt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT; cbatt.blendEnable=VK_FALSE;
            VkPipelineColorBlendStateCreateInfo cb{}; cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.logicOpEnable=VK_FALSE; cb.attachmentCount=1; cb.pAttachments=&cbatt;
            VkPipelineLayoutCreateInfo plci{}; plci.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; if (vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout)!=VK_SUCCESS) throw std::runtime_error("failed create pipeline layout");
            VkGraphicsPipelineCreateInfo gpci{}; gpci.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gpci.stageCount=2; gpci.pStages=stages; gpci.pVertexInputState=&vp; gpci.pInputAssemblyState=&ia; gpci.pViewportState=&vpstate; gpci.pRasterizationState=&rast; gpci.pMultisampleState=&ms; gpci.pColorBlendState=&cb; gpci.layout=pipelineLayout; gpci.renderPass=renderPass; gpci.subpass=0;
            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &graphicsPipeline) != VK_SUCCESS) throw std::runtime_error("failed to create pipeline");
            vkDestroyShaderModule(device, fragSm, nullptr); vkDestroyShaderModule(device, vertSm, nullptr);

            // framebuffers
            swapchainFramebuffers.resize(swapchainImageViews.size());
            for (size_t i=0;i<swapchainImageViews.size();++i){ VkImageView av = swapchainImageViews[i]; VkFramebufferCreateInfo fbci{}; fbci.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fbci.renderPass=renderPass; fbci.attachmentCount=1; fbci.pAttachments=&av; fbci.width=swapchainExtent.width; fbci.height=swapchainExtent.height; fbci.layers=1; if (vkCreateFramebuffer(device, &fbci, nullptr, &swapchainFramebuffers[i])!=VK_SUCCESS) throw std::runtime_error("failed fb"); }

            // command pool + buffers
            VkCommandPoolCreateInfo cpci{}; cpci.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; cpci.queueFamilyIndex = graphicsFamily; if (commandPool!=VK_NULL_HANDLE) { vkDestroyCommandPool(device, commandPool, nullptr); commandPool = VK_NULL_HANDLE; }
            if (vkCreateCommandPool(device, &cpci, nullptr, &commandPool)!=VK_SUCCESS) throw std::runtime_error("cmd pool");
            commandBuffers.resize(swapchainFramebuffers.size());
            VkCommandBufferAllocateInfo cbai{}; cbai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; cbai.commandPool=commandPool; cbai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount=(uint32_t)commandBuffers.size(); if (vkAllocateCommandBuffers(device, &cbai, commandBuffers.data())!=VK_SUCCESS) throw std::runtime_error("alloc cmds");
            for (size_t i=0;i<commandBuffers.size();++i){ VkCommandBufferBeginInfo binfo{}; binfo.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; vkBeginCommandBuffer(commandBuffers[i], &binfo); VkRenderPassBeginInfo rpbi{}; rpbi.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; rpbi.renderPass=renderPass; rpbi.framebuffer=swapchainFramebuffers[i]; rpbi.renderArea.offset={0,0}; rpbi.renderArea.extent=swapchainExtent; VkClearValue clearVal = { {{0.0f,0.0f,0.0f,1.0f}} }; rpbi.clearValueCount=1; rpbi.pClearValues=&clearVal; vkCmdBeginRenderPass(commandBuffers[i], &rpbi, VK_SUBPASS_CONTENTS_INLINE); vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline); vkCmdDraw(commandBuffers[i], 3, 1, 0, 0); vkCmdEndRenderPass(commandBuffers[i]); if (vkEndCommandBuffer(commandBuffers[i])!=VK_SUCCESS) throw std::runtime_error("end cb"); }
            // recreate per-image signal semaphores to match swapchain images
            if (!renderFinishedSemaphores.empty()) {
                for (auto s : renderFinishedSemaphores) vkDestroySemaphore(device, s, nullptr);
                renderFinishedSemaphores.clear();
            }
            renderFinishedSemaphores.resize(commandBuffers.size());
            VkSemaphoreCreateInfo localSemci{}; localSemci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            for (size_t ri=0; ri<renderFinishedSemaphores.size(); ++ri) {
                if (vkCreateSemaphore(device, &localSemci, nullptr, &renderFinishedSemaphores[ri]) != VK_SUCCESS) throw std::runtime_error("sem create");
            }
        };

        // initial creation
        createSwapchainAndResources(true);

        // synchronization: typical MAX_FRAMES_IN_FLIGHT=2
        const int MAX_FRAMES_IN_FLIGHT = 2;
        std::vector<VkSemaphore> imageAvailableSemaphores(MAX_FRAMES_IN_FLIGHT);
        std::vector<VkFence> inFlightFences(MAX_FRAMES_IN_FLIGHT);
        std::vector<VkFence> imagesInFlight; // sized to swapchain images after creation
        VkSemaphoreCreateInfo semci{}; semci.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fci{}; fci.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i=0;i<MAX_FRAMES_IN_FLIGHT;++i){ if (vkCreateSemaphore(device,&semci,nullptr,&imageAvailableSemaphores[i])!=VK_SUCCESS) throw std::runtime_error("sem create"); if (vkCreateFence(device,&fci,nullptr,&inFlightFences[i])!=VK_SUCCESS) throw std::runtime_error("fence create"); }
        // create per-image renderFinishedSemaphores sized to commandBuffers (swapchain images)
        renderFinishedSemaphores.resize(commandBuffers.size());
        for (size_t i=0;i<renderFinishedSemaphores.size(); ++i) { if (vkCreateSemaphore(device,&semci,nullptr,&renderFinishedSemaphores[i])!=VK_SUCCESS) throw std::runtime_error("sem create"); }
        imagesInFlight.resize(commandBuffers.size(), VK_NULL_HANDLE);

        size_t currentFrame = 0;
        while(!glfwWindowShouldClose(window)){
            glfwPollEvents();
            vkWaitForFences(device,1,&inFlightFences[currentFrame],VK_TRUE,UINT64_MAX);

            uint32_t imageIndex;
            VkResult acq = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
            if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
                createSwapchainAndResources(false);
                imagesInFlight.assign(commandBuffers.size(), VK_NULL_HANDLE);
                continue;
            } else if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
                throw std::runtime_error("Failed to acquire swap chain image");
            }

            // If a previous frame is using this image, wait for it
            if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
                vkWaitForFences(device,1,&imagesInFlight[imageIndex],VK_TRUE,UINT64_MAX);
            }
            // mark image as now being in use by this frame
            imagesInFlight[imageIndex] = inFlightFences[currentFrame];

            vkResetFences(device,1,&inFlightFences[currentFrame]);
            VkSubmitInfo submit{}; submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
            VkSemaphore waits[] = { imageAvailableSemaphores[currentFrame] };
            VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = waits; submit.pWaitDstStageMask = waitStages;
            submit.commandBufferCount = 1; submit.pCommandBuffers = &commandBuffers[imageIndex];
            VkSemaphore signals[] = { renderFinishedSemaphores[imageIndex] };
            submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = signals;
            if (vkQueueSubmit(graphicsQueue,1,&submit,inFlightFences[currentFrame])!=VK_SUCCESS) throw std::runtime_error("submit failed");

            VkPresentInfoKHR pres{}; pres.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pres.waitSemaphoreCount = 1; pres.pWaitSemaphores = signals;
            pres.swapchainCount = 1; pres.pSwapchains = &swapchain; pres.pImageIndices = &imageIndex;
            VkResult presRes = vkQueuePresentKHR(presentQueue,&pres);
            if (presRes == VK_ERROR_OUT_OF_DATE_KHR || presRes == VK_SUBOPTIMAL_KHR) {
                createSwapchainAndResources(false);
                imagesInFlight.assign(commandBuffers.size(), VK_NULL_HANDLE);
            }

            currentFrame = (currentFrame+1)%MAX_FRAMES_IN_FLIGHT;
        }

        vkDeviceWaitIdle(device);
        for (int i=0;i<MAX_FRAMES_IN_FLIGHT;++i){ vkDestroyFence(device,inFlightFences[i],nullptr); vkDestroySemaphore(device,imageAvailableSemaphores[i],nullptr); }
        for (auto s : renderFinishedSemaphores) vkDestroySemaphore(device, s, nullptr);
        for (auto fb: swapchainFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        for (auto iv: swapchainImageViews) vkDestroyImageView(device, iv, nullptr);
        if (graphicsPipeline!=VK_NULL_HANDLE) vkDestroyPipeline(device, graphicsPipeline, nullptr);
        if (pipelineLayout!=VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (renderPass!=VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
        if (commandPool!=VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
        if (swapchain!=VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
#ifdef ENABLE_VALIDATION
        if (debugMessenger != VK_NULL_HANDLE) DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
#endif
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    } catch (const std::exception& e){ std::cerr<<e.what() << "\n"; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
