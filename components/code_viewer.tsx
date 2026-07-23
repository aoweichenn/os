"use client";

import { useState } from "react";

interface CodeViewerProperties {
  filePath: string;
  highlightedCode: string;
  sourceCode: string;
}

export function CodeViewer({
  filePath,
  highlightedCode,
  sourceCode,
}: CodeViewerProperties) {
  const [copyState, setCopyState] = useState<"idle" | "path" | "source">(
    "idle",
  );

  const copyText = async (
    content: string,
    completedState: "path" | "source",
  ) => {
    await navigator.clipboard.writeText(content);
    setCopyState(completedState);
    window.setTimeout(() => setCopyState("idle"), 1600);
  };

  const handleCodeClick = (mouseEvent: React.MouseEvent<HTMLDivElement>) => {
    const eventTarget = mouseEvent.target;

    if (!(eventTarget instanceof Element)) {
      return;
    }

    const sourceLine = eventTarget.closest<HTMLElement>(".line");
    if (!sourceLine?.id) {
      return;
    }

    window.history.replaceState(undefined, "", `#${sourceLine.id}`);
  };

  return (
    <section className="codeViewer">
      <div className="codeViewerToolbar">
        <code>{filePath}</code>
        <div>
          <button onClick={() => copyText(filePath, "path")} type="button">
            {copyState === "path" ? "路径已复制" : "复制路径"}
          </button>
          <button onClick={() => copyText(sourceCode, "source")} type="button">
            {copyState === "source" ? "源码已复制" : "复制源码"}
          </button>
        </div>
      </div>
      <div
        className="codeHighlight"
        dangerouslySetInnerHTML={{ __html: highlightedCode }}
        onClick={handleCodeClick}
      />
    </section>
  );
}
