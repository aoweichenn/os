"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";

const navigationItems = [
  { href: "/", label: "首页" },
  { href: "/architecture/", label: "启动架构" },
  { href: "/roadmap/", label: "开发路线" },
  { href: "/engineering/", label: "工程规范" },
  { href: "/docs/", label: "文档中心" },
  { href: "/code/", label: "代码走读" },
] as const;

function normalizePathname(pathname: string) {
  if (pathname === "/") {
    return pathname;
  }

  return pathname.endsWith("/") ? pathname : `${pathname}/`;
}

function isNavigationItemActive(pathname: string, itemHref: string) {
  if (itemHref === "/") {
    return pathname === itemHref;
  }

  return pathname.startsWith(itemHref);
}

export function SiteHeader() {
  const pathname = normalizePathname(usePathname());

  return (
    <header className="topbar">
      <Link className="brand" href="/" aria-label="返回项目首页">
        <span className="brandMark">OS</span>
        <span>x86-64 LAB</span>
      </Link>
      <nav className="navLinks" aria-label="主导航">
        {navigationItems.map((item) => (
          <Link
            className={
              isNavigationItemActive(pathname, item.href) ? "active" : undefined
            }
            href={item.href}
            key={item.href}
          >
            {item.label}
          </Link>
        ))}
      </nav>
      <span className="statusPill">
        <i />
        PLANNING
      </span>
    </header>
  );
}
