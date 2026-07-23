"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { useEffect, useMemo, useRef, useState } from "react";
import { codeFileHref } from "@/lib/code_paths";

interface CodeDirectoryTreeProperties {
  filePaths: readonly string[];
}

interface CodeTreeNode {
  name: string;
  path: string;
  type: "directory" | "file";
  children: CodeTreeNode[];
}

function buildCodeTree(filePaths: readonly string[]) {
  const rootNode: CodeTreeNode = {
    name: "",
    path: "",
    type: "directory",
    children: [],
  };

  for (const filePath of filePaths) {
    const pathSegments = filePath.split("/");
    let currentNode = rootNode;

    pathSegments.forEach((pathSegment, segmentIndex) => {
      const childPath = pathSegments.slice(0, segmentIndex + 1).join("/");
      const isFile = segmentIndex === pathSegments.length - 1;
      let childNode = currentNode.children.find(
        (candidateNode) => candidateNode.name === pathSegment,
      );

      if (!childNode) {
        childNode = {
          name: pathSegment,
          path: childPath,
          type: isFile ? "file" : "directory",
          children: [],
        };
        currentNode.children.push(childNode);
      }

      currentNode = childNode;
    });
  }

  const sortTreeNodes = (treeNode: CodeTreeNode) => {
    treeNode.children.sort((leftNode, rightNode) => {
      if (leftNode.type !== rightNode.type) {
        return leftNode.type === "directory" ? -1 : 1;
      }

      return leftNode.name.localeCompare(rightNode.name);
    });
    treeNode.children.forEach(sortTreeNodes);
  };

  sortTreeNodes(rootNode);
  return rootNode;
}

function resolveCurrentFilePath(pathname: string) {
  const encodedFilePath = pathname
    .replace(/^\/code\/?/, "")
    .replace(/\/$/, "");

  if (!encodedFilePath) {
    return "";
  }

  return encodedFilePath
    .split("/")
    .map((pathSegment) => decodeURIComponent(pathSegment))
    .join("/");
}

function resolveAncestorDirectories(filePath: string) {
  const pathSegments = filePath.split("/");
  const ancestorDirectories = [];

  for (let segmentIndex = 1; segmentIndex < pathSegments.length; segmentIndex += 1) {
    ancestorDirectories.push(pathSegments.slice(0, segmentIndex).join("/"));
  }

  return ancestorDirectories;
}

interface DirectoryNodeProperties {
  node: CodeTreeNode;
  activeFilePath: string;
  openDirectoryPaths: ReadonlySet<string>;
  onToggleDirectory: (directoryPath: string) => void;
}

function DirectoryNode({
  node,
  activeFilePath,
  openDirectoryPaths,
  onToggleDirectory,
}: DirectoryNodeProperties) {
  if (node.type === "file") {
    return (
      <li>
        <Link
          className={node.path === activeFilePath ? "active" : undefined}
          href={codeFileHref(node.path)}
          title={node.path}
        >
          <span className="codeTreeFileMark">F</span>
          <span>{node.name}</span>
        </Link>
      </li>
    );
  }

  const isOpen = openDirectoryPaths.has(node.path);

  return (
    <li>
      <button
        aria-expanded={isOpen}
        onClick={() => onToggleDirectory(node.path)}
        type="button"
      >
        <span className="codeTreeChevron">{isOpen ? "⌄" : "›"}</span>
        <span>{node.name}</span>
      </button>
      {isOpen && (
        <ul>
          {node.children.map((childNode) => (
            <DirectoryNode
              activeFilePath={activeFilePath}
              key={childNode.path}
              node={childNode}
              onToggleDirectory={onToggleDirectory}
              openDirectoryPaths={openDirectoryPaths}
            />
          ))}
        </ul>
      )}
    </li>
  );
}

export function CodeDirectoryTree({
  filePaths,
}: CodeDirectoryTreeProperties) {
  const pathname = usePathname();
  const searchInputReference = useRef<HTMLInputElement>(null);
  const [searchQuery, setSearchQuery] = useState("");
  const [openDirectoryPaths, setOpenDirectoryPaths] = useState<Set<string>>(
    new Set(),
  );
  const codeTree = useMemo(() => buildCodeTree(filePaths), [filePaths]);
  const activeFilePath = resolveCurrentFilePath(pathname);
  const normalizedSearchQuery = searchQuery.trim().toLocaleLowerCase();
  const matchingFilePaths = normalizedSearchQuery
    ? filePaths.filter((filePath) =>
        filePath.toLocaleLowerCase().includes(normalizedSearchQuery),
      )
    : [];

  useEffect(() => {
    if (!activeFilePath) {
      return;
    }

    setOpenDirectoryPaths((currentDirectoryPaths) => {
      const nextDirectoryPaths = new Set(currentDirectoryPaths);
      resolveAncestorDirectories(activeFilePath).forEach((directoryPath) => {
        nextDirectoryPaths.add(directoryPath);
      });
      return nextDirectoryPaths;
    });
  }, [activeFilePath]);

  useEffect(() => {
    const handleKeyboardShortcut = (keyboardEvent: KeyboardEvent) => {
      const eventTarget = keyboardEvent.target;
      const isTyping =
        eventTarget instanceof HTMLInputElement ||
        eventTarget instanceof HTMLTextAreaElement;

      if (keyboardEvent.key === "/" && !isTyping) {
        keyboardEvent.preventDefault();
        searchInputReference.current?.focus();
      }

      if (keyboardEvent.key === "Escape") {
        setSearchQuery("");
        searchInputReference.current?.blur();
      }
    };

    window.addEventListener("keydown", handleKeyboardShortcut);
    return () => window.removeEventListener("keydown", handleKeyboardShortcut);
  }, []);

  const toggleDirectory = (directoryPath: string) => {
    setOpenDirectoryPaths((currentDirectoryPaths) => {
      const nextDirectoryPaths = new Set(currentDirectoryPaths);

      if (nextDirectoryPaths.has(directoryPath)) {
        nextDirectoryPaths.delete(directoryPath);
      } else {
        nextDirectoryPaths.add(directoryPath);
      }

      return nextDirectoryPaths;
    });
  };

  return (
    <aside className="codeTreePanel">
      <div className="codeTreeHeader">
        <Link href="/code/">
          <span>CODE INDEX</span>
          <strong>x86-64 OS Lab</strong>
        </Link>
        <label>
          <span className="srOnly">搜索项目文件</span>
          <input
            onChange={(event) => setSearchQuery(event.target.value)}
            placeholder="搜索文件，快捷键 /"
            ref={searchInputReference}
            type="search"
            value={searchQuery}
          />
        </label>
      </div>

      <div className="codeTreeBody">
        {normalizedSearchQuery ? (
          <div className="codeSearchResults">
            <span>{matchingFilePaths.length} 个匹配文件</span>
            {matchingFilePaths.map((filePath) => (
              <Link href={codeFileHref(filePath)} key={filePath}>
                {filePath}
              </Link>
            ))}
          </div>
        ) : (
          <ul className="codeTreeRoot">
            {codeTree.children.map((treeNode) => (
              <DirectoryNode
                activeFilePath={activeFilePath}
                key={treeNode.path}
                node={treeNode}
                onToggleDirectory={toggleDirectory}
                openDirectoryPaths={openDirectoryPaths}
              />
            ))}
          </ul>
        )}
      </div>
    </aside>
  );
}
