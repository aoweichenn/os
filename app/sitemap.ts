import type { MetadataRoute } from "next";
import { projectDocuments } from "@/lib/document_catalog";

const SITE_BASE_URL = "https://x86-64-os-lab.aoweichenn.chatgpt.site";

export const dynamic = "force-static";

export default function sitemap(): MetadataRoute.Sitemap {
  const siteRoutes = [
    "/",
    "/architecture/",
    "/roadmap/",
    "/engineering/",
    "/docs/",
    ...projectDocuments.map((document) => `/docs/${document.slug}/`),
  ];

  return siteRoutes.map((path) => ({
    url: `${SITE_BASE_URL}${path}`,
    changeFrequency: "weekly",
    priority: path === "/" ? 1 : 0.8,
  }));
}
