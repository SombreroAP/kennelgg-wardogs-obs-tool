import { build } from "esbuild";
await build({
  entryPoints: ["src/plugin.js"],
  bundle: true,
  platform: "node",
  target: "node20",
  format: "esm",
  outfile: "com.kennelgg.wardogs.sdPlugin/bin/plugin.js",
  banner: { js: "import { createRequire as __cr } from 'module'; const require = __cr(import.meta.url);" },
});
console.log("bundled");
