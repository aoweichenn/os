const OS_SITE_FAVICON_REQUEST_PATH = "/favicon.ico";
const OS_SITE_ICON_ASSET_PATH = "/icon.svg";

export default {
  async fetch(request, environment) {
    const requestUrl = new URL(request.url);

    if (requestUrl.pathname === OS_SITE_FAVICON_REQUEST_PATH) {
      requestUrl.pathname = OS_SITE_ICON_ASSET_PATH;
      return environment.ASSETS.fetch(new Request(requestUrl, request));
    }

    return environment.ASSETS.fetch(request);
  },
};
