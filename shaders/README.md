# 着色器

存放 GLSL 着色器源码 (.vert, .frag) 及编译后的 SPIR-V 二进制文件 (.spv)。

## 编译

需要 `glslc` (Vulkan SDK) 或 `glslangValidator`：

```bash
glslc shader.vert -o shader.vert.spv
glslc shader.frag -o shader.frag.spv
```
