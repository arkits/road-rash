// Fault injection at the browser boundary; no production telemetry or disc data.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {webcrypto} = require('node:crypto');
const app = fs.readFileSync('web/app.js', 'utf8');
async function fixture(options = {}) {
    const elements = new Map();
    const element = id => {
        const attributes = new Map();
        if (!elements.has(id)) elements.set(id, {hidden:true, disabled:false, value:'0', textContent:'',
            classList:{add(){},remove(){},toggle(){}}, addEventListener(){}, focus(){}, getContext(){},
            dataset:{}, style:{}, lastElementChild:{textContent:''},
            setAttribute(name,value){attributes.set(name,String(value));},
            getAttribute(name){return attributes.get(name) ?? null;},
            removeAttribute(name){attributes.delete(name);}, replaceChildren(){}});
        return elements.get(id);
    };
    const events = {}, reports = [], tracks = [], calls = [], fetches = [], args = [];
    const bytes = new Uint8Array([1,2,3]);
    const hash = Buffer.from(await webcrypto.subtle.digest('SHA-256', bytes)).toString('hex');
    const files = ['TEXT/BADLOC.DLL','TEXT/FUTR.DLL','TEXT/FUTB.DLL','TEXT/FUTD.DLL'].map(path=>({path,hash,size:3}));
    let storageTries = 0, syncTries = 0, factories = 0;
    const cache = {getDirectoryHandle:async()=>cache,getFileHandle:async()=>{throw Object.assign(new Error('missing'),{name:'NotFoundError'});}};
    let context;
    const engine = {FS:{mkdirTree(){},mount(){},unmount(){calls.push('unmount');},filesystems:{IDBFS:{}},
        syncfs(populate, done){syncTries++;done(options.idbBlocked || (!populate && options.saveBlocked) ? new Error('IDB blocked') : null);}},
        HEAPU8:new Uint8Array(10000), _malloc:()=>options.mallocFail ? 0 : 64,
        _free(){},lengthBytesUTF8:s=>s.length,stringToUTF8:s=>args.push(s),
        ccall(name){calls.push(name); if(name==='main') {if(options.gameError) engine.gameError=options.gameError;return options.exitCode || 0;}},
        SDL3:{}};
    const sandbox = {console:{log(){},warn(){}}, File, Uint8Array, DataView, Set, Map, Error, DOMException,
        crypto:webcrypto, performance, setTimeout:fn=>setTimeout(fn,0), setInterval:()=>0, clearInterval(){},
        URL, URLSearchParams, AbortController, history:{replaceState(){}},
        FontFace:class {async load(){return this;}},
        document:{getElementById:element,querySelector:element,querySelectorAll:()=>[],
            fonts:{add(){}},body:{classList:{add(){}}},addEventListener:(name,fn)=>{events[name]=fn;}},
        navigator:{storage:{getDirectory:async()=>{
            storageTries++;
            if(options.opfsBlocked || (options.transient && storageTries===1)) throw Object.assign(new Error('blocked'),{name:options.transient?'UnknownError':'SecurityError'});
            return cache;
        }}},
        fetch:async url=>{
            fetches.push(url);
            if(options.networkBlocked || (options.networkTransient && fetches.length===1)) throw new TypeError('Failed to fetch');
            if(url==='references.json') return {ok:true,json:async()=>files};
            // No rr-netd behind the fixture: the multiplayer panel reports offline.
            if(url==='/net/health') return {ok:false,status:404};
            if(url.endsWith('manifest.json')) return {ok:true,json:async()=>({version:1,files})};
            return {ok:true,arrayBuffer:async()=>bytes.buffer};
        },
        createRoadRash:async module=>{factories++;if(options.wasmBlocked || (options.wasmTransient && factories===1)){module.onAbort('fetch failed');throw new Error('wasm fetch failed');}return engine;},
        location:{reload(){},search:options.search||'',href:'https://localhost/'+(options.search||''),protocol:'https:',host:'localhost'},addEventListener(){}};
    sandbox.window={isSecureContext:true,addEventListener(){},rrTelemetry:{track:(...args)=>tracks.push(args),error:(...args)=>reports.push(args),log(){},gameStarted(){},gameStopped(){calls.push('stopped');}}};
    if(options.missingOPFS) sandbox.navigator.storage={};
    context=vm.createContext(sandbox);
    vm.runInContext(app,context);
    for(let i=0;i<100 && element('ready').hidden && element('retry').hidden;i++) await new Promise(r=>setTimeout(r,2));
    return {element, reports, calls, fetches, engine, events, args, eval:code=>vm.runInContext(code,context),
        counts:()=>({storageTries,syncTries,factories})};
}
(async()=>{
    for(const options of [{opfsBlocked:true},{opfsBlocked:true,idbBlocked:true},{missingOPFS:true},{transient:true},{idbBlocked:true},{wasmTransient:true},{networkTransient:true}]) {
        const f=await fixture(options);
        assert.equal(f.element('ready').hidden,false,JSON.stringify(options));
        assert.deepEqual(f.reports,[]);
        if(options.idbBlocked) assert.match(f.element('cache-note').textContent,/session/);
        if(options.transient) assert.equal(f.counts().storageTries,2);
        if(options.wasmTransient) assert.equal(f.counts().factories,2);
        await f.eval('startGame(true)');
        assert.equal(f.eval('running'),false);
        assert.deepEqual(f.reports,[]);
        if(options.idbBlocked) assert.equal(f.counts().syncTries,2,'no exit sync after failed save mount');
    }
    console.log('PASS blocked OPFS/IndexedDB, transient storage/network/Wasm failures, session-only game exit');
    for(const options of [{wasmBlocked:true},{networkBlocked:true}]) {
        const f=await fixture(options);
        assert.equal(f.element('ready').hidden,true);
        assert.equal(f.element('retry').hidden,false);
        assert.equal(f.reports.length,1,'only exhausted retries are reported');
        assert.equal(f.reports[0][1],'initialization');
    }
    const save=await fixture({saveBlocked:true});
    await save.eval('startGame(true)');
    assert.equal(save.reports[0][1],'save','a save failure is separate from game failure');
    assert.match(save.element('loading-message').textContent,/saves could not be stored/);
    console.log('PASS exhausted downloads offer retry, save failure preserves clean game exit');
    const f=await fixture();
    assert.equal(f.eval('fullscreenRequest()'),undefined);
    await f.element('fullscreen').onclick();
    assert.equal(f.element('fullscreen').disabled,true);
    f.element('.viewport').requestFullscreen=()=>{throw new TypeError('Denied');};
    f.element('loading').hidden=true;
    await f.element('fullscreen').onclick();
    assert.equal(f.element('loading').hidden,true,'denied fullscreen never covers the game');
    let prefixed=0;
    delete f.element('.viewport').requestFullscreen;
    f.element('.viewport').webkitRequestFullscreen=()=>{prefixed++;};
    await f.element('fullscreen').onclick();
    assert.equal(prefixed,1);
    console.log('PASS missing, denied and prefixed fullscreen APIs');
    for(const options of [{exitCode:1,gameError:'vector'},{mallocFail:true}]) {
        const f=await fixture(options);
        await f.eval('startGame(true)');
        assert.equal(f.eval('running'),false);
        assert.equal(f.reports.length,1);
        assert.match(f.reports[0][0].message,options.mallocFail ? /memory/ : /vector/);
        const count=f.calls.length;
        f.events.visibilitychange();
        assert.equal(f.calls.length,count,'no callback into a stopped engine');
    }
    console.log('PASS actual engine error preserved, allocation failures and stopped-engine lifecycle');
    // Multiplayer panel: an unreachable /net/health never blocks play, an
    // invite is offered, and the launch carries the room intent and server.
    const online=await fixture({search:'?room=qxe-tn8'});
    await new Promise(r=>setTimeout(r,5));
    assert.equal(online.element('net-status').dataset.state,'offline');
    assert.equal(online.element('invite').hidden,false);
    assert.equal(online.element('invite-code').textContent,'QXE-TN8');
    assert.deepEqual(online.reports,[]);
    await online.element('invite-watch').onclick();
    assert.equal(online.eval('running'),false);
    const room=online.args.indexOf('--room');
    assert.equal(online.args[room+1],'@watch:QXETN8');
    assert.equal(online.args[online.args.indexOf('--net-server')+1],'wss://localhost/net');
    console.log('PASS offline multiplayer panel, invite banner and online launch arguments');
    const progress=await fixture();
    progress.eval('running = true');
    progress.engine.webLoading('Verifying game files...', 127, 341);
    assert.equal(progress.element('loading-progress').getAttribute('aria-valuenow'),'37');
    assert.equal(progress.element('loading-fill').style.width,'37%');
    assert.match(progress.element('loading-count').textContent,/127 of 341 files verified/);
    assert.equal(progress.element('loading-count').hidden,false);
    progress.engine.webLoading('Preparing video...');
    assert.equal(progress.element('loading-progress').getAttribute('aria-valuenow'),null);
    assert.equal(progress.element('loading-progress').getAttribute('aria-valuetext'),null);
    assert.equal(progress.element('loading-count').hidden,true);
    progress.eval("showGameMessage('Engine error')");
    assert.equal(progress.element('.loading-track').hidden,true);
    assert.equal(progress.element('loading-count').hidden,true);
    console.log('PASS measured loading progress, unknown-stage reset and terminal-error cleanup');
})().catch(error=>{console.error(error);process.exitCode=1;});
