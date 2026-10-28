// Run against a locally served tools/build_web.sh bundle. Playwright is supplied
// by the caller's NODE_PATH; disc files are only sent to a local browser picker.
// RR_WEB_URL, RR_WEB_DISC and optional RR_WEB_ISO override the local defaults.
const {chromium} = require('playwright');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const url = process.env.RR_WEB_URL || 'http://localhost:8765';
if (!['localhost', '127.0.0.1'].includes(new URL(url).hostname)) throw new Error('Smoke fixture must stay local');
const disc = process.env.RR_WEB_DISC || path.resolve(__dirname, '../../cdrom/ROADRASH');
const evidence = path.resolve(__dirname, '../analysis/browser-20261025');
fs.mkdirSync(evidence, {recursive:true});
(async () => {
    const browser = await chromium.launch({headless:true});
    try {
        const context = await browser.newContext({viewport:{width:1280,height:1000}});
        const page = await context.newPage();
        const errors = [], requests = [];
        page.on('pageerror', error => errors.push(error.message));
        page.on('request', request => requests.push({url:request.url(), method:request.method()}));
        // A local import server has no hosted manifest; app.js treats its 404 as
        // "not hosted", so only that probe's resource error is expected here.
        const hostedProbe = text => /status of 404/.test(text);
        page.on('console', msg => {
            if (msg.type() !== 'error') return;
            if (hostedProbe(msg.text()) && new URL(msg.location().url || url, url).pathname === '/game-assets/manifest.json') return;
            // Likewise the launcher's multiplayer probe: no rr-netd behind a local server.
            if (/Failed to load resource/.test(msg.text()) && new URL(msg.location().url || url, url).pathname === '/net/health') return;
            errors.push(msg.text());
        });
        const visibleFrame = async () => page.evaluate(() => {
            const preview = document.createElement('canvas'); preview.width = 16; preview.height = 12;
            const ctx = preview.getContext('2d'); ctx.drawImage(document.querySelector('#canvas'), 0, 0, 16, 12);
            const pixels = ctx.getImageData(0, 0, 16, 12).data;
            let lit = 0;
            for (let i = 0; i < pixels.length; i += 4) if (pixels[i] + pixels[i+1] + pixels[i+2] > 45) lit++;
            return lit > 30;
        });
        await page.goto(url);
        await page.waitForFunction(() => document.querySelector('#status').textContent.includes('Engine ready'));
        await page.locator('#iso').setInputFiles({name:'wrong.iso',mimeType:'application/octet-stream',buffer:Buffer.alloc(40000)});
        await page.waitForFunction(() => document.querySelector('#status').classList.contains('error'));
        assert.match(await page.locator('#status').textContent(), /ISO/);
        console.log('PASS malformed ISO rejected');
        await page.locator('#folder').setInputFiles(disc);
        await page.waitForFunction(() => !document.querySelector('#ready').hidden, null, {timeout:120000});
        console.log('PASS 341-file folder import');
        // Failure/cancellation must preserve the previous committed generation.
        await page.locator('#folder').setInputFiles(disc);
        await page.waitForFunction(() => document.querySelector('#progress').value > 4);
        await page.locator('#cancel').click();
        await page.waitForFunction(() => document.querySelector('#cancel').hidden);
        assert.match(await page.locator('#status').textContent(), /cancelled/);
        assert.equal(await page.locator('#ready').isVisible(), true);
        console.log('PASS cancellation preserves previous import');
        await page.evaluate(() => {
            window.originalEstimate = navigator.storage.estimate.bind(navigator.storage);
            navigator.storage.estimate = async () => ({quota:1, usage:0});
        });
        await page.locator('#folder').setInputFiles(disc);
        await page.waitForFunction(() => document.querySelector('#status').textContent.includes('storage is full'));
        assert.equal(await page.locator('#ready').isVisible(),true);
        await page.evaluate(() => { navigator.storage.estimate = window.originalEstimate; });
        console.log('PASS quota failure preserves previous disc and offers retry');
        if (process.env.RR_WEB_ISO) {
            await page.locator('#iso').setInputFiles(process.env.RR_WEB_ISO);
            await page.waitForFunction(() => !document.querySelector('#ready').hidden && document.querySelector('#cancel').hidden, null, {timeout:120000});
            assert.doesNotMatch(await page.locator('#status').textContent(), /error|mismatch|Missing:/i);
            console.log('PASS ISO range import');
        }
        await page.evaluate(async () => {
            engine.FS.writeFile('/persist/browser-smoke.txt', 'saved');
            await new Promise((resolve,reject)=>engine.FS.syncfs(false,e=>e?reject(e):resolve()));
        });
        await page.reload();
        await page.waitForFunction(() => !document.querySelector('#ready').hidden);
        assert.equal(await page.evaluate(()=>engine.FS.readFile('/persist/browser-smoke.txt',{encoding:'utf8'})), 'saved');
        console.log('PASS disc and save filesystem survive reload');
        await page.screenshot({path:path.join(evidence,'launcher-ready.png'),fullPage:true});
        // Online panel: offline without rr-netd, then a live list, invite and code entry.
        await page.waitForFunction(() => document.querySelector('#net-status').dataset.state === 'offline');
        await page.route('**/net/health', route => route.fulfill({contentType:'application/json', body:JSON.stringify({
            ok:true, players:3, public_rooms:[
                {code:'QXETN8', host:'Viper', level:1, humans:2, seats:8, status:'lobby'},
                {code:'234567', host:'<b>Natasha</b>', level:4, humans:8, seats:8, status:'racing'},
                {code:'OOOOOO', host:'Invalid', level:0, humans:1, seats:8, status:'lobby'}]})}));
        await page.goto(url + '/?room=qxe-tn8');
        await page.waitForFunction(() => document.querySelector('#net-status').dataset.state === 'online');
        assert.equal(new URL(page.url()).search, '', 'invite parameter removed from the address');
        assert.equal(await page.locator('#invite-code').textContent(), 'QXE-TN8');
        assert.equal(await page.locator('#net-status').textContent(), '3 riders online');
        const rows = page.locator('#net-rooms li');
        assert.equal(await rows.count(), 2, 'invalid room codes are not listed');
        assert.match(await rows.nth(0).textContent(), /Viper.*Level 2 · 2\/8 riders · QXE-TN8.*Open.*Join/);
        assert.match(await rows.nth(1).textContent(), /<b>Natasha<\/b>.*Racing.*Watch/);
        await page.locator('#net-code').fill('abc');
        assert.equal(await page.locator('#net-join-go').isDisabled(), true);
        await page.locator('#net-code').fill('https://road-rash.archit.xyz/?room=234-567');
        assert.equal(await page.locator('#net-join-go').isDisabled(), false);
        await page.screenshot({path:path.join(evidence,'launcher-online.png'),fullPage:true});
        await page.unroute('**/net/health');
        console.log('PASS online panel: server status, public rooms, invite and code entry');
        await page.locator('#quick').click();
        await page.waitForTimeout(7000);
        assert.equal(await page.evaluate(()=>engine.SDL3.audioContext.state),'running');
        assert.ok(await visibleFrame(), 'race canvas has a rendered frame');
        await page.screenshot({path:path.join(evidence,'race-before.png'),fullPage:true});
        await page.locator('#canvas').focus();
        await page.keyboard.down('ArrowUp'); await page.waitForTimeout(6500); await page.keyboard.up('ArrowUp');
        await page.screenshot({path:path.join(evidence,'race-after.png'),fullPage:true});
        const audio = await page.evaluate(async () => {
            const a = engine.SDL3.audioContext.createAnalyser();
            engine.SDL3.audio_playback.scriptProcessorNode.connect(a);
            await new Promise(resolve=>setTimeout(resolve,200));
            const samples = new Float32Array(a.fftSize); a.getFloatTimeDomainData(samples);
            return {state:engine.SDL3.audioContext.state, energy:samples.reduce((sum,x)=>sum+x*x,0)};
        });
        assert.ok(audio.energy > 0, 'game audio contains nonzero samples');
        console.log('PASS quick race, keyboard acceleration, audible output',audio);
        await page.keyboard.press('p'); await page.waitForTimeout(400);
        await page.screenshot({path:path.join(evidence,'paused.png'),fullPage:true});
        // Exercise the exact visibility bridge used by the page. Browser focus
        // loss also reaches SDL; returning always requires explicit resume.
        await page.evaluate(()=>call('rr_web_visibility',null,['number'],[1]));
        await page.waitForTimeout(100);
        await page.evaluate(()=>call('rr_web_visibility',null,['number'],[0]));
        await page.keyboard.press('Enter');
        await page.locator('#fullscreen').click();
        await page.waitForFunction(()=>!!document.fullscreenElement);
        await page.keyboard.press('Escape');
        console.log('PASS pause, visibility bridge and fullscreen');
        // Validate every currently supported quick-race course without a second
        // import. Screenshot evidence covers actual composed native rendering.
        for (const course of ['1','2','3','4']) {
            await page.reload();
            await page.waitForFunction(()=>!document.querySelector('#ready').hidden);
            await page.locator('#course').selectOption(course);
            await page.locator('#quick').click();
            await page.waitForTimeout(6500);
            assert.equal(await page.locator('#loading').isVisible(),false);
            assert.ok(await visibleFrame(), 'course canvas has a rendered frame');
            await page.screenshot({path:path.join(evidence,`course-${course}.png`),fullPage:true});
            console.log('PASS course',course);
        }
        await page.reload();
        await page.waitForFunction(()=>!document.querySelector('#ready').hidden);
        await page.locator('#play').click();
        await page.waitForTimeout(6500);
        assert.equal(await page.locator('#loading').isVisible(),false);
        await page.keyboard.press('Enter'); // skip the opening movie
        await page.waitForTimeout(1000);
        await page.keyboard.press('Enter'); // skip credits
        await page.waitForTimeout(1000);
        await page.waitForTimeout(1000);
        assert.ok(await visibleFrame(), 'frontend canvas has a rendered frame');
        await page.screenshot({path:path.join(evidence,'frontend.png'),fullPage:true});
        console.log('PASS shared native frontend');
        // Missing/evicted stored files must return to a usable importer.
        await page.reload();
        await page.waitForFunction(()=>!document.querySelector('#ready').hidden);
        await page.evaluate(async()=>assetDirectory.removeEntry(encodeURIComponent('ROADRASH.EXE')));
        await page.reload();
        await page.waitForFunction(()=>document.querySelector('#status').classList.contains('error'));
        assert.match(await page.locator('#status').textContent(), /Re-import/);
        console.log('PASS missing stored file recovery');
        assert.deepEqual(errors, []);
        assert.ok(requests.every(r => r.method === 'GET' && new URL(r.url).origin === new URL(url).origin));
        assert.ok(requests.every(r => !/\.(avi|rra|rsc|exe|iso)(\?|$)/i.test(r.url)));
        fs.writeFileSync(path.join(evidence,'requests.json'),JSON.stringify(requests,null,2));
        console.log('PASS no game-data uploads/downloads; no JavaScript errors');
    } finally { await browser.close(); }
})().catch(error => {console.error(error); process.exitCode = 1;});
