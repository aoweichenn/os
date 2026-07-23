export interface CodeWalkthrough {
  index: string;
  title: string;
  description: string;
  files: readonly string[];
}

export interface CodeFileGuide {
  role: string;
  focusPoints: readonly string[];
}

export const codeWalkthroughs: readonly CodeWalkthrough[] = [
  {
    index: "01",
    title: "先看工程入口",
    description: "理解一条命令如何检查工具链、配置 CMake、构建目标并运行全部测试。",
    files: [
      "scripts/build_and_test.sh",
      "scripts/check_toolchain.sh",
      "CMakePresets.json",
      "CMakeLists.txt",
    ],
  },
  {
    index: "02",
    title: "再看第一个领域模块",
    description: "从公开类型和失败语义进入实现，理解半开地址区间为什么适合内核。",
    files: [
      "source/foundation/include/os/foundation/address_range.hpp",
      "source/foundation/address_range.cpp",
      "docs/modules/foundation.md",
    ],
  },
  {
    index: "03",
    title: "沿测试层向外扩展",
    description: "依次阅读单元、集成、随机和 QEMU 测试，观察每层证明的结论不同。",
    files: [
      "tests/unit/address_range_test.cpp",
      "tests/integration/boot_memory_layout_test.cpp",
      "tests/randomized/address_range_randomized_test.cpp",
      "tests/system/qemu_hardware_smoke.sh",
    ],
  },
  {
    index: "04",
    title: "最后看门户如何发布",
    description: "理解仓库 Markdown 和源码如何在构建期变成纯静态文档与代码浏览页面。",
    files: [
      "scripts/generate_code_catalog.mjs",
      "app/docs/[slug]/page.tsx",
      "app/code/[...path]/page.tsx",
      "site/worker.mjs",
    ],
  },
] as const;

