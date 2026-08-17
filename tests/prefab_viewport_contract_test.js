/* Prefab Details layout, resize, and zero-copy geometry transport contract. */
'use strict';
const fs=require('fs'), path=require('path');
const root=path.join(__dirname,'..');
const html=fs.readFileSync(path.join(root,'src','ui','webview','mockup.html'),'utf8');
const js=fs.readFileSync(path.join(root,'src','ui','webview','prefab_viewport.js'),'utf8');
const host=fs.readFileSync(path.join(root,'src','ui','webview','snapmap_plus_ui_webview.cpp'),'utf8');
const backend=fs.readFileSync(path.join(root,'src','backend','prefabpreview.c'),'utf8');
const iface=fs.readFileSync(path.join(root,'src','backend','iface_engine.c'),'utf8');
let failures=0;
function check(value,message){if(!value){console.error('[FAIL] '+message);failures++;}}

new Function(js); /* syntax check without constructing the DOM-backed viewport */
check(html.includes('id="pcPreview"'),'Prefab Details has the 3D viewport');
check(html.indexOf('id="pcPreview"') < html.indexOf('for="pcName"'),'viewport appears above Name');
check(!html.includes('id="pcChips"') && !html.includes('class="pc-chip"'),'per-type metadata is absent');
check(html.indexOf('id="pcEntityCount"') > html.indexOf('id="pcCanvas"') &&
      html.indexOf('id="pcEntityCount"') < html.indexOf('for="pcName"'),
      'aggregate entity count appears inside the preview above Name');
check(html.includes("d.count + (d.count === 1 ? ' entity' : ' entities')"),'aggregate count uses singular/plural text');
check(/\.pc-preview-count\s*\{[^}]*right:\s*8px;[^}]*bottom:\s*7px;/s.test(html),
      'aggregate count is bottom-right preview metadata');
check(/\.pc-preview-help\s*\{[^}]*top:\s*7px;[^}]*right:\s*8px;/s.test(html),
      'orbit controls replace the count at top-right');
check(/\.pc-preview[^}]*background:\s*var\(--field\)/s.test(html),'viewport uses the Decl Text field background');
check(html.includes('class="pc-preview-legend"') && html.includes('Logic / I/O') && html.includes('Triggers'),
      'viewport explains semantic scene roles');
check(/\.pc-name\s*\{\s*width:\s*100%;\s*\}/.test(html),'Name has no typography override over shared field rules');
check(js.includes('new ResizeObserver('),'element resize is observed directly');
check(js.includes('requestAnimationFrame('),'resize/render work is coalesced to animation frames');
check(js.includes('canvas.width !== w || canvas.height !== h'),'drawing buffer changes only when dimensions change');
check(js.includes('reach = 14') && js.includes('function fadeAt(x,y)') && js.includes('uAlpha*vFade'),
      'extended Cartesian floor grid uses a circular outward alpha mask');
check(js.includes('gl.vertexAttrib4f(1,.5,.5,1,1)'),'grid constant attribute supplies XYZW before mesh drawing');
check(js.includes("setStatus('3D preview render failed')"),'draw failures become visible instead of leaving a silent blank canvas');
check(js.includes('snapedit_logic_hexagon.lwo') && js.includes('snapedit_logic_circle.lwo') &&
      js.includes('snapedit_logic_diamond.lwo'),'logic, input/output, and filter nodes use installed editor glyphs');
check(js.includes("return 'decal'") && js.includes("role!=='decal'&&!MARKER_MODELS[role]"),
      'texture-only decals use flat helper geometry instead of model-resolution cubes');
check(!js.includes("edit.isVisible === false") &&
      js.indexOf("h.indexOf('snapmaps/interactibles/')") < js.indexOf("c.indexOf('volume_trigger')") &&
      js.includes("e.role !== 'trigger'"),
      'semantic triggers are excluded from framing without misclassifying normal isVisible=false entities');
check(js.includes('drawHelperOutline(e') && js.includes("e.role==='trigger'?.10:.16"),
      'trigger geometry is distinguished with translucent fill and an outline');
check(js.includes("rmi.customMaterial && listed[2]") && js.includes("return listed[1] || listed[2] || listed[0]"),
      'blocking volumes prefer their visible shell over item[0] editor trigger geometry');
check(!js.includes('setInterval('),'viewport does not run a continuous polling/render loop');
check(html.includes("addEventListener('sharedbufferreceived'"),'page receives binary geometry by shared buffer');
check(html.includes('releaseBuffer(buffer)'),'page releases WebView shared buffers after GPU upload');
check(host.includes('PostSharedBufferToScript'),'native host posts geometry without base64 encoding');
check(backend.includes('PP_MAX_VERTICES') && backend.includes('PP_MAX_INDICES'),'native decode has hard geometry budgets');
check(backend.includes('fixed 32-byte cooked metadata block') && backend.includes('len - r.pos >= 24u'),
      'multi-surface installed BMODEL geometry consumes per-surface metadata exactly');
check(backend.includes('sh_prefabpreview_resolve_model') && backend.includes('spawnerEntityPair') &&
      backend.includes('entityStatic'),'pickup spawners resolve through installed entityStatic inheritance');
check(iface.indexOf('strstr(inherit_name, "spawner")') < iface.indexOf('sh_typeinfo_inherit_model(inherit_name'),
      'pickup spawners prefer their semantic entityStatic model over a generic resolved editor model');

if(failures){console.error('prefab_viewport_contract_test: '+failures+' failure(s)');process.exit(1);}
console.log('prefab_viewport_contract_test: OK');
