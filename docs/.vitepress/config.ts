import { readdirSync } from "node:fs";
import { execSync } from "node:child_process";
import path from "node:path";
import { DefaultTheme, defineConfig } from "vitepress";

function listItems(root: string, folder: string): DefaultTheme.SidebarItem[] {
  return readdirSync(path.join(root, folder))
    .filter((file) => file.endsWith(".md") && file !== "index.md")
    .map((file) => file.replace(/\.md$/, ""))
    .map((name) => ({
      text: name.replace(/_/g, " "),
      link: `/${folder}/${name}`,
    }));
}

const repoUrl = execSync(`git remote get-url origin`, { encoding: "utf-8" }).trim();

// https://vitepress.dev/reference/site-config
export default defineConfig({
  title: "warpo document",
  description: "wasm optimizer designed for AssemblyScript and wasm-compiler",
  base: "/warpo/",
  head: [["meta", { name: "google-site-verification", content: "762vxla4bLoGKFlH_iYkk7TVUhrwwpMFS2r7idty0_Y" }]],
  sitemap: {
    hostname: "https://wasm-ecosystem.github.io/warpo/",
  },
  themeConfig: {
    // https://vitepress.dev/reference/default-theme-config
    search: {
      provider: "local",
    },
    nav: [
      { text: "Home", link: "/" },
      { text: "GitHub", link: "https://github.com/wasm-ecosystem/warpo" },
    ],
    sidebar: [
      {
        text: "Document",
        items: [
          {
            text: "Lowering Passes",
            link: "/lower/index",
            items: listItems("docs", "lower"),
          },
          {
            text: "Builtin Transform",
            link: "/transform/index",
            items: listItems("docs", "transform"),
          },
          {
            text: "Optimization Passes",
            link: "/opt/index",
            items: listItems("docs", "opt"),
          },
          {
            text: "Infrastructure",
            link: "/infra/index",
            items: listItems("docs", "infra"),
          },
        ],
      },
    ],
    socialLinks: [{ icon: "github", link: "https://github.com/wasm-ecosystem/warpo" }],
  },
});
