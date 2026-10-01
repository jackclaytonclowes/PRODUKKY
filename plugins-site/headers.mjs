// headers.mjs — the response headers for the download site, in Vercel's format.
// build.mjs writes them to public/vercel.json; plugins-site/vercel.json (Vercel
// from git) and the render.yaml at the top of the repository (Render) repeat
// them, and tests/plugins-site.mjs checks all three match. tests/plugins-site.mjs serves the built folder with
// exactly these, so a page they would break fails there first.
//
// The one page that runs code is play/fracture.html, the browser version: it
// needs inline script, a blob: URL for its AudioWorklet, the microphone if
// someone chooses to play through it, and 'wasm-unsafe-eval' to compile the
// plugin's engine, which it carries as WebAssembly. That source allows
// WebAssembly and nothing else: eval() and new Function() stay refused.
// Nothing on the site may load from, or send to, anywhere else.
export const csp = [
  "default-src 'self'",
  "script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval' blob:",
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

// render.yaml, the Render Blueprint, as it should read: the test compares the
// committed file with this, so the two cannot drift
export function renderYaml() {
  const q = v => JSON.stringify(v);
  return [
    '# Render Blueprint for the download site (plugins-site/). Generated from',
    '# plugins-site/headers.mjs; tests/plugins-site.mjs checks it still matches.',
    '#',
    '# In Render: New, then Blueprint, then pick this repository.',
    'services:',
    '  - type: web',
    '    name: fracture-crate',
    '    runtime: static',
    '    buildCommand: node plugins-site/build.mjs',
    '    staticPublishPath: ./plugins-site/public',
    '    pullRequestPreviewsEnabled: false',
    '    headers:',
    ...headers[0].headers.flatMap(h => [
      '      - path: /*',
      `        name: ${h.key}`,
      `        value: ${q(h.value)}`,
    ]),
    '',
  ].join('\n');
}
