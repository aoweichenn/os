export interface ProjectDocument {
  slug: string;
  title: string;
  description: string;
  repositoryPath: string;
  category: "入门" | "设计" | "工程" | "记录" | "决策";
}

export const projectDocuments: readonly ProjectDocument[] = [
  {
    slug: "background-knowledge",
    title: "背景知识",
    description: "从宿主机、交叉编译和 QEMU TCG，一直理解到 x86-64 复位向量。",
    repositoryPath: "docs/background_knowledge.md",
    category: "入门",
  },
  {
    slug: "code-walkthrough",
    title: "代码走读指南",
    description: "按依赖方向阅读构建、基础模块、测试和门户代码。",
    repositoryPath: "docs/code_walkthrough.md",
    category: "入门",
  },
  {
    slug: "requirements",
    title: "项目需求",
    description: "项目目标、固定技术边界、质量要求和当前范围。",
    repositoryPath: "docs/requirements.md",
    category: "设计",
  },
  {
    slug: "architecture",
    title: "项目架构",
    description: "启动链、处理器模式、模块边界和依赖方向。",
    repositoryPath: "docs/architecture.md",
    category: "设计",
  },
  {
    slug: "roadmap",
    title: "开发路线",
    description: "从工程基线到用户环境的 13 个可验收阶段。",
    repositoryPath: "docs/roadmap.md",
    category: "设计",
  },
  {
    slug: "building",
    title: "构建说明",
    description: "工具链、构建命令、目标产物和 freestanding 编译边界。",
    repositoryPath: "docs/building.md",
    category: "工程",
  },
  {
    slug: "testing",
    title: "测试策略",
    description: "单元、集成、固定种子随机测试和 QEMU 系统测试。",
    repositoryPath: "docs/testing.md",
    category: "工程",
  },
  {
    slug: "foundation-module",
    title: "Foundation 模块",
    description: "物理地址、字节数、半开区间、不变量和失败语义。",
    repositoryPath: "docs/modules/foundation.md",
    category: "工程",
  },
  {
    slug: "v0-0-release",
    title: "v0.0 工程基线",
    description: "阶段目标、实施结构、测试证据、问题复盘和已知限制。",
    repositoryPath: "docs/releases/v0.0.md",
    category: "记录",
  },
  {
    slug: "debugging",
    title: "调试记录",
    description: "工程基线与项目门户开发中出现过的故障、根因和预防措施。",
    repositoryPath: "docs/debugging.md",
    category: "记录",
  },
  {
    slug: "glossary",
    title: "项目词汇表",
    description: "统一 x86-64、构建、测试和架构相关术语。",
    repositoryPath: "docs/glossary.md",
    category: "入门",
  },
  {
    slug: "adr-own-boot-chain",
    title: "ADR 0001：自研完整启动链",
    description: "为什么 QEMU 只模拟硬件，固件和启动链必须由项目实现。",
    repositoryPath: "docs/adr/0001-own-the-entire-boot-chain.md",
    category: "决策",
  },
  {
    slug: "adr-layered-testing",
    title: "ADR 0002：分层与随机测试",
    description: "为什么所有纯逻辑模块都需要可复现的分层测试。",
    repositoryPath: "docs/adr/0002-layered-randomized-testing.md",
    category: "决策",
  },
] as const;

export function findProjectDocument(slug: string) {
  return projectDocuments.find((document) => document.slug === slug);
}
