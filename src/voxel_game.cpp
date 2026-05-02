#include <iostream>
#include <stdexcept>
#include <vector>
#include <optional>
#include <set>
#include <cstring>
#include <cstdint>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include "vert_spv.h"
#include "frag_spv.h"
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <map>
#include <deque>
#include <atomic>

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

// Mouse-look globals
static float g_camYaw = -90.0f;
static float g_camPitch = 0.0f;
static double g_lastX = 400.0, g_lastY = 300.0;
static bool g_firstMouse = true;

static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    if (g_firstMouse) { g_lastX = xpos; g_lastY = ypos; g_firstMouse = false; }
    double xoffset = xpos - g_lastX;
    double yoffset = g_lastY - ypos; // reversed: y ranges top to bottom
    g_lastX = xpos; g_lastY = ypos;
    const float sensitivity = 0.12f;
    g_camYaw += (float)xoffset * sensitivity;
    g_camPitch += (float)yoffset * sensitivity;
    if (g_camPitch > 89.0f) g_camPitch = 89.0f;
    if (g_camPitch < -89.0f) g_camPitch = -89.0f;
}

// Simple height function for terrain
static int heightAt(int x, int z) {
    float fx = x * 0.1f;
    float fz = z * 0.1f;
    float h = (sinf(fx) + cosf(fz)) * 4.0f + 8.0f; // simple wave height
    return (int)h;
}

// Forward declaration
static int getHeightAt(int x, int z);

// Build visible faces mesh for a single chunk at origin (ox,oz) of size CX*CY*CZ
static void buildChunkMeshAt(int ox, int oz, int CX, int CY, int CZ, std::vector<float>& outVerts) {
    outVerts.clear();
    auto pushFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 normal, glm::vec3 color){
        // triangle a,b,c then a,c,d ; for each vertex: pos(3), normal(3), color(3)
        auto pushV = [&](glm::vec3 v){ outVerts.push_back(v.x); outVerts.push_back(v.y); outVerts.push_back(v.z); };
        auto pushN = [&](glm::vec3 n){ outVerts.push_back(n.x); outVerts.push_back(n.y); outVerts.push_back(n.z); };
        auto pushC = [&](glm::vec3 ccol){ outVerts.push_back(ccol.r); outVerts.push_back(ccol.g); outVerts.push_back(ccol.b); };
        pushV(a); pushN(normal); pushC(color);
        pushV(b); pushN(normal); pushC(color);
        pushV(c); pushN(normal); pushC(color);
        pushV(a); pushN(normal); pushC(color);
        pushV(c); pushN(normal); pushC(color);
        pushV(d); pushN(normal); pushC(color);
    };
    for(int x=0;x<CX;++x){
        for(int z=0;z<CZ;++z){
            int worldX = ox + x;
            int worldZ = oz + z;
            int h = getHeightAt(worldX, worldZ);
            for(int y=0;y<=h && y<CY;++y){
                auto isEmpty = [&](int nx,int ny,int nz)->bool{
                    // Always check neighbor using global world coordinates so chunk borders query adjacent chunks
                    int wx = ox + nx; int wz = oz + nz;
                    if (ny < 0 || ny >= CY) return true; // outside vertical bounds -> empty
                    int hh = getHeightAt(wx, wz);
                    return ny > hh; // empty if above terrain height
                };
                float fx = (float)worldX;
                float fy = (float)y;
                float fz = (float)worldZ;
                glm::vec3 color = glm::vec3(0.6f, 0.5f, 0.3f); // default dirt
                // +X face
                if (isEmpty(x+1,y,z)){
                    glm::vec3 n = glm::vec3(1,0,0);
                    pushFace(glm::vec3(fx+1,fy, fz), glm::vec3(fx+1,fy, fz+1), glm::vec3(fx+1,fy+1, fz+1), glm::vec3(fx+1,fy+1,fz), n, color);
                }
                // -X face
                if (isEmpty(x-1,y,z)){
                    glm::vec3 n = glm::vec3(-1,0,0);
                    pushFace(glm::vec3(fx,fy, fz+1), glm::vec3(fx,fy, fz), glm::vec3(fx,fy+1,fz), glm::vec3(fx,fy+1, fz+1), n, color);
                }
                // +Z face
                if (isEmpty(x,y,z+1)){
                    glm::vec3 n = glm::vec3(0,0,1);
                    pushFace(glm::vec3(fx+1,fy,fz+1), glm::vec3(fx,fy,fz+1), glm::vec3(fx,fy+1,fz+1), glm::vec3(fx+1,fy+1,fz+1), n, color);
                }
                // -Z face
                if (isEmpty(x,y,z-1)){
                    glm::vec3 n = glm::vec3(0,0,-1);
                    pushFace(glm::vec3(fx,fy,fz), glm::vec3(fx+1,fy,fz), glm::vec3(fx+1,fy+1,fz), glm::vec3(fx,fy+1,fz), n, color);
                }
                // +Y face (top)
                if (isEmpty(x,y+1,z)){
                    glm::vec3 n = glm::vec3(0,1,0);
                    glm::vec3 topColor = glm::vec3(0.2f,0.7f,0.2f); // grass on top
                    pushFace(glm::vec3(fx,fy+1,fz), glm::vec3(fx+1,fy+1,fz), glm::vec3(fx+1,fy+1,fz+1), glm::vec3(fx,fy+1,fz+1), n, topColor);
                }
                // -Y face
                if (isEmpty(x,y-1,z)){
                    glm::vec3 n = glm::vec3(0,-1,0);
                    pushFace(glm::vec3(fx,fy,fz+1), glm::vec3(fx+1,fy,fz+1), glm::vec3(fx+1,fy,fz), glm::vec3(fx,fy,fz), n, color);
                }
            }
        }
    }
}

