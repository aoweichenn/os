import type { Metadata } from "next";
import { CodeDirectoryTree } from "@/components/code_directory_tree";
import { codeFiles } from "@/lib/code_catalog";

export const metadata: Metadata = {
  title: {
    default: "代码走读",
    template: "%s · 代码走读 · x86-64 OS Lab",
  },
  description: "按目录浏览 x86-64 OS Lab 全部项目源码，并结合中文走读理解设计。",
};

export default function CodeLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  const filePaths = codeFiles.map((codeFile) => codeFile.path);

  return (
    <main className="codeBrowserMain">
      <div className="codeBrowserShell">
        <CodeDirectoryTree filePaths={filePaths} />
        <div className="codeBrowserContent">{children}</div>
      </div>
    </main>
  );
}