export const codeFileGuides: Readonly<Record<string, CodeFileGuide>> = {
  "CMakeLists.txt": {
    role: "定义宿主测试库、x86-64 freestanding 库、镜像产物和测试子目录，是原生工程构建图的中心。",
    focusPoints: [
      "同一份 foundation 源码为什么同时生成 host 与 x86-64 两个目标。",
      "freestanding 编译选项如何关闭异常、RTTI、红区和宿主标准库。",
      "空 ROM 与空磁盘如何成为显式构建产物，而不是测试脚本中的隐式副作用。",
    ],
  },
  "scripts/build_and_test.sh": {
    role: "项目的最短验证入口，把工具链、配置、构建和 CTest 串成一次可复现操作。",
    focusPoints: [
      "为什么先检查工具链，再让 CMake 开始配置。",
      "为什么脚本只编排稳定命令，不复制 CMake 中的构建逻辑。",
      "任何一步失败都会因严格 Shell 选项立即终止。",
    ],
  },
  "scripts/check_toolchain.sh": {
    role: "在构建前验证全部显式工具依赖，并打印用于复现问题的版本证据。",
    focusPoints: [
      "NASM、QEMU 和 GDB 即使在 v0.0 尚未全面使用，也必须进入基线。",
      "LLVM 符号与 ELF 检查工具是验收的一部分，不能在 CI 中静默缺失。",
      "检查失败发生在配置前，错误信息比后续模糊的构建失败更直接。",
    ],
  },
  "source/foundation/include/os/foundation/address_range.hpp": {
    role: "声明物理地址、字节数、半开地址区间和显式创建状态，是模块对外契约。",
    focusPoints: [
      "不同概念不再全部使用裸整数，从接口层减少单位与地址空间混淆。",
      "普通成员函数只有声明，具体实现位于 .cpp，保持头源分离。",
      "错误使用 enum class 表达，调用方不能忽略溢出语义。",
    ],
  },
  "source/foundation/address_range.cpp": {
    role: "实现地址区间的创建、包含和重叠规则，并保证失败时输出对象不变。",
    focusPoints: [
      "先用 maximum - begin 判断可用长度，避免 begin + size 本身先发生回绕。",
      "所有类成员访问显式使用 this->，与项目 C++ 规范一致。",
      "半开区间让 size、空区间和相邻区间具有统一数学语义。",
    ],
  },
  "tests/unit/address_range_test.cpp": {
    role: "用明确样例验证地址类型、空区间、边界包含关系和溢出失败原子性。",
    focusPoints: [
      "单元测试负责最小行为，不引入 ROM 或内核布局等跨模块事实。",
      "每个有语义的值和断言说明都使用具名常量。",
      "溢出后输出对象保持原值，是失败路径的重要后置条件。",
    ],
  },
  "tests/integration/boot_memory_layout_test.cpp": {
    role: "把 foundation 地址区间与未来启动链的真实地址规划组合起来验证。",
    focusPoints: [
      "128 KiB ROM 覆盖 0xFFFE0000，并包含 0xFFFFFFF0 复位向量。",
      "Stage 1、内核和 ROM 计划加载区间必须互不重叠。",
      "它验证模块组合关系，不声称这些启动阶段已经实现。",
    ],
  },
  "tests/randomized/address_range_randomized_test.cpp": {
    role: "使用固定种子生成 10,000 组输入，以 100,000 次断言验证通用性质。",
    focusPoints: [
      "固定种子让本地与 CI 重放完全相同的输入序列。",
      "生成器同时覆盖合法区间、相邻区间和必然溢出的非法区间。",
      "失败输出携带种子和迭代位置，便于把随机故障固化为回归样例。",
    ],
  },
  "tests/system/qemu_hardware_smoke.sh": {
    role: "验证自定义空 ROM、空磁盘和 QEMU TCG 硬件参数能形成稳定测试环境。",
    focusPoints: [
      "通过 -bios 显式提供项目镜像，不使用默认 SeaBIOS 或 OVMF。",
      "通过 -S 暂停 CPU，因此测试不冒充已经实现启动代码。",
      "超时状态是预期结果；异常提前退出才代表硬件配置失败。",
    ],
  },
  "scripts/generate_code_catalog.mjs": {
    role: "在构建期从明确允许的项目目录生成源码目录和文件内容索引。",
    focusPoints: [
      "只收录项目源码、脚本、配置和文档，排除部署元数据、依赖锁和生成目录。",
      "生产页面读取生成的静态 JSON，不需要运行时文件系统或 Git。",
      "语言、行数和大小在生成阶段确定，浏览器只负责交互。",
    ],
  },
  "app/docs/[slug]/page.tsx": {
    role: "把登记的仓库 Markdown 构建为每篇文档的独立静态页面。",
    focusPoints: [
      "generateStaticParams 明确所有允许发布的文档路由。",
      "正文只从固定 docs 目录读取，避免任意路径访问。",
      "构建后不再需要 Node.js 文件系统。",
    ],
  },
  "app/code/[...path]/page.tsx": {
    role: "为目录中的每个文件生成独立静态路由，并组合元数据、走读说明、高亮源码和相邻导航。",
    focusPoints: [
      "generateStaticParams 从构建期目录生成所有文件路由，不接受生产环境中的任意路径。",
      "文件内容和语法高亮都在构建时完成，生产请求只读取静态 HTML。",
      "关键文件从 codeFileGuides 获取人工编写的中文说明，普通文件仍保留完整浏览能力。",
    ],
  },
  "components/code_directory_tree.tsx": {
    role: "在浏览器中提供目录展开、当前文件高亮、路径搜索和键盘快捷键。",
    focusPoints: [
      "客户端只接收文件路径，不接收整个源码目录内容，控制首屏数据量。",
      "当前 URL 的祖先目录自动展开，切换文件后仍保留用户手动展开状态。",
      "搜索只匹配已发布目录，无法构造路径读取未收录文件。",
    ],
  },
  "components/code_viewer.tsx": {
    role: "为静态高亮结果增加复制路径、复制源码和可分享行号交互。",
    focusPoints: [
      "Shiki 已在构建期转义并高亮源码，客户端不执行源码内容。",
      "点击任意源码行只更新 URL hash，不触发服务端请求。",
      "原始源码只传给当前文件页面，用于复制功能，不把全部仓库打进客户端包。",
    ],
  },
  "site/worker.mjs": {
    role: "生产环境唯一 Worker，只把请求交给静态资源绑定并兼容 favicon 路径。",
    focusPoints: [
      "不加载 Next.js 服务端，也不依赖 CommonJS require。",
      "所有应用页面都已在构建时变成静态 HTML、CSS 和浏览器脚本。",
      "Worker 保持纯 ESM 和最小职责，降低 Cloudflare 运行时故障面。",
    ],
  },
};