struct Chunk {
    int ox, oz; // origin
    std::vector<float> verts; // pos(3), normal(3), color(3)
    VkBuffer vb = VK_NULL_HANDLE;
    VkDeviceMemory vmem = VK_NULL_HANDLE;
    uint32_t vertexCount = 0; // number of vertices (not floats)
    bool uploaded = false;
};

// Globals for background build + interaction
static std::mutex g_buildMutex;
static std::condition_variable g_buildCv;
static std::deque<std::pair<int,int>> g_buildQueue; // ox, oz to build
struct BuiltChunk { int ox; int oz; std::vector<float> verts; uint32_t vertexCount; };
static std::deque<BuiltChunk> g_readyChunks;
static std::mutex g_readyMutex;
static std::map<std::pair<int,int>, int> g_heightOverrides; // (x,z) -> override height
static std::mutex g_overrideMutex;
static std::atomic<bool> g_builderRunning{false};


// Query height allowing overrides from interactions
static int getHeightAt(int x, int z) {
    std::lock_guard<std::mutex> lk(g_overrideMutex);
    auto it = g_heightOverrides.find({x,z});
    if (it != g_heightOverrides.end()) return it->second;
    return heightAt(x,z);
}

// Helper: create host-visible vertex buffer and upload
static void createVertexBuffer(VkPhysicalDevice physical, VkDevice device, const std::vector<float>& verts, VkBuffer& outBuf, VkDeviceMemory& outMem){
    if (verts.empty()) return;
    VkDeviceSize bufferSize = sizeof(float) * verts.size();
    VkBufferCreateInfo bci{}; bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bci.size = bufferSize; bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &outBuf) != VK_SUCCESS) throw std::runtime_error("failed create vertex buffer");
    VkMemoryRequirements memReq; vkGetBufferMemoryRequirements(device, outBuf, &memReq);
    VkMemoryAllocateInfo mai{}; mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; mai.allocationSize = memReq.size;
    VkPhysicalDeviceMemoryProperties memProps; vkGetPhysicalDeviceMemoryProperties(physical, &memProps);
    int memoryTypeIndex = -1;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memReq.memoryTypeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memoryTypeIndex = (int)i; break;
        }
    }
    if (memoryTypeIndex == -1) throw std::runtime_error("Failed to find memory type for vertex buffer");
    mai.memoryTypeIndex = memoryTypeIndex;
    if (vkAllocateMemory(device, &mai, nullptr, &outMem) != VK_SUCCESS) throw std::runtime_error("failed alloc vb mem");
    vkBindBufferMemory(device, outBuf, outMem, 0);
    void* data; vkMapMemory(device, outMem, 0, bufferSize, 0, &data); memcpy(data, verts.data(), (size_t)bufferSize); vkUnmapMemory(device, outMem);
}

