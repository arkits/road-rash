// Run against a local built bundle with NODE_PATH pointing to Playwright.
const {chromium} = require('playwright');
const assert = require('node:assert/strict');
const path = require('node:path');
const start = process.env.RR_WEB_START || 'quick';
assert.ok(['quick', 'play'].includes(start));
const url = process.env.RR_WEB_URL || 'http://127.0.0.1:8768';
if (!['localhost', '127.0.0.1'].includes(new URL(url).hostname)) throw new Error('Local fixture only');
(async () => {
    const browser = await chromium.launch({headless:true});
    try {
        const page = await browser.newPage({viewport:{width:1280,height:800}});
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.goto(url);
        await page.waitForFunction(() => document.querySelector('#status').textContent.includes('Engine ready'));
        await page.locator('#folder').setInputFiles(path.resolve(__dirname, '../../cdrom/ROADRASH'));
        await page.waitForFunction(() => !document.querySelector('#ready').hidden, null, {timeout:120000});
        await page.evaluate(() => {
            window.loadingSamples = [];
            new MutationObserver(() => {
                const bar = document.querySelector('#loading-progress');
                if (!document.querySelector('#loading').hidden && bar.hasAttribute('aria-valuenow')) {
                    loadingSamples.push({percent:Number(bar.getAttribute('aria-valuenow')), text:document.querySelector('#loading-count').textContent,
                        width:document.querySelector('#loading-fill').style.width});
                }
            }).observe(document.querySelector('#loading'), {subtree:true,attributes:true,childList:true});
        });
        await page.locator('#' + start).click();
        await page.waitForFunction(() => loadingSamples.some(sample => sample.percent > 0 && sample.percent < 100), null, {timeout:120000});
        await page.screenshot({path:path.resolve(__dirname, '../analysis/loading-progress-20261027.png')});
        await page.waitForFunction(() => loadingSamples.some(sample => sample.percent === 100), null, {timeout:120000});
        await page.waitForFunction(() => document.querySelector('#loading').hidden, null, {timeout:120000});
        const samples = await page.evaluate(() => loadingSamples);
        assert.equal(samples[0].percent, 0);
        assert.match(samples.at(-1).text, /341 of 341 files verified/);
        for (let i = 0; i < samples.length; i++) {
            assert.equal(samples[i].width, samples[i].percent + '%');
            if (i) assert.ok(samples[i].percent >= samples[i-1].percent);
        }
        assert.equal(await page.locator('#loading-progress').getAttribute('aria-valuenow'), null);
        assert.equal(await page.locator('#loading-count').evaluate(node => node.hidden), true);
        assert.deepEqual(errors, []);
        console.log(`PASS actual ${start} startup: ${samples.length} progress samples, 0–100%, 341 verified files, overlay dismissed`);
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
