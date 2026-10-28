// Hosted assets acceptance: no picker, on-demand reads, hash repair, cache reuse, live race.
const {chromium} = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const url = process.env.RR_WEB_URL || 'http://localhost:8780';
const evidence = process.env.RR_WEB_EVIDENCE || path.resolve(__dirname, '../analysis/dokku-web-20261025');
fs.mkdirSync(evidence, {recursive:true});
// Startup verifies 341 files (~467 MiB). A storage-blocked session must
// download them again; the former 90s limit expired during active verification.
// Keep a bounded five-minute cold-start budget and retain state if it is ever exceeded.
async function waitForStartup(page, predicate) {
    try {
        await page.waitForFunction(predicate, null, {timeout:300000});
    } catch (error) {
        const state = await page.evaluate(() => ({
            stage:document.querySelector('#loading-message').textContent,
            progress:document.querySelector('#loading-count').textContent,
            visible:!document.querySelector('#loading').hidden,
            assetError:engine.assetError, gameError:engine.gameError,
            visibility:document.visibilityState
        }));
        fs.writeFileSync(path.join(evidence,'startup-timeout.json'),JSON.stringify(state,null,2));
        await page.screenshot({path:path.join(evidence,'startup-timeout.png')});
        throw error;
    }
}
(async () => {
    const browser = await chromium.launch({headless:true});
    try {
        const context = await browser.newContext({viewport:{width:1440,height:900}});
        // Deterministic acceptance never sends synthetic failures to production telemetry.
        const tunnel = new URL('/t/e', url).href; // same-origin Sentry tunnel (deploy/nginx.conf)
        await context.route(tunnel, r => r.fulfill({json:{}}));
        await context.route('https://nazar.archit.xyz/**', r => r.fulfill({json:{status:1}}));
        const page = await context.newPage();
        const errors = [], requests = [];
        page.on('pageerror', e => errors.push(e.message));
        page.on('request', r => requests.push({url:r.url(),method:r.method()}));
        let wasmRequests=0;
        await page.route(/\/rr-game\.[a-f0-9]+\.wasm$/,route=>{
            wasmRequests++;
            return wasmRequests<=2 ? route.abort('failed') : route.continue();
        });
        await page.route('**/game-assets/manifest.json', async route => {
            const response=await route.fetch(), manifest=await response.json();
            manifest.files[0].hash='0'.repeat(64);
            await route.fulfill({response,json:manifest});
        });
        await page.goto(url);
        await page.locator('#retry').waitFor({state:'visible',timeout:90000});
        assert.match(await page.locator('#status').textContent(),/need updating/);
        assert.equal(await page.locator('#ready').isVisible(),false);
        assert.ok(wasmRequests>=3,'engine factory recovers after both initial Wasm fetch paths fail');
        await page.unroute(/\/rr-game\.[a-f0-9]+\.wasm$/);
        await page.unroute('**/game-assets/manifest.json');
        console.log('PASS mismatched server manifest rejected');
        await page.goto(url);
        await page.locator('#ready').waitFor({state:'visible',timeout:90000});
        assert.equal(await page.locator('.import-actions').isVisible(),false);
        assert.equal(await page.evaluate(()=>Array.from(document.fonts).filter(f=>f.status==='loaded').length),4);
        const initial = requests.filter(r=>/game-assets\/[a-f0-9]{64}$/.test(r.url));
        assert.equal(initial.length,4,'only fonts are downloaded before launch');
        await page.screenshot({path:path.join(evidence,'launcher.png')});
        const executable=await page.evaluate(async () => (await (await fetch('game-assets/manifest.json')).json()).files.find(f=>f.path==='ROADRASH.EXE'));
        const executableUrl='**/game-assets/'+executable.hash;
        await page.route(executableUrl,route=>route.fulfill({body:Buffer.alloc(executable.size)}));
        const failure=await page.evaluate(async () => {
            try {await engine.localAssets.read('ROADRASH.EXE');return '';}
            catch(error){return error.message;}
        });
        assert.match(failure,/failed verification/);
        await page.unroute(executableUrl);
        assert.equal(await page.evaluate(async()=> (await engine.localAssets.read('ROADRASH.EXE')).size),executable.size);
        console.log('PASS corrupt network file rejected, retry succeeds');
        // A same-size damaged cached font must be detected and fetched again.
        await page.evaluate(async () => {
            const manifest = await (await fetch('game-assets/manifest.json')).json();
            const record = manifest.files.find(r=>r.path==='TEXT/BADLOC.DLL');
            const cache = await (await (await navigator.storage.getDirectory()).getDirectoryHandle('road-rash')).getDirectoryHandle('hosted-v1');
            const stream = await (await cache.getFileHandle(record.hash)).createWritable();
            await stream.write(new Uint8Array(record.size)); await stream.close();
        });
        const beforeRepair = requests.length;
        await page.reload();
        await page.locator('#ready').waitFor({state:'visible',timeout:90000});
        assert.equal(requests.slice(beforeRepair).filter(r=>/game-assets\/[a-f0-9]{64}$/.test(r.url)).length,1);
        console.log('PASS automatic launcher, lazy fonts, corrupt cache repaired');
        const bike=await page.evaluate(async () => (await (await fetch('game-assets/manifest.json')).json()).files.find(f=>f.path==='DATA/BIKESPEC.RSC'));
        let releaseLoading;
        const loadingGate=new Promise(resolve=>{releaseLoading=resolve;});
        await page.route('**/game-assets/'+bike.hash,async route=>{await loadingGate;await route.continue();});
        await page.locator('#quick').click();
        try {
            await page.locator('#loading').waitFor({state:'visible'});
            assert.match(await page.locator('#loading-title').textContent(),/Loading Road Rash/);
            assert.match(await page.locator('#loading-title').evaluate(el=>getComputedStyle(el).fontFamily),/RoadRashBadloc/);
            assert.match(await page.locator('#loading-message').evaluate(el=>getComputedStyle(el).fontFamily),/RoadRashFutura/);
            await page.screenshot({path:path.join(evidence,'loading.png')});
        } finally {releaseLoading();}
        await page.unrouteAll({behavior:'wait'});
        await waitForStartup(page, () => engine.SDL3?.audioContext?.state==='running');
        await waitForStartup(page, () => {
            const canvas=document.querySelector('#canvas');
            const preview=document.createElement('canvas');preview.width=16;preview.height=12;
            const ctx=preview.getContext('2d');ctx.drawImage(canvas,0,0,16,12);
            const p=ctx.getImageData(0,0,16,12).data;
            // Loading screens have a dark gray background and yellow text.
            // A race has a blue sky; require it before sending driving input.
            let sky=0;for(let i=0;i<p.length;i+=4) if(p[i+2]>p[i]*1.3 && p[i+2]>100)sky++;
            return sky>15 && document.querySelector('#loading').hidden;
        });
        // SDL applies these inline CSS sizes when its native Window Size
        // setting changes. Neither may override the page layout: the canvas
        // fills the play area and SDL letterboxes 4:3/widescreen inside it.
        for (const [width,height] of [[1907,1280],[1280,960],[1440,900],[390,844],[844,390]]) {
            await page.setViewportSize({width,height});
            for (const scale of [1,2]) {
                await page.locator('#canvas').evaluate((canvas,scale)=>{
                    canvas.style.width=640*scale+'px';canvas.style.height=480*scale+'px';
                },scale);
                const frame=await page.locator('#canvas').boundingBox();
                const area=await page.locator('.viewport').boundingBox();
                assert.ok(Math.abs(frame.width-area.width)<2,'canvas fills viewport width');
                assert.ok(Math.abs(frame.height-area.height)<2,'canvas fills viewport height');
                // The render buffer follows the CSS box (window size tracks it),
                // so pixels are never stretched and mouse mapping holds.
                await page.waitForFunction(()=>{
                    const c=document.querySelector('#canvas'),r=c.getBoundingClientRect();
                    return Math.abs(c.width/c.height-r.width/r.height)<0.02;
                },null,{timeout:5000});
                assert.ok(frame.y+frame.height<=area.y+area.height+1,'bottom of game fits');
                assert.equal(await page.locator('footer').isVisible(),false);
                if (scale===2 && width>=1280)
                    await page.screenshot({path:path.join(evidence,`fit-${width}x${height}.png`)});
            }
        }
        await page.setViewportSize({width:1440,height:900});
        await page.locator('#fullscreen').click();
        await page.waitForFunction(()=>document.fullscreenElement?.classList.contains('viewport'));
        // Synthetic Escape is not a reliable browser-chrome gesture in
        // headless Chromium. Exercise the actual fullscreen exit API.
        await page.evaluate(()=>document.exitFullscreen());
        await page.waitForFunction(()=>!document.fullscreenElement);
        console.log('PASS fitted canvas at both window sizes, responsive viewports, fullscreen and original-font loading');
        await page.locator('#canvas').focus();
        await page.keyboard.down('ArrowUp');await page.waitForTimeout(3500);await page.keyboard.up('ArrowUp');
        await page.screenshot({path:path.join(evidence,'race.png')});
        assert.equal(await page.evaluate(()=>engine.assetError || null),null);
        const audio = await page.evaluate(async () => {
            const context=engine.SDL3.audioContext, analyser=context.createAnalyser();
            engine.SDL3.audio_playback.scriptProcessorNode.connect(analyser);
            await new Promise(resolve=>setTimeout(resolve,200));
            const samples=new Float32Array(analyser.fftSize);analyser.getFloatTimeDomainData(samples);
            analyser.disconnect();
            return samples.reduce((sum,x)=>sum+x*x,0);
        });
        assert.ok(audio>0,'game produces audio samples');
        console.log('PASS live quick race, keyboard input, nonzero audio');
        const downloaded = requests.filter(r=>/game-assets\/[a-f0-9]{64}$/.test(r.url));
        assert.ok(new Set(downloaded.map(r=>r.url)).size < 341,'does not fetch the entire disc');
        const beforeReload=requests.length;
        await page.reload();await page.locator('#ready').waitFor({state:'visible',timeout:90000});
        assert.equal(requests.slice(beforeReload).filter(r=>/game-assets\/[a-f0-9]{64}$/.test(r.url)).length,0);
        assert.ok(requests.every(r=>r.method==='GET' || (r.method==='POST' && (r.url===tunnel || new URL(r.url).origin==='https://nazar.archit.xyz'))),'no file uploads');
        assert.deepEqual(errors,[]);
        console.log('PASS cache reused after reload, no uploads or page errors; unique assets:',new Set(downloaded.map(r=>r.url)).size);
        fs.writeFileSync(path.join(evidence,'requests.json'),JSON.stringify(requests,null,2));
        // Storage restrictions must not prevent hosted play. This exercises
        // the real IDBFS fallback and real engine, rather than a mocked mount.
        await context.addInitScript(() => {
            navigator.storage.getDirectory = async () => { throw new DOMException('Blocked', 'SecurityError'); };
            indexedDB.open = () => { throw new DOMException('Blocked', 'SecurityError'); };
        });
        const restricted = await context.newPage();
        restricted.on('pageerror', e => errors.push(e.message));
        await restricted.goto(url);
        await restricted.locator('#ready').waitFor({state:'visible',timeout:90000});
        assert.match(await restricted.locator('#cache-note').textContent(),/session/);
        await restricted.locator('#quick').click();
        await waitForStartup(restricted,()=>document.querySelector('#loading').hidden);
        assert.equal(await restricted.evaluate(()=>running),true);
        // Include SDL's registered callback and the launcher's lifecycle bridge.
        for (let i=0;i<3;i++) await restricted.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
        assert.equal(await restricted.evaluate(()=>engine.assetError || null),null);
        assert.deepEqual(errors,[]);
        console.log('PASS hosted race with blocked OPFS and IndexedDB, live visibility callbacks');
        // A caught C++ failure must preserve its throw-site stack, and no
        // lifecycle bridge may call back into SDL after main has returned.
        const failing = await context.newPage();
        failing.on('pageerror', e => errors.push(e.message));
        await failing.route(/\/app\.[a-f0-9]+\.js$/,async route=>{
            const response=await route.fetch();
            const body=(await response.text()).replace('const argv = engine._malloc', "args.push('--bike', '999'); const argv = engine._malloc");
            await route.fulfill({response,body});
        });
        await failing.goto(url);
        await failing.locator('#ready').waitFor({state:'visible',timeout:90000});
        await failing.locator('#quick').click();
        await failing.waitForFunction(()=>!running,null,{timeout:90000});
        const failureDetails=await failing.evaluate(()=>({message:engine.gameException?.message,stack:engine.gameException?.stack}));
        assert.match(failureDetails.message,/Bike index out of range/);
        assert.match(failureDetails.stack,/wasm/);
        assert.match(failureDetails.stack,/main/,'Wasm function names survive release linking');
        for(let i=0;i<3;i++) await failing.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
        assert.deepEqual(errors,[]);
        console.log('PASS caught C++ throw-site diagnostics, visibility events after main exits');
        await failing.reload();
        await failing.locator('#ready').waitFor({state:'visible',timeout:90000});
        await failing.evaluate(()=>{engine.localAssets.read=async()=>{throw new Error('Fixture network unavailable');};});
        await failing.locator('#quick').click();
        await failing.waitForFunction(()=>!running,null,{timeout:90000});
        assert.match(await failing.locator('#loading-message').textContent(),/Fixture network unavailable/);
        assert.doesNotMatch(await failing.locator('#loading-message').textContent(),/Re-import/);
        assert.deepEqual(errors,[]);
        console.log('PASS actual asset failure survives the C++ boundary without a misleading disc-import hint');
        await context.close();
    } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
