// Real browser DataChannels; optionally exercise the native echo fixture too.
// NODE_PATH points to Playwright; RR_RTC_BUILD to the Emscripten build directory.
const { chromium } = require('playwright');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const build = path.resolve(process.env.RR_RTC_BUILD || 'build-p34-web');

(async () => {
  const allowed = new Set(['net_rtc-web.js','net_rtc-web.wasm','net_transport-web.js','net_transport-web.wasm']);
  const server = http.createServer((request, response) => {
    const url = new URL(request.url, 'http://localhost');
    if (url.pathname === '/') {
      const fixture = url.searchParams.get('fixture') === 'native' ? 'net_transport-web' : 'net_rtc-web';
      response.setHeader('Content-Type','text/html');
      response.end(`<script>var Module={print:console.log,printErr:console.error};</script><script src="/${fixture}.js"></script>`);
    } else {
      const name = url.pathname.slice(1);
      if (!allowed.has(name)) { response.writeHead(404); response.end(); return; }
      response.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':'application/javascript');
      fs.createReadStream(path.join(build,name)).on('error',()=>response.destroy()).pipe(response);
    }
  });
  let browser, echo;
  try {
    await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
    browser = await chromium.launch({headless:true});
    const origin=`http://127.0.0.1:${server.address().port}`;
    async function check(url, marker) {
      const page = await browser.newPage(); let passed=false, failure;
      page.on('console',message=>{const text=message.text(); console.log(text); if(text.includes(marker))passed=true; if(text.startsWith('FAIL '))failure=text;});
      page.on('pageerror',error=>failure=error.message);
      await page.goto(url);
      for(let n=0;n<300&&!passed&&!failure;++n)await page.waitForTimeout(100);
      await page.close();
      if(!passed||failure)throw Error(failure||`Missing ${marker}`);
    }
    await check(origin,'net_rtc OK');
    if (process.env.RR_RTC_ECHO) {
      echo=spawn(path.resolve(process.env.RR_RTC_ECHO),[],{stdio:['ignore','pipe','inherit']});
      const port=await new Promise((resolve,reject)=>{
        let text='';const timeout=setTimeout(()=>reject(Error('native echo did not start')),10000);
        echo.stdout.on('data',chunk=>{text+=chunk;const match=text.match(/echo ready (\d+)/);if(match){clearTimeout(timeout);resolve(Number(match[1]));}});
        echo.once('error',error=>{clearTimeout(timeout);reject(error);});
        echo.once('exit',code=>{clearTimeout(timeout);reject(Error(`native echo exit ${code}`));});
      });
      await check(`${origin}/?fixture=native&url=${encodeURIComponent(`ws://127.0.0.1:${port}/net`)}`,'net_transport browser-native OK');
    }
  } finally {
    if(echo)echo.kill('SIGTERM');
    if(browser)await browser.close();
    await new Promise(resolve=>server.close(resolve));
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
