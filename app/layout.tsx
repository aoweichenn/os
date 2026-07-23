import type { Metadata } from "next";
import { SiteFooter } from "@/components/site_footer";
import { SiteHeader } from "@/components/site_header";
import "./globals.css";

export const metadata: Metadata = {
  title: {
    default: "x86-64 OS Lab",
    template: "%s · x86-64 OS Lab",
  },
  description: "从 CPU 复位向量开始，自研完整的 x86-64 教学操作系统。",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="zh-CN">
      <body>
        <SiteHeader />
        {children}
        <SiteFooter />
      </body>
    </html>
  );
}
