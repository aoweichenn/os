import type { Components } from "react-markdown";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";

const markdownComponents: Components = {
  a: ({ children, href }) => (
    <a href={href} rel="noreferrer">
      {children}
    </a>
  ),
};

interface MarkdownArticleProperties {
  source: string;
}

export function MarkdownArticle({ source }: MarkdownArticleProperties) {
  return (
    <div className="documentArticle">
      <ReactMarkdown
        components={markdownComponents}
        remarkPlugins={[remarkGfm]}
      >
        {source}
      </ReactMarkdown>
    </div>
  );
}
