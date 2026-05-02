#include <iostream>
#include <vulkan/vulkan.h>

int main() {
    uint32_t ver = 0;
#if VK_HEADER_VERSION >= 70
    if (vkEnumerateInstanceVersion != nullptr) {
        vkEnumerateInstanceVersion(&ver);
    }
#endif
    if (ver == 0) {
        std::cout << "Vulkan runtime or loader did not provide vkEnumerateInstanceVersion; assuming 1.0\n";
    } else {
        uint32_t major = VK_VERSION_MAJOR(ver);
        uint32_t minor = VK_VERSION_MINOR(ver);
        uint32_t patch = VK_VERSION_PATCH(ver);
        std::cout << "Vulkan API version (runtime): " << major << "." << minor << "." << patch << "\n";
    }
    return 0;
}
