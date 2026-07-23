import type { MetadataRoute } from "next";

const SITE_BASE_URL = "https://x86-64-os-lab.aoweichenn.chatgpt.site";

export const dynamic = "force-static";

export default function sitemap(): MetadataRoute.Sitemap {
  return ["/", "/architecture/", "/roadmap/", "/engineering/", "/docs/"].map(
    (path) => ({
      url: `${SITE_BASE_URL}${path}`,
      changeFrequency: "weekly",
      priority: path === "/" ? 1 : 0.8,
    }),
  );
}
