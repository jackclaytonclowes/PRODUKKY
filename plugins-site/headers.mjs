// headers.mjs — the response headers for the download site, in Vercel's format.
// build.mjs writes them to public/vercel.json, and plugins-site/vercel.json
// repeats them for a Vercel project built from git (tests/plugins-site.mjs checks
// the two match). On Render, add the same list under the static site's Headers
// (README.md lists them). tests/plugins-site.mjs serves the built folder with
// exactly these, so a page they would break fails there first.
//
// The one page that runs code is play/fracture.html, the browser version: it
// needs inline script, a blob: URL for its AudioWorklet, and the microphone if
// someone chooses to play through it. Nothing on the site may load from, or
// send to, anywhere else.
export const csp = [
  "default-src 'self'",
  "script-src 'self' 'unsafe-inline' blob:",
  "style-src 'self' 'unsafe-inline'",
  "img-src 'self' data: blob:",
  "media-src 'self' blob:",
  "worker-src 'self' blob:",
  "connect-src 'self' blob:",
  "object-src 'none'",
  "base-uri 'none'",
  "form-action 'none'",
  "frame-ancestors 'none'",
].join('; ');

export const headers = [
  {
    source: '/(.*)',
    headers: [
      { key: 'Content-Security-Policy', value: csp },
      { key: 'X-Content-Type-Options', value: 'nosniff' },
      { key: 'Referrer-Policy', value: 'no-referrer' },
      { key: 'Permissions-Policy', value: 'microphone=(self), camera=(), geolocation=(), usb=()' },
    ],
  },
];
