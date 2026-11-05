const PREFIX = "devilutionx-ps3-fmv-";
// Bump this whenever a published runtime asset changes, especially FFmpeg core.
const CACHE = PREFIX + "v3";
self.addEventListener("install", (event) => event.waitUntil((async () => {
  const cache = await caches.open(CACHE);
  await cache.add(self.registration.scope);
  await self.skipWaiting();
})()));
self.addEventListener("activate", (event) => event.waitUntil((async () => {
  const keys = await caches.keys();
  await Promise.all(keys.filter((key) => key.startsWith(PREFIX) && key !== CACHE).map((key) => caches.delete(key)));
  await self.clients.claim();
})()));
self.addEventListener("fetch", (event) => {
  const request = event.request;
  if (request.method !== "GET" || new URL(request.url).origin !== self.location.origin) return;
  event.respondWith((async () => {
    const cache = await caches.open(CACHE);
    // A cache read/write failure must never turn a valid network response into
    // "TypeError: Failed to fetch" (the FFmpeg WASM core is over 30 MB).
    try {
      const hit = await cache.match(request);
      if (hit) return hit;
    } catch (error) {
      console.warn("Local asset cache read failed; using network:", error);
    }
    const response = await fetch(request);
    if (response.ok && response.type === "basic") {
      try {
        await cache.put(request, response.clone());
      } catch (error) {
        console.warn("Local asset cache write failed; continuing without caching:", error);
      }
    }
    return response;
  })());
});