int main(){
    std::freopen("voxel.stdout.txt","w",stdout);
    std::freopen("voxel.stderr.txt","w",stderr);
    try{
        if (!glfwInit()) throw std::runtime_error("Failed to init GLFW");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow* window = glfwCreateWindow(800,600,"Voxel Prototype",nullptr,nullptr);
        if (!window) throw std::runtime_error("Failed to create window");

        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwSetCursorPosCallback(window, cursor_pos_callback);

        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
        VkApplicationInfo appInfo{}; appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; appInfo.pApplicationName = "Voxel"; appInfo.applicationVersion = VK_MAKE_VERSION(1,0,0); appInfo.pEngineName = "No Engine"; appInfo.engineVersion = VK_MAKE_VERSION(1,0,0); appInfo.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo createInfo{}; createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; createInfo.pApplicationInfo = &appInfo;

        uint32_t glfwExtensionCount=0; const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions+glfwExtensionCount);
#ifdef ENABLE_VALIDATION
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
#else
        createInfo.enabledLayerCount = 0;
#endif
        createInfo.enabledExtensionCount = (uint32_t)extensions.size(); createInfo.ppEnabledExtensionNames = extensions.data();
#ifdef ENABLE_VALIDATION
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{}; debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT; debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT; debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; debugCreateInfo.pfnUserCallback = debugCallback; createInfo.pNext = &debugCreateInfo;
#endif
        if (vkCreateInstance(&createInfo,nullptr,&instance)!=VK_SUCCESS) throw std::runtime_error("Failed to create instance");
#ifdef ENABLE_VALIDATION
        VkDebugUtilsMessengerCreateInfoEXT dbgCreate{}; dbgCreate.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT; dbgCreate.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT; dbgCreate.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; dbgCreate.pfnUserCallback = debugCallback; VkResult dmRes = CreateDebugUtilsMessengerEXT(instance, &dbgCreate, nullptr, &debugMessenger); if (dmRes==VK_SUCCESS) std::cerr<<"Debug messenger created"<<std::endl;
#endif
        VkSurfaceKHR surface; if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) throw std::runtime_error("Failed to create surface");
        uint32_t deviceCount=0; vkEnumeratePhysicalDevices(instance,&deviceCount,nullptr); if (deviceCount==0) throw std::runtime_error("No GPU"); std::vector<VkPhysicalDevice> devices(deviceCount); vkEnumeratePhysicalDevices(instance,&deviceCount,devices.data()); VkPhysicalDevice physical = devices[0]; VkPhysicalDeviceProperties pdp; vkGetPhysicalDeviceProperties(physical,&pdp); std::cerr<<"Using GPU: "<<pdp.deviceName<<std::endl;

        uint32_t qCount=0; vkGetPhysicalDeviceQueueFamilyProperties(physical,&qCount,nullptr); std::vector<VkQueueFamilyProperties> qprops(qCount); vkGetPhysicalDeviceQueueFamilyProperties(physical,&qCount,qprops.data());
        int graphicsFamily=-1,presentFamily=-1; for(int i=0;i<(int)qprops.size();++i){ if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphicsFamily=i; VkBool32 pres=false; vkGetPhysicalDeviceSurfaceSupportKHR(physical,i,surface,&pres); if (pres) presentFamily=i; }
        if (graphicsFamily<0||presentFamily<0) throw std::runtime_error("No suitable queue"); float priority=1.0f; std::set<int> families={graphicsFamily,presentFamily}; std::vector<VkDeviceQueueCreateInfo> qcis; for(int f:families){ VkDeviceQueueCreateInfo qci{}; qci.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qci.queueFamilyIndex=f; qci.queueCount=1; qci.pQueuePriorities=&priority; qcis.push_back(qci);} 
        VkDevice device; VkDeviceCreateInfo dci{}; dci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; dci.queueCreateInfoCount=(uint32_t)qcis.size(); dci.pQueueCreateInfos=qcis.data(); const char* devExts[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME}; dci.enabledExtensionCount=1; dci.ppEnabledExtensionNames=devExts; if (vkCreateDevice(physical,&dci,nullptr,&device)!=VK_SUCCESS) throw std::runtime_error("Failed to create device");
        VkQueue graphicsQueue; vkGetDeviceQueue(device,graphicsFamily,0,&graphicsQueue); VkQueue presentQueue; vkGetDeviceQueue(device,presentFamily,0,&presentQueue);

        // create swapchain
        VkSurfaceCapabilitiesKHR caps; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps);
        uint32_t formatCount=0; vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&formatCount,nullptr); if (formatCount==0) throw std::runtime_error("No surface formats"); std::vector<VkSurfaceFormatKHR> formats(formatCount); vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&formatCount,formats.data());
        VkSurfaceFormatKHR surfaceFormat = formats[0];
        for(auto &f:formats){ if (f.format==VK_FORMAT_B8G8R8A8_UNORM){ surfaceFormat=f; break; } }
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        VkExtent2D extent{800,600};
        if (caps.currentExtent.width != UINT32_MAX) {
            extent = caps.currentExtent;
        } else {
            // clamp to supported extents
            extent.width = std::max(caps.minImageExtent.width, (uint32_t)std::min((int)extent.width, (int)caps.maxImageExtent.width));
            extent.height = std::max(caps.minImageExtent.height, (uint32_t)std::min((int)extent.height, (int)caps.maxImageExtent.height));
        }
        uint32_t imageCount = caps.minImageCount + 1; if (caps.maxImageCount>0 && imageCount>caps.maxImageCount) imageCount=caps.maxImageCount;
        VkSwapchainCreateInfoKHR sc{}; sc.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR; sc.surface=surface; sc.minImageCount=imageCount; sc.imageFormat=surfaceFormat.format; sc.imageColorSpace=surfaceFormat.colorSpace; sc.imageExtent=extent; sc.imageArrayLayers=1; sc.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; uint32_t qfIndices[2]={(uint32_t)graphicsFamily,(uint32_t)presentFamily}; if (graphicsFamily!=presentFamily){ sc.imageSharingMode = VK_SHARING_MODE_CONCURRENT; sc.queueFamilyIndexCount=2; sc.pQueueFamilyIndices=qfIndices; } else { sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; sc.queueFamilyIndexCount=0; sc.pQueueFamilyIndices=nullptr; }
        sc.preTransform = caps.currentTransform; sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; sc.presentMode = presentMode; sc.clipped = VK_TRUE; sc.oldSwapchain = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain; if (vkCreateSwapchainKHR(device,&sc,nullptr,&swapchain)!=VK_SUCCESS) throw std::runtime_error("Failed create swapchain");
        uint32_t scImgCount=0; vkGetSwapchainImagesKHR(device, swapchain, &scImgCount, nullptr); std::vector<VkImage> swapImages(scImgCount); vkGetSwapchainImagesKHR(device, swapchain, &scImgCount, swapImages.data());
        std::vector<VkImageView> swapImageViews(scImgCount);
        for(size_t i=0;i<swapImages.size();++i){ VkImageViewCreateInfo iv{}; iv.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; iv.image=swapImages[i]; iv.viewType=VK_IMAGE_VIEW_TYPE_2D; iv.format=surfaceFormat.format; iv.components.r=VK_COMPONENT_SWIZZLE_IDENTITY; iv.components.g=VK_COMPONENT_SWIZZLE_IDENTITY; iv.components.b=VK_COMPONENT_SWIZZLE_IDENTITY; iv.components.a=VK_COMPONENT_SWIZZLE_IDENTITY; iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; iv.subresourceRange.baseMipLevel=0; iv.subresourceRange.levelCount=1; iv.subresourceRange.baseArrayLayer=0; iv.subresourceRange.layerCount=1; vkCreateImageView(device,&iv,nullptr,&swapImageViews[i]); }

        // render pass
        VkAttachmentDescription colorAttach{}; colorAttach.format = surfaceFormat.format; colorAttach.samples = VK_SAMPLE_COUNT_1_BIT; colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE; colorAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; colorAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; colorAttach.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference colorRef{}; colorRef.attachment = 0; colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &colorRef;
        VkRenderPassCreateInfo rp{}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rp.attachmentCount = 1; rp.pAttachments = &colorAttach; rp.subpassCount = 1; rp.pSubpasses = &subpass;
        VkRenderPass renderPass; if (vkCreateRenderPass(device,&rp,nullptr,&renderPass)!=VK_SUCCESS) throw std::runtime_error("Failed create render pass");

        // pipeline
        VkShaderModule vertModule; VkShaderModule fragModule;
        auto createModule = [&](const uint32_t* code, size_t codeSize, VkShaderModule* out){ VkShaderModuleCreateInfo smi{}; smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; smi.codeSize = codeSize; smi.pCode = code; if (vkCreateShaderModule(device,&smi,nullptr,out)!=VK_SUCCESS) throw std::runtime_error("failed create shader module"); };
        createModule(reinterpret_cast<const uint32_t*>(vert_spv), vert_spv_size, &vertModule);
        createModule(reinterpret_cast<const uint32_t*>(frag_spv), frag_spv_size, &fragModule);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertModule; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragModule; stages[1].pName = "main";

        // vertex input: pos(3 floats) normal(3) color(3)
        VkVertexInputBindingDescription vib{}; vib.binding = 0; vib.stride = sizeof(float)*9; vib.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0].binding = 0; attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
        attrs[1].binding = 0; attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = sizeof(float)*3;
        attrs[2].binding = 0; attrs[2].location = 2; attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[2].offset = sizeof(float)*6;
        VkPipelineVertexInputStateCreateInfo vpi{}; vpi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; vpi.vertexBindingDescriptionCount = 1; vpi.pVertexBindingDescriptions = &vib; vpi.vertexAttributeDescriptionCount = 3; vpi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; ia.primitiveRestartEnable = VK_FALSE;
        VkViewport vp{}; vp.x=0; vp.y=0; vp.width = (float)extent.width; vp.height = (float)extent.height; vp.minDepth=0.0f; vp.maxDepth=1.0f;
        VkRect2D scissor{}; scissor.offset={0,0}; scissor.extent=extent;
        VkPipelineViewportStateCreateInfo vpst{}; vpst.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vpst.viewportCount = 1; vpst.pViewports = &vp; vpst.scissorCount = 1; vpst.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo rast{}; rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rast.depthClampEnable=VK_FALSE; rast.rasterizerDiscardEnable=VK_FALSE; rast.polygonMode = VK_POLYGON_MODE_FILL; rast.lineWidth = 1.0f; rast.cullMode = VK_CULL_MODE_BACK_BIT; rast.frontFace = VK_FRONT_FACE_CLOCKWISE; rast.depthBiasEnable = VK_FALSE;
        VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cbatt{}; cbatt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT; cbatt.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = 1; cb.pAttachments = &cbatt;
        VkPipelineLayout pipelineLayout;
        VkPushConstantRange pcr{}; pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; pcr.offset = 0; pcr.size = sizeof(glm::mat4);
        VkPipelineLayoutCreateInfo plci{}; plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr; if (vkCreatePipelineLayout(device,&plci,nullptr,&pipelineLayout)!=VK_SUCCESS) throw std::runtime_error("failed create pipeline layout");
        VkGraphicsPipelineCreateInfo gpci{}; gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gpci.stageCount=2; gpci.pStages=stages; gpci.pVertexInputState = &vpi; gpci.pInputAssemblyState = &ia; gpci.pViewportState = &vpst; gpci.pRasterizationState = &rast; gpci.pMultisampleState = &ms; gpci.pColorBlendState = &cb; gpci.layout = pipelineLayout; gpci.renderPass = renderPass; gpci.subpass = 0;
        VkPipeline pipeline; if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline) != VK_SUCCESS) throw std::runtime_error("failed create pipeline");

        // framebuffers
        std::vector<VkFramebuffer> framebuffers(swapImageViews.size());
        for(size_t i=0;i<swapImageViews.size();++i){ VkImageView iv = swapImageViews[i]; VkFramebufferCreateInfo fbi{}; fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fbi.renderPass = renderPass; fbi.attachmentCount = 1; fbi.pAttachments = &iv; fbi.width = extent.width; fbi.height = extent.height; fbi.layers = 1; if (vkCreateFramebuffer(device,&fbi,nullptr,&framebuffers[i])!=VK_SUCCESS) throw std::runtime_error("failed create fb"); }

        // command pool
        VkCommandPoolCreateInfo cpci{}; cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; cpci.queueFamilyIndex = graphicsFamily; cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCommandPool cmdPool; if (vkCreateCommandPool(device,&cpci,nullptr,&cmdPool)!=VK_SUCCESS) throw std::runtime_error("failed create cmd pool");
        std::vector<VkCommandBuffer> cmdBuffers(swapImageViews.size()); VkCommandBufferAllocateInfo cbai{}; cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; cbai.commandPool = cmdPool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = (uint32_t)cmdBuffers.size(); if (vkAllocateCommandBuffers(device,&cbai,cmdBuffers.data())!=VK_SUCCESS) throw std::runtime_error("failed alloc cmd buffers");

        // sync
        std::vector<VkSemaphore> imageAvailable(swapImageViews.size()); std::vector<VkSemaphore> renderFinished(swapImageViews.size()); std::vector<VkFence> inFlightFences(swapImageViews.size());
        for(size_t i=0;i<swapImageViews.size();++i){ VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO; vkCreateSemaphore(device,&sci,nullptr,&imageAvailable[i]); vkCreateSemaphore(device,&sci,nullptr,&renderFinished[i]); VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT; vkCreateFence(device,&fci,nullptr,&inFlightFences[i]); }

        // Build chunk grid around origin
        const int CHUNK_SIZE = 16; const int CHUNK_HEIGHT = 32;
        const int viewDistance = 2; // in chunks
        const int grid = viewDistance*2 + 1;
        std::vector<Chunk> chunks; chunks.reserve(grid*grid);
        int centerOx = - (grid/2) * CHUNK_SIZE;
        int centerOz = - (grid/2) * CHUNK_SIZE;
        for(int gz=0; gz<grid; ++gz){ for(int gx=0; gx<grid; ++gx){ Chunk c; c.ox = centerOx + gx*CHUNK_SIZE; c.oz = centerOz + gz*CHUNK_SIZE; buildChunkMeshAt(c.ox, c.oz, CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE, c.verts); c.vertexCount = (uint32_t)(c.verts.size() / 9); if (!c.verts.empty()) createVertexBuffer(physical, device, c.verts, c.vb, c.vmem), c.uploaded = true; chunks.push_back(std::move(c)); }}

        // Start background builder thread (will build meshes on demand)
        g_builderRunning = true;
        std::thread builderThread([&](){
            while (g_builderRunning) {
                std::pair<int,int> task;
                {
                    std::unique_lock<std::mutex> lk(g_buildMutex);
                    g_buildCv.wait(lk, [&]{ return !g_buildQueue.empty() || !g_builderRunning; });
                    if (!g_builderRunning) break;
                    task = g_buildQueue.front(); g_buildQueue.pop_front();
                }
                BuiltChunk bc; bc.ox = task.first; bc.oz = task.second;
                buildChunkMeshAt(bc.ox, bc.oz, CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE, bc.verts);
                bc.vertexCount = (uint32_t)(bc.verts.size() / 9);
                {
                    std::lock_guard<std::mutex> lk2(g_readyMutex);
                    g_readyChunks.push_back(std::move(bc));
                }
            }
        });

        // Simple physics loop with AABB collision against unit blocks
        glm::vec3 playerPos = glm::vec3(0.0f + CHUNK_SIZE*grid/2.0f, 20.0f, 0.0f + CHUNK_SIZE*grid/2.0f);
        glm::vec3 playerVel = glm::vec3(0.0f);
        const float playerHalfWidth = 0.3f;
        const float playerHeight = 1.8f;
        const float gravity = -20.0f;
        bool grounded = false;
        double lastTimeLoop = glfwGetTime();
        size_t currentFrame = 0;
        while(!glfwWindowShouldClose(window)){
            glfwPollEvents();
            double now = glfwGetTime(); float dt = (float)(now - lastTimeLoop); if (dt>0.1f) dt=0.1f; lastTimeLoop = now;

            // compute orientation
            float yawRad = glm::radians(g_camYaw);
            float pitchRad = glm::radians(g_camPitch);
            glm::vec3 forwardDir; forwardDir.x = cos(pitchRad)*cos(yawRad); forwardDir.y = sin(pitchRad); forwardDir.z = cos(pitchRad)*sin(yawRad);
            glm::vec3 flatForward = glm::normalize(glm::vec3(forwardDir.x, 0.0f, forwardDir.z));
            glm::vec3 right = glm::normalize(glm::cross(flatForward, glm::vec3(0.0f,1.0f,0.0f)));

            // input
            glm::vec3 moveDir(0.0f);
            const float moveSpeed = 5.0f;
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += flatForward;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= flatForward;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += right;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= right;
            if (glm::length(moveDir) > 0.001f) moveDir = glm::normalize(moveDir);

            // apply controls to velocity (horizontal)
            playerVel.x = moveDir.x * moveSpeed;
            playerVel.z = moveDir.z * moveSpeed;

            // jump
            if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && grounded) { playerVel.y = 7.0f; grounded = false; }

            // gravity
            playerVel.y += gravity * dt;

            // helpers (world bounds approximate)
            auto isSolidBlockAt = [&](int bx,int by,int bz)->bool{ int wx = bx; int wz = bz; if (by<0||by>=CHUNK_HEIGHT) return false; int hh = getHeightAt(wx,wz); return by<=hh; };
            auto aabbIntersectsBlock = [&](glm::vec3 pos, int bx, int by, int bz)->bool{
                float minX = pos.x - playerHalfWidth; float maxX = pos.x + playerHalfWidth;
                float minY = pos.y; float maxY = pos.y + playerHeight;
                float minZ = pos.z - playerHalfWidth; float maxZ = pos.z + playerHalfWidth;
                if (maxX <= bx || minX >= bx+1) return false;
                if (maxY <= by || minY >= by+1) return false;
                if (maxZ <= bz || minZ >= bz+1) return false;
                return true;
            };

            // axis-resolved movement
            // X
            glm::vec3 nextPos = playerPos + glm::vec3(playerVel.x * dt, 0.0f, 0.0f);
            int minX = (int)floor(nextPos.x - playerHalfWidth), maxX = (int)floor(nextPos.x + playerHalfWidth);
            int minY = (int)floor(nextPos.y), maxY = (int)floor(nextPos.y + playerHeight);
            int minZ = (int)floor(nextPos.z - playerHalfWidth), maxZ = (int)floor(nextPos.z + playerHalfWidth);
            bool collide = false;
            for(int bx=minX; bx<=maxX && !collide; ++bx) for(int by=minY; by<=maxY && !collide; ++by) for(int bz=minZ; bz<=maxZ && !collide; ++bz) {
                if (isSolidBlockAt(bx,by,bz) && aabbIntersectsBlock(nextPos,bx,by,bz)) collide = true;
            }
            if (!collide) playerPos.x = nextPos.x; else playerVel.x = 0.0f;

            // Y
            nextPos = playerPos + glm::vec3(0.0f, playerVel.y * dt, 0.0f);
            minX = (int)floor(nextPos.x - playerHalfWidth); maxX = (int)floor(nextPos.x + playerHalfWidth);
            minY = (int)floor(nextPos.y), maxY = (int)floor(nextPos.y + playerHeight);
            minZ = (int)floor(nextPos.z - playerHalfWidth); maxZ = (int)floor(nextPos.z + playerHalfWidth);
            collide = false;
            for(int bx=minX; bx<=maxX && !collide; ++bx) for(int by=minY; by<=maxY && !collide; ++by) for(int bz=minZ; bz<=maxZ && !collide; ++bz) {
                if (isSolidBlockAt(bx,by,bz) && aabbIntersectsBlock(nextPos,bx,by,bz)) collide = true;
            }
            if (!collide) { playerPos.y = nextPos.y; grounded = false; } else { if (playerVel.y < 0.0f) grounded = true; playerVel.y = 0.0f; }

            // Z
            nextPos = playerPos + glm::vec3(0.0f, 0.0f, playerVel.z * dt);
            minX = (int)floor(nextPos.x - playerHalfWidth); maxX = (int)floor(nextPos.x + playerHalfWidth);
            minY = (int)floor(nextPos.y), maxY = (int)floor(nextPos.y + playerHeight);
            minZ = (int)floor(nextPos.z - playerHalfWidth); maxZ = (int)floor(nextPos.z + playerHalfWidth);
            collide = false;
            for(int bx=minX; bx<=maxX && !collide; ++bx) for(int by=minY; by<=maxY && !collide; ++by) for(int bz=minZ; bz<=maxZ && !collide; ++bz) {
                if (isSolidBlockAt(bx,by,bz) && aabbIntersectsBlock(nextPos,bx,by,bz)) collide = true;
            }
            if (!collide) playerPos.z = nextPos.z; else playerVel.z = 0.0f;

            // camera / view matrix
            glm::vec3 eye = playerPos + glm::vec3(0.0f, 1.6f, 0.0f);
            glm::vec3 lookDir; lookDir.x = cos(pitchRad)*cos(yawRad); lookDir.y = sin(pitchRad); lookDir.z = cos(pitchRad)*sin(yawRad);
            glm::vec3 center = eye + lookDir;
            glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0,1,0));
            glm::mat4 proj = glm::perspective(glm::radians(60.0f), (float)extent.width/(float)extent.height, 0.1f, 1000.0f);
            proj[1][1] *= -1; // Vulkan clip
            glm::mat4 vp = proj * view;

            // process any built chunks from background thread and upload
            {
                std::lock_guard<std::mutex> lk(g_readyMutex);
                while(!g_readyChunks.empty()){
                    BuiltChunk bc = std::move(g_readyChunks.front()); g_readyChunks.pop_front();
                    for(auto &c : chunks){
                        if (c.ox == bc.ox && c.oz == bc.oz){
                            if (c.vb != VK_NULL_HANDLE) vkDestroyBuffer(device, c.vb, nullptr);
                            if (c.vmem != VK_NULL_HANDLE) vkFreeMemory(device, c.vmem, nullptr);
                            c.verts = std::move(bc.verts);
                            c.vertexCount = bc.vertexCount;
                            if (!c.verts.empty()) { createVertexBuffer(physical, device, c.verts, c.vb, c.vmem); c.uploaded = true; } else { c.uploaded = false; }
                            break;
                        }
                    }
                }
            }

            // handle mouse interactions (edge-triggered)
            static bool prevLeft = false, prevRight = false, prevEsc = false;
            int leftState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
            int rightState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);
            int escState = glfwGetKey(window, GLFW_KEY_ESCAPE);
            if (escState == GLFW_PRESS && !prevEsc) {
#ifdef _WIN32
                // simple native menu dialog with "退出游戏" option
                // show cursor while in menu
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                int resp = MessageBoxA(NULL, "菜单\n\n按 OK 退出游戏，按 Cancel 返回游戏", "菜单", MB_OKCANCEL | MB_TOPMOST | MB_SETFOREGROUND | MB_TASKMODAL);
                if (resp == IDOK) {
                    // request window close and break main loop
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
                // restore cursor mode
                if (!glfwWindowShouldClose(window)) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
#endif
            }
            prevEsc = (escState == GLFW_PRESS);
            if (leftState == GLFW_PRESS && !prevLeft) {
                // raycast and remove first solid block hit
                for(float t=0.0f; t<6.0f; t+=0.1f){
                    glm::vec3 p = eye + lookDir * t;
                    int bx = (int)floor(p.x); int by = (int)floor(p.y); int bz = (int)floor(p.z);
                    int h = getHeightAt(bx,bz);
                    if (by <= h){
                        std::lock_guard<std::mutex> ol(g_overrideMutex);
                        g_heightOverrides[{bx,bz}] = h-1;
                        int cx = (int)floor((float)bx / CHUNK_SIZE); int cz = (int)floor((float)bz / CHUNK_SIZE);
                        int ox = cx * CHUNK_SIZE; int oz = cz * CHUNK_SIZE;
                        { std::lock_guard<std::mutex> bl(g_buildMutex); g_buildQueue.emplace_back(ox,oz); g_buildCv.notify_one(); }
                        break;
                    }
                }
            }
            if (rightState == GLFW_PRESS && !prevRight) {
                // raycast and place block on top of column
                for(float t=0.0f; t<6.0f; t+=0.1f){
                    glm::vec3 p = eye + lookDir * t;
                    int bx = (int)floor(p.x); int bz = (int)floor(p.z);
                    int h = getHeightAt(bx,bz);
                    if (h+1 < CHUNK_HEIGHT){
                        std::lock_guard<std::mutex> ol(g_overrideMutex);
                        g_heightOverrides[{bx,bz}] = h+1;
                        int cx = (int)floor((float)bx / CHUNK_SIZE); int cz = (int)floor((float)bz / CHUNK_SIZE);
                        int ox = cx * CHUNK_SIZE; int oz = cz * CHUNK_SIZE;
                        { std::lock_guard<std::mutex> bl(g_buildMutex); g_buildQueue.emplace_back(ox,oz); g_buildCv.notify_one(); }
                        break;
                    }
                }
            }
            prevLeft = (leftState == GLFW_PRESS);
            prevRight = (rightState == GLFW_PRESS);

            // draw frame
            vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
            vkResetFences(device, 1, &inFlightFences[currentFrame]);
            uint32_t imageIndex;
            VkResult acq = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex);
            if (acq == VK_ERROR_OUT_OF_DATE_KHR) break;

            // record command buffer for this image
            vkResetCommandBuffer(cmdBuffers[imageIndex], 0);
            VkCommandBufferBeginInfo cbbi{}; cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            vkBeginCommandBuffer(cmdBuffers[imageIndex], &cbbi);
            VkClearValue clearColor{}; clearColor.color.float32[0]=0.53f; clearColor.color.float32[1]=0.8f; clearColor.color.float32[2]=0.92f; clearColor.color.float32[3]=1.0f;
            VkRenderPassBeginInfo rpbi{}; rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; rpbi.renderPass = renderPass; rpbi.framebuffer = framebuffers[imageIndex]; rpbi.renderArea.offset = {0,0}; rpbi.renderArea.extent = extent; rpbi.clearValueCount = 1; rpbi.pClearValues = &clearColor;
            vkCmdBeginRenderPass(cmdBuffers[imageIndex], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmdBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            // push vp matrix
            vkCmdPushConstants(cmdBuffers[imageIndex], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vp);
            // draw all chunks
            for(const Chunk& c : chunks){ if (!c.uploaded || c.vertexCount==0) continue; VkDeviceSize offsets[1] = {0}; vkCmdBindVertexBuffers(cmdBuffers[imageIndex], 0, 1, &c.vb, offsets); vkCmdDraw(cmdBuffers[imageIndex], c.vertexCount, 1, 0, 0); }
            vkCmdEndRenderPass(cmdBuffers[imageIndex]);
            vkEndCommandBuffer(cmdBuffers[imageIndex]);

            VkSemaphore waitSem = imageAvailable[currentFrame]; VkSemaphore signalSem = renderFinished[currentFrame];
            VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; VkSemaphore waits[] = {waitSem}; VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT}; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = waits; submit.pWaitDstStageMask = waitStages; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmdBuffers[imageIndex]; VkSemaphore signals[] = {signalSem}; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = signals;
            if (vkQueueSubmit(graphicsQueue, 1, &submit, inFlightFences[currentFrame]) != VK_SUCCESS) throw std::runtime_error("failed submit");

            VkPresentInfoKHR prez{}; prez.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR; prez.waitSemaphoreCount = 1; prez.pWaitSemaphores = signals; prez.swapchainCount = 1; prez.pSwapchains = &swapchain; prez.pImageIndices = &imageIndex;
            vkQueuePresentKHR(presentQueue, &prez);

            currentFrame = (currentFrame + 1) % inFlightFences.size();

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Stop builder thread and cleanup chunks
        g_builderRunning = false;
        g_buildCv.notify_all();
        try { if (builderThread.joinable()) builderThread.join(); } catch(...) {}
        for(auto &c: chunks){ if (c.vb!=VK_NULL_HANDLE) vkDestroyBuffer(device, c.vb, nullptr); if (c.vmem!=VK_NULL_HANDLE) vkFreeMemory(device, c.vmem, nullptr); }

        // Cleanup
        vkDeviceWaitIdle(device);
        for(auto fb: framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        for(auto iv: swapImageViews) vkDestroyImageView(device, iv, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        for(auto s: imageAvailable) vkDestroySemaphore(device, s, nullptr);
        for(auto s: renderFinished) vkDestroySemaphore(device, s, nullptr);
        for(auto f: inFlightFences) vkDestroyFence(device, f, nullptr);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
#ifdef ENABLE_VALIDATION
        if (debugMessenger != VK_NULL_HANDLE) DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
#endif
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }catch(const std::exception& e){ std::cerr<<e.what()<<std::endl; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
