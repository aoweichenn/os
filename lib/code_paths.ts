export function codeFileHref(filePath: string) {
  const encodedPath = filePath
    .split("/")
    .map((pathSegment) => encodeURIComponent(pathSegment))
    .join("/");

  return `/code/${encodedPath}/`;
}
